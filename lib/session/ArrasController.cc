// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ArrasController.h"
#include "ArrasSessions.h"
#include "ComputationDefaults.h"
#include "Computation.h"
#include <node/messages/SessionRoutingDataMessage.h>
#include <node/messages/ComputationStatusMessage.h>
#include <node/messages/RouterInfoMessage.h>
#include <node/messages/ClientConnectionStatusMessage.h>
#include "EventHandler.h"

#include <execute/ProcessManager.h>
#include <execute/SpawnArgs.h>
#include <message_impl/messaging_version.h>
#include <message_impl/PeerMessageEndpoint.h>
#include <shared_impl/RegistrationData.h>
#include <core_messages/ControlMessage.h>
#include <core_messages/EngineReadyMessage.h>
#include <core_messages/ExecutorHeartbeat.h>

namespace {
    std::chrono::milliseconds WAIT_FOR_ROUTER_HAS_ROUTING(10000);

    // wait for router to send back a message containing its message port
    unsigned WAIT_FOR_ROUTER_PORT_COUNT(100);
    std::chrono::milliseconds WAIT_FOR_ROUTER_PORT_INTERVAL(100); 
    // total timeout for router port is 10 seconds

    const char* ROUTER_PROGRAM = "arras4_router";
}

namespace arras4 {
    namespace node {

ArrasController::ArrasController(const api::UUID& nodeId,
				 ArrasSessions& sessions) :
    mNodeId(nodeId), 
    mSessions(sessions),
    mDispatcher("service", *this, impl::MessageDispatcher::NO_IDLE, this),
    mEventHandler(nullptr),
    mExiting(false)
{}

ArrasController::~ArrasController()
{
    mExiting = true;
    mDispatcher.postQuit();
    mDispatcher.waitForExit();
}

// start the node router as a separate process
bool ArrasController::startRouter(const ComputationDefaults& defaults,
                                  impl::ProcessManager& processManager)
{
    mRouterProcessId = api::UUID::generate();
    impl::Process::Ptr pp = processManager.addProcess(mRouterProcessId,
						      "arras4_router",
						      mRouterProcessId);
    if (!pp) {
        ARRAS_ERROR(log::Id("routerCreateFail") <<
                    "Failed to create Router process object");
        return false;
    }
    impl::SpawnArgs sa;
    sa.program = ROUTER_PROGRAM;
    sa.args.push_back("--nodeid");
    sa.args.push_back(mNodeId.toString());
    sa.args.push_back("--ipcName");
    sa.args.push_back(defaults.ipcName);
    sa.args.push_back("-l");
    sa.args.push_back(std::to_string(defaults.logLevel));
    sa.args.push_back("--athena-env");
    sa.args.push_back(defaults.athenaEnv);
    sa.args.push_back("--athena-host");
    sa.args.push_back(defaults.athenaHost);
    sa.args.push_back("--athena-port");
    sa.args.push_back(std::to_string(defaults.athenaPort));
    sa.environment.setFromCurrent();
    sa.setCurrentWorkingDirectory();
    
    impl::StateChange sc = pp->spawn(sa);
    if (!impl::StateChange_success(sc)) {
        ARRAS_ERROR(log::Id("routerSpawnFail") <<
                    "Failed to spawn router process");
        return false;
    }
    return true;
}

// open an IPC messaging channel to the router. This enables
// communication with both the router itself and with the
// computations running on this node.
bool ArrasController::connectToRouter(const ComputationDefaults& defaults)
{
    try {
        mRouterPeer.connect(defaults.ipcName); 
        impl::RegistrationData regData(ARRAS_MESSAGING_API_VERSION_MAJOR,
                                       ARRAS_MESSAGING_API_VERSION_MINOR,
                                       ARRAS_MESSAGING_API_VERSION_PATCH);
        regData.mType = impl::REGISTRATION_CONTROL;
        regData.mNodeId = mNodeId;
        mRouterPeer.send_or_throw(&regData, sizeof(regData), "to router");
    } 
    catch (std::exception&) {
        return false;
    }

    std::string traceInfo("N:" + mNodeId.toString() + " service");
    std::shared_ptr<impl::PeerMessageEndpoint>  endpoint =
        std::make_shared<impl::PeerMessageEndpoint>(mRouterPeer, true, traceInfo);
    mDispatcher.startQueueing(endpoint);
    mDispatcher.startDispatching();

    // we can't register the node until we know its inet port number, which
    // is decided by the router and sent back to us in a message
    for (unsigned i = 0; i < WAIT_FOR_ROUTER_PORT_COUNT; i++) {
	if (mRouterInetPort > 0) break;
	std::this_thread::sleep_for(WAIT_FOR_ROUTER_PORT_INTERVAL);
    }
    if (mRouterInetPort == 0) {
	ARRAS_ERROR(log::Id("routerConnectFail") <<
                    "Did not receive internet port number from router within timeout");
	return false;
    }
    return true;
}
 
   
void
ArrasController::onDispatcherExit(impl::DispatcherExitReason reason) 
{
    // Lost router connection: arras node should shut down
    ARRAS_ERROR(log::Id("dispatcherExit") << "Lost router connection");

    // Do nothing if arras controller is already exiting/shutting down.
    if (mExiting) return;

    api::Object data;
    data["eventType"] = "shutdownWithError";
    data["reason"] = "Lost router connection";
    data["DispatcherExitReason"] = static_cast<int>(reason);
    data["nodeId"] = mNodeId.toString();
    data["routerProcessId"] = mRouterProcessId.toString();
    handleEvent(api::UUID(), api::UUID(), data);
}

// handle messages coming from the router. 
// some of these are handled directly by this library (e.g. ExecutorHeartbeat)
// others generate "events" that get forwarded to Coordinator via NodeService
void
ArrasController::handleMessage(const api::Message& message)
{
    ARRAS_DEBUG("Received from router: " << message.describe());
    if (message.classId() == RouterInfoMessage::ID) {
	// router responds to initial connection by sending back a message with its
	// internet message port number, which is needed to register the node
	RouterInfoMessage::ConstPtr info = message.contentAs<RouterInfoMessage>();
	mRouterInetPort = info->mMessagePort;
    } else if (message.classId() == SessionRoutingDataMessage::ID) {
	// router acknowledging receipt of routing data from us
        SessionRoutingDataMessage::ConstPtr routingData = message.contentAs<SessionRoutingDataMessage>();
        if (routingData) {
	    if (routingData->mAction == SessionRoutingAction::Acknowledge) {
		std::unique_lock<std::mutex> lock(mMutex);
		mRouterHasRoutingData[routingData->mSessionId.toString()] = true;
		mCondition.notify_all();
	    } else {
		ARRAS_ERROR(log::Id("InvalidRoutingDataAction") <<
			    "ArrasController expected to receive SessionRoutingDataMessage " <<
			    "with 'Acknowledge' action, but got action (" <<
			    static_cast<int>(routingData->mAction) << ")");
	    }
        }
    } else if (message.classId() == ComputationStatusMessage::ID) {
	// status message from router : is always "computation ready", sent when computation process
	// registers back with router IPC socket. This causes a notification to go to Coordinator
        ComputationStatusMessage::ConstPtr statusMsg = message.contentAs<ComputationStatusMessage>();
        if (statusMsg) {
            // ARRAS-3500 statusMsg.mStatus isn't actually serialized
            // but we can assume it is always "ready"
	    api::Object data;
	    data["eventType"] = "computationReady";
	    handleEvent(statusMsg->mSessionId,statusMsg->mComputationId,data);
        }
    } else if (message.classId() == ClientConnectionStatus::ID) {
	// router telling us that a client has connected to or disconnected from a session
	ClientConnectionStatus::ConstPtr msg = message.contentAs<ClientConnectionStatus>();
	if (msg->mReason == "connected") {
	    ARRAS_DEBUG(log::Session(msg->mSessionId.toString()) <<
		       "Client has connected to session");

	    Session::Ptr session = mSessions.getSession(msg->mSessionId);
	    if (session) {
		if (session->isActive()) {
                    // sessions on the entry node expire if client doesn't connect quickly enough :
	            // since the client has now connected, clear the expiration time on the session
		    session->stopExpiration();
		} else {
		    // connection to a defunct session is part of a race condition :
		    // see comment on "kickClient"
		    kickClient(msg->mSessionId,"sessionDeleted",session->getDeleteReason());
		}
	    } else {
		// attempted connection to an unknown id causes the client
		// to be immediately kicked. In this case there is no "execStoppedReason"
		// to report
		 kickClient(msg->mSessionId,"unknownSession","unknownSession");
	    }
	} else {
	    // client has disconnected : generate event
	    // this will generally cause session to be deleted
	    api::Object data;
	    data["eventType"] = "sessionClientDisconnected";
	    data["reason"] = msg->mReason;
	    handleEvent(msg->mSessionId,api::UUID(),data);
	}
    } else if (message.classId() == impl::ExecutorHeartbeat::ID) {
	// performance stats being send from a computation via the router
	impl::ExecutorHeartbeat::ConstPtr msg = message.contentAs<impl::ExecutorHeartbeat>();
	// message doesn't have "from" ids, so get it from message metadata
	api::Object fromVal = message.get("from");
	api::Address fromAddr;
	try {
	    fromAddr.fromObject(fromVal);
	    Computation::Ptr cp = mSessions.getComputation(fromAddr.session,fromAddr.computation);
	    if (cp)
		cp->onHeartbeat(msg);
	} catch (std::exception& ex) {
	    ARRAS_ERROR(log::Id("InvalidHeartbeat") <<
			"Cannot get 'from' address from ExecutorHeartbeat message: " <<
			ex.what());
	}
    }
}

// called to deal with an event e.g. computation ready or terminated
// that needs to be forwarded to an external observer (e.g. Coordinator)
// compId can be null for session-level events
void
ArrasController::handleEvent(const api::UUID& sessionId,
			     const api::UUID& compId,
			     api::ObjectConstRef eventData)
{
    if (mEventHandler)
	mEventHandler->handleEvent(sessionId,compId,eventData);
}

// called to attempt to terminate a process by asking
// it politely to stop. The process state mutex
// is held when this is called
bool 
ArrasController::sendStop(const api::UUID& id, const api::UUID& sessionId)
{
    return sendControl(id,sessionId,"stop");
}

// send a "control" message to a computation via router.
bool
ArrasController::sendControl(const api::UUID& id, const api::UUID& sessionId,
                             const std::string& command, api::ObjectConstRef data)
{
    ARRAS_DEBUG(log::Session(sessionId.toString()) <<
               "Sending control '" << command << "' to " << 
               id.toString());
    std::string dataStr;
    if (!data.isNull()) {
        dataStr = api::objectToString(data);
    }
    impl::ControlMessage* message = new impl::ControlMessage(command,dataStr,std::string());
    impl::Envelope envelope(message);
    api::Address addr(sessionId, mNodeId, id);
    envelope.to().push_back(addr);
    mDispatcher.send(envelope);
    return true;
}



void
ArrasController::run()
{
    mDispatcher.waitForExit();
}
  
void
ArrasController::stopRunning()
{
    mDispatcher.postQuit();
}

// initialize the session with router by sending it the session
// routing data
bool ArrasController::initializeSession(const SessionConfig& config)
{
    std::string routingString = api::objectToString(config.getRouting());
    SessionRoutingDataMessage* message = new SessionRoutingDataMessage(SessionRoutingAction::Initialize,
								       config.sessionId(), routingString);
    impl::Envelope envelope(message);
    mDispatcher.send(envelope);

    std::unique_lock<std::mutex> lock(mMutex);
    auto expiration = std::chrono::system_clock::now() + WAIT_FOR_ROUTER_HAS_ROUTING;
    while (!mRouterHasRoutingData[config.sessionId().toString()]) {
        if (mCondition.wait_until(lock, expiration) == std::cv_status::timeout) {
            return false;
        }
    }
    return true;
}
   
// update the session routing data with router
// -- currently this updates the ClientAddresser : computation addressers
// are updated via a signal
void ArrasController::updateSession(const api::UUID& sessionId,
				    api::ObjectConstRef data)
{
    std::string routingString = api::objectToString(data["routing"]);
    SessionRoutingDataMessage* message = new SessionRoutingDataMessage(SessionRoutingAction::Update,
								       sessionId, routingString);
    impl::Envelope envelope(message);
    mDispatcher.send(envelope);
}

// send a message to the router asking it to disconnect a client
// this is used when a session is deleted, but also when a connection
// has been made to a defunct session. There is a potential race condition
// between these two cases, so we arrange that connection just after
// deletion looks (to the client) much the same as deletion just after
// connection. The difference is detectable in the "disconnectReason" field,
// which should therefore be used with care.
void ArrasController::kickClient(const api::UUID& sessionId,
				 const std::string& disconnectReason,
                                 const std::string& stoppedReason)
{
    ClientConnectionStatus* kick= new ClientConnectionStatus;
    kick->mSessionId = sessionId;
    kick->mReason = disconnectReason;
    // previously complete session status would be sent to the client just before disconnect
    // but this is more misleading than useful because it only includes computations
    // on this node. The computation list is no longer included
    api::Object disconnectStatus;
    disconnectStatus["disconnectReason"] = disconnectReason;
    disconnectStatus["execStatus"] = "stopped";
    disconnectStatus["execStoppedReason"] = stoppedReason;

    kick->mSessionStatus = api::objectToString(disconnectStatus);
    impl::Envelope envelope(kick);
    mDispatcher.send(envelope);
}

// shutdown a session with router
void ArrasController::shutdownSession(const api::UUID& sessionId,
				      const std::string& reason)
{
    // tell router to close any connection to the session client
    kickClient(sessionId,reason,reason);

    // tell router that it can release routing info for this session
    SessionRoutingDataMessage* msg = new SessionRoutingDataMessage(SessionRoutingAction::Delete,sessionId);
    impl::Envelope envelope2(msg);
    mDispatcher.send(envelope2);
}

// tell the client that the session is ready, via router
void ArrasController::signalEngineReady(const api::UUID& sessionId)
{
    impl::EngineReadyMessage* message = new impl::EngineReadyMessage(); 
    impl::Envelope envelope(message);
    // empty comp and node ids mean message goes to client
    api::Address addr(sessionId, api::UUID(), api::UUID());
    envelope.to().push_back(addr);
    mDispatcher.send(envelope);
}

// generate an event when a create or modify session operation goes wrong.
// Currently we just tell the Coordinator to delete the session altogether
void ArrasController::sessionOperationFailed(const api::UUID& sessionId,
					     const std::string& opname,
					     const std::string& message)
{
    ARRAS_ERROR(log::Id("SessionOpFailed") <<
		log::Session(sessionId.toString()) <<
		"Session operation '" << opname << "' failed : " << message);
    api::Object data;
    data["eventType"] = "sessionOperationFailed";
    data["reason"] = message;
    handleEvent(sessionId,api::UUID(),data);
}

// generate an event when a session on the entry node expires because
// the client hasn't connected quickly enough
// Currently we just tell the Coordinator to delete the session altogether
void ArrasController::sessionExpired(const api::UUID& sessionId,
				     const std::string& message)
{
    ARRAS_WARN(log::Id("SessionExpired") <<
	       log::Session(sessionId.toString()) <<
	       "Session expired : " << message);
    api::Object data;
    data["eventType"] = "sessionExpired";
    data["reason"] = message;
    handleEvent(sessionId,api::UUID(),data);
}

}
}
