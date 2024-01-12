// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "NodeRouter.h"
#include "ClientRemoteEndpoint.h"
#include <node/messages/ClientConnectionStatusMessage.h>
#include <node/messages/RouterInfoMessage.h>

#include "ListenServer.h"
#include "PeerManager.h"
#include "RouteMessage.h"
#include "SessionRoutingData.h"
#include <node/messages/SessionRoutingDataMessage.h>
#include <core_messages/EngineReadyMessage.h>
#include <core_messages/ControlMessage.h>
#include <core_messages/SessionStatusMessage.h>

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <network/Peer.h>
#include <http/HttpResponse.h>
#include <http/HttpRequest.h>
#include <shared_impl/MessageQueue.h>
#include <shared_impl/RegistrationData.h>
#include <message_impl/messaging_version.h>
#include <message_impl/Envelope.h>

#include <sys/select.h>
#include <assert.h>
#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>
#include <vector>

// HTTP headers are used for responding to the WebSocket requests from clients
// More headers are used by lower level libraries
#ifdef TEST_CASE_INSENSITIVITY
#define HTTP_CONNECTION "cOnNection"
#define HTTP_ARRAS_MESSAGING_API_VERSION "x-ARRas-MEssaGing-API-Version"
#else
#define HTTP_CONNECTION "Connection"
#define HTTP_ARRAS_MESSAGING_API_VERSION "X-Arras-Messaging-API-Version"
#endif

namespace {

// number of milliseconds for listen server to wait in poll() before timing out
const int LISTEN_SERVER_POLL_TIMEOUT = 1000;

constexpr int NEGOTIATION_TIMEOUT = 5000; // time in milliseconds that a connector needs to identify itself

// time in milliseconds wait for SessionStatusMessage to be sent before giving up
constexpr int SESSIONSTATUSMESSAGE_DRAIN_TIMEOUT = 5000;

// time in microseconds to wait on the service to router queue before checking for an exit request
constexpr unsigned long SERVICETOROUTER_QUEUE_TIMEOUT_USEC= 500000; // timeout every 1/2 of a second

} // namespace

using namespace arras4::api;
using namespace arras4::network;
using namespace arras4::impl;

namespace arras4 {
namespace node {

    NodeRouter::NodeRouter(const UUID& aNodeId, unsigned short aInetSocket, unsigned short aIpcSocket)
    : mThreadedNodeRouter(aNodeId)
    , mNetwork(new SocketPeer(aInetSocket))
    , mIPC(new SocketPeer(aIpcSocket))
    , mRequestShutdown(false)
{
}

NodeRouter::~NodeRouter()
{
}

SessionRoutingData::Ptr
NodeRouter::putSessionRoutingData(const UUID& aSessionId, ObjectConstRef aRoutingData)
{
    SessionRoutingData::Ptr data = mThreadedNodeRouter.sessionRoutingData(aSessionId);
    if (data)
        return data;
    return mThreadedNodeRouter.addSessionRoutingData(aSessionId,aRoutingData);
}           
    
SessionRoutingData::Ptr 
NodeRouter::getSessionRoutingData(const UUID& aSessionId)
{
    return mThreadedNodeRouter.sessionRoutingData(aSessionId);
}

void
NodeRouter::start()
{
    // start the routing thread
    mThread = std::thread(&NodeRouter::threadProc, this);
    mServiceToRouterThread = std::thread(&NodeRouter::serviceToRouterProc, this);
}

struct PeerConnectFilterContext : public ListenServer::ConnectFilterContext
{
    PeerConnectFilterContext() : 
        mRegData(ARRAS_MESSAGING_API_VERSION_MAJOR,
                 ARRAS_MESSAGING_API_VERSION_MINOR,
                 ARRAS_MESSAGING_API_VERSION_PATCH),
        mFailed(false) {}
    RegistrationData mRegData;
    bool mFailed;
};

PeerConnectFilterContext*
ReadRegistrationData(Peer* aPeer)
{
    PeerConnectFilterContext* ctx = new PeerConnectFilterContext;

    // no response in 5 seconds is considered a failed connection
    // only read as far as the protocol major version before reading the rest of the RegistrationData
    size_t preReadSize = sizeof(ctx->mRegData.mMagic) + sizeof(ctx->mRegData.mMessagingAPIVersionMajor);
    aPeer->receive_all_or_throw(&ctx->mRegData.mMagic, preReadSize, "server.addEndpointConnectFilter", NEGOTIATION_TIMEOUT);
    if (ctx->mRegData.mMagic != RegistrationData::MAGIC) {
        ARRAS_ERROR(log::Id("BadConnectionAttempt") <<
                    "Invalid registration block received from socket : someone may be attempting an unsupported connection type");
        ctx->mFailed = true;
        return ctx;
    }
    if (ctx->mRegData.mMessagingAPIVersionMajor != ARRAS_MESSAGING_API_VERSION_MAJOR) {
        ARRAS_ERROR(log::Id("BadAPIVersion") <<
                    "Messaging API version mismatch from TCP connection. Found major version " <<
                    ctx->mRegData.mMessagingAPIVersionMajor << " require " << ARRAS_MESSAGING_API_VERSION_MAJOR);
        ctx->mFailed = true;
        return ctx;
    }

    // read the rest of the registration structure
    aPeer->receive_all_or_throw((char*)(&ctx->mRegData) + preReadSize,
                                sizeof(ctx->mRegData) - preReadSize,
                                "server.addEndpointConnectFilter",
                                NEGOTIATION_TIMEOUT);

    return ctx;
}

void
NodeRouter::sendSessionStatusToClient(const std::string& aSessionStatusJson, RemoteEndpoint& aEndPoint)
{
    Envelope envelope(new SessionStatusMessage(aSessionStatusJson));
    aEndPoint.queueEnvelope(envelope);
}

//
// This will eventially be just another RemoteEndpoint which is
// connected to NodeService
//
void
NodeRouter::serviceToRouterProc()
{
    // set a thread specific prefix for log messages from this thread
    arras4::log::Logger::instance().setThreadName("service_to_router");

    while (mRun) {
        impl::Envelope envelope = mThreadedNodeRouter.popServiceToRouterQueue(SERVICETOROUTER_QUEUE_TIMEOUT_USEC);

        // check for a SIGINT or SIGTERM having happened
        if (mRequestShutdown) {
             mThreadedNodeRouter.notifyRouterShutdown();
             mRequestShutdown = false;
        }

        // if it timed out it will get an empty envelope
        if (!envelope.isEmpty()) {
            UUID sessionId;

            if (envelope.classId() == ClientConnectionStatus::ID) {
                ClientConnectionStatus::ConstPtr statusMessage = envelope.contentAs<ClientConnectionStatus>();
                if (statusMessage) {
                    sessionId = statusMessage->mSessionId;
                    std::string sessionIdStr = sessionId.toString();
                    ARRAS_DEBUG(log::Session(sessionIdStr) <<
                               "Received client status notification [reason " <<
                               statusMessage->mReason << "]");

                    // "connected" messages shouldn't be sent to NodeRouter from NodeService
                    // but check for them anyway for robustness. Everything else is a request
                    // to kick the client.
                    if (statusMessage->mReason != "connected") {
                        kickClient(sessionId, statusMessage->mReason, statusMessage->mSessionStatus);
                    }
                }
            } else if (envelope.classId() == SessionRoutingDataMessage::ID) {
                SessionRoutingDataMessage::ConstPtr routingDataMessage = envelope.contentAs<SessionRoutingDataMessage>();
                if (routingDataMessage) {
		    sessionId = routingDataMessage->mSessionId;
                    if (routingDataMessage->mAction == SessionRoutingAction::Initialize) {
                        // initial setup of routing data
			// store the routing information in the local table
			Object object;
			api::stringToObject(routingDataMessage->mRoutingData, object);
			putSessionRoutingData(sessionId, object);
                        // send one back as an acknowledgement
			SessionRoutingDataMessage* message = new SessionRoutingDataMessage(SessionRoutingAction::Acknowledge,
                                                                                           sessionId);
			mThreadedNodeRouter.notifyService(message);
		    }
                    else if (routingDataMessage->mAction == SessionRoutingAction::Update) {
                        // update existing routing data. Currently limited to updating the
			// client addresser.
                        Object object;
                        api::stringToObject(routingDataMessage->mRoutingData, object);
                        SessionRoutingData::Ptr routingData = mThreadedNodeRouter.sessionRoutingData(sessionId);
                        if (routingData) {
                            routingData->updateClientAddresser(object);
                        }
                    }
		    else if (routingDataMessage->mAction == SessionRoutingAction::Delete) {
                        // the router should no longer need the route for this session
			mThreadedNodeRouter.deleteSessionRoutingData(sessionId);
                    }
                }
            } else if ((envelope.classId() == ControlMessage::ID) ||
                       (envelope.classId() == EngineReadyMessage::ID)) {
                // ControlMessages and EngineReadyMessages have been pre-addressed by NodeService so just route it
                if (envelope.classId() == ControlMessage::ID) {
                    ControlMessage::ConstPtr control= envelope.contentAs<ControlMessage>();
                }
                sessionId = envelope.to().front().session;
                SessionRoutingData::Ptr routingData = mThreadedNodeRouter.sessionRoutingData(sessionId);
                if (routingData) {
                    routeMessage(envelope, routingData, mThreadedNodeRouter);
                }
            }
        }
    }
}

void
NodeRouter::threadProc()
{
    ListenServer server;

    // set a thread specific prefix for log messages from this thread
    arras4::log::Logger::instance().setThreadName("router");

    // add our listening sockets
    server.addAcceptor(mNetwork);
    server.addAcceptor(mIPC);

    // add filter to create endpoints on new client connections (standard handler)
    server.addEndpointConnectFilter([&] (Peer* aPeer, ListenServer::ConnectFilterContext** aCtx) -> RemoteEndpoint* {
        ClientRemoteEndpoint* ep = nullptr;

        PeerConnectFilterContext* ctx = static_cast<PeerConnectFilterContext*>(*aCtx);
        if (!ctx) {
            *aCtx = ctx = ReadRegistrationData(aPeer);
        }
        if (ctx->mFailed) return ep;

        if (ctx->mRegData.mType == REGISTRATION_CLIENT) {
            // refuse the client connection if session already has a client
            RemoteEndpoint::Ptr existingEndpoint = mThreadedNodeRouter.findClientPeer(ctx->mRegData.mSessionId);
            if (existingEndpoint) {
	        std::string errorMsg("sessionId:");
                errorMsg += (ctx->mRegData.mSessionId.toString());
                errorMsg += " refusing client connection because one already exists for the session";
                ARRAS_ERROR(log::Id("duplicateClientConnection") << log::Session(ctx->mRegData.mSessionId.toString()) << " refusing client connection because one already exists for the session");
                throw std::runtime_error(errorMsg);
            }

            // check to see if we actually have a session with this ID; it is highly unlikely that a random attack or
            // garbage input will happen upon a valid session UUID
            const SessionRoutingData::Ptr routingData = getSessionRoutingData(ctx->mRegData.mSessionId);
            if (routingData) {
                std::string traceInfo("N:"+ getNodeId().toString() + " client");
                ep = new ClientRemoteEndpoint(aPeer, ctx->mRegData.mSessionId, mThreadedNodeRouter,traceInfo);

                ARRAS_DEBUG(log::Session(ctx->mRegData.mSessionId.toString()) <<
                           "Basic handshake succeeded for client");
            } else {
                // unless something is terribly wrong this is a client
                // connecting after the session has already shut down. We want
                // to allow the connection anyway so we can send back the
                // shutdown status. There will be no routing information though
                // so incoming messages from client will be ignored.
                std::string traceInfo("N:"+ getNodeId().toString() + " client");
                // pass in an invalid UUID so it will allow no routing information
                ep = new ClientRemoteEndpoint(aPeer, UUID(), mThreadedNodeRouter,traceInfo);

                ARRAS_DEBUG(log::Session(ctx->mRegData.mSessionId.toString()) <<
                           "Client for invalid session accepted temporarily");
            }
        }

        if (ep) {
            // add to peer manager for tracking
            mThreadedNodeRouter.trackClient(ctx->mRegData.mSessionId, ep);

            // notify NodeSession that the client connected
            mThreadedNodeRouter.notifyClientConnected(ctx->mRegData.mSessionId);

            ARRAS_DEBUG("New connection is a standard client connection");
        } else {

            ARRAS_TRACE("New connection is not a standard client connection");
        } 

        return ep;
    });

    // add filter to create endpoints on new node connections
    server.addEndpointConnectFilter([&] (Peer* aPeer, ListenServer::ConnectFilterContext** aCtx) -> RemoteEndpoint* {
        RemoteEndpoint* ep = nullptr;

        PeerConnectFilterContext* ctx = static_cast<PeerConnectFilterContext*>(*aCtx);
        if (!ctx) {
            *aCtx = ctx = ReadRegistrationData(aPeer);
        }
        if (ctx->mFailed) return ep;

        if (ctx->mRegData.mType == REGISTRATION_NODE) {
            ARRAS_DEBUG("Registration received from node peer '" << 
                        ctx->mRegData.mNodeId.toString() << "'");

            // Node to node connections can have a multi-system race condition
            // To simplify collision management the approach is to force the
            // final connection to always be from the greater nodeId to the
            // lesser nodeId. 
            //
            // If a greater nodeId receives a connection request from a lesser
            // nodeId before starting a connection it will drop the incoming
            // connection (which the lesser nodeId will expect) and initiate
            // a connection to the lesser nodeId
            //
            // If a greater nodeId receives a connection request from a lesser
            // nodeId after already starting a connection to the lesser nodeId
            // it will simply drop the connection (which the less nodeId
            // will expect).
            //
            // If a lesser nodeId receives a connection request from a greater
            // nodeId before starting a connection it will accept the connection
            // and will never need to attempt to connect the other direction
            //
            // If a lesser nodeId receives a connection request from a greater
            // nodeId after already starting a connection it will pass the 
            // Peer to the existing RemoteEndpoint
            //
            
            // See if one already exists. This would happen when two nodes are contacting each other at the same time
            std::lock_guard<std::mutex> lock(mThreadedNodeRouter.mNodeConnectionMutex);
            std::string traceInfo("N:"+getNodeId().toString()+" N:"+ctx->mRegData.mNodeId.toString());
            RemoteEndpoint::Ptr epPtr = mThreadedNodeRouter.findNodePeer(ctx->mRegData.mNodeId);
            if (!epPtr) {
                if (ctx->mRegData.mNodeId < getNodeId()) {
                    // this is a connection from lesser to greater nodeId so reject the connection and create a reciprocal connection
                    SessionNodeMap::NodeInfo nodeInfo;
                    if (mThreadedNodeRouter.findNodeInfo(ctx->mRegData.mNodeId, nodeInfo)) {
                        ARRAS_DEBUG("Rejecting node to node connection from lesser nodeId. Reciprical connection will be created.");
                        ep = RemoteEndpoint::createNodeRemoteEndpoint(ctx->mRegData.mNodeId, nodeInfo, mThreadedNodeRouter,traceInfo);
                        mThreadedNodeRouter.trackNode(ctx->mRegData.mNodeId, ep);
                        // when ep is set the caller assumes the peer was used. go ahead and delete
                        delete aPeer;
                    } else {
                        ARRAS_ERROR(log::Id("BadNodeConnection") << 
                                    "Unexpected node connection from nodeId " << 
                                    ctx->mRegData.mNodeId.toString());
                        // set ep back to null so this connection is ignored
                        // the caller will destroy the Peer
                        ep = nullptr;
                    }

                    // the lesser nodeId will be expecting this rejection, waiting for the reciprical
                } else {
                    // this isn't a race and it's from greater to lesser nodeId so just let it connect normally
                    ARRAS_DEBUG("Accepting node to node connection from greater nodeId");
                    UUID dummy;
                    ep = new RemoteEndpoint(aPeer, PeerManager::PEER_NODE, ctx->mRegData.mNodeId, dummy, mThreadedNodeRouter,traceInfo);
                    mThreadedNodeRouter.trackNode(ctx->mRegData.mNodeId, ep);
                }
            } else {
                ep = epPtr.get();
                if (ctx->mRegData.mNodeId < getNodeId()) {

                    ARRAS_DEBUG("Rejecting node to node connection from lesser nodeId. Reciprical connection is already in progress.");
                    // Don't need this connection. One is already in progress in the correct direction
                    // when ep is set the caller assumes the peer was used. go ahead and delete the peer
                    delete aPeer;
                } else {
                    ARRAS_DEBUG("Accepting node to node connection from greater nodeId. Using for existing RemoteEndpoint.");
                    // Use this Peer in the existing RemoteEndpoint the other Peer will be destroyed
                    ep->setPeer(aPeer);
                }
            }
        }

        if (ep == nullptr) {
            ARRAS_TRACE("New connection is not a node connection");
        } else {
            ARRAS_DEBUG("New connection is a node connection");
        }

        return ep;
    });

    // add filter to create endpoints on new IPC connections
    server.addEndpointConnectFilter([&] (Peer* aPeer, ListenServer::ConnectFilterContext** aCtx) -> RemoteEndpoint* {
        RemoteEndpoint* ep = nullptr;

        PeerConnectFilterContext* ctx = static_cast<PeerConnectFilterContext*>(*aCtx);
        if (!ctx) {
            *aCtx = ctx = ReadRegistrationData(aPeer);
        }
        if (ctx->mFailed) return ep;

        if (ctx->mRegData.mType == REGISTRATION_EXECUTOR) {
            ARRAS_DEBUG(log::Session(ctx->mRegData.mSessionId.toString()) <<
                       "Registration received from computation '" <<
                       ctx->mRegData.mComputationId.toString() << "'");
            mThreadedNodeRouter.notifyComputationStatus(ctx->mRegData.mSessionId, ctx->mRegData.mComputationId, "ready");
            std::string traceInfo("N:"+getNodeId().toString() + " C:"+ctx->mRegData.mComputationId.toString());
            ep = new RemoteEndpoint(aPeer, PeerManager::PEER_IPC, ctx->mRegData.mComputationId, 
                                    ctx->mRegData.mSessionId, mThreadedNodeRouter,traceInfo);
            mThreadedNodeRouter.trackIpc(ctx->mRegData.mComputationId, ep);

        }

        if (ep == nullptr) {
            ARRAS_TRACE("New connection on is not an IPC connection");
        } else {
            ARRAS_DEBUG(log::Session(ctx->mRegData.mSessionId.toString()) <<
                       "New connection is an IPC connection");
        }
        return ep;
    });


    // add filter to create an endpoint on new nodeservice connection
    server.addEndpointConnectFilter([&] (Peer* aPeer, ListenServer::ConnectFilterContext** aCtx) -> RemoteEndpoint* {
        RemoteEndpoint* ep = nullptr;

        PeerConnectFilterContext* ctx = static_cast<PeerConnectFilterContext*>(*aCtx);
        if (!ctx) {
            *aCtx = ctx = ReadRegistrationData(aPeer);
        }
        if (ctx->mFailed) return ep;

        if (ctx->mRegData.mType == REGISTRATION_CONTROL) {
            // refuse the service connection if  NodeService has already connected
            if (mThreadedNodeRouter.getServiceEndpoint()) {
                ARRAS_ERROR(log::Id("duplicateServiceConnection") << " refusing service connection because one already exists");
                throw std::runtime_error("refusing service connection because one already exists");
            }

            std::string traceInfo("N:"+ getNodeId().toString() + " service");
            UUID dummy;
            ep = new RemoteEndpoint(aPeer, PeerManager::PEER_SERVICE, ctx->mRegData.mNodeId, dummy, mThreadedNodeRouter,traceInfo);

	    // send a message back with info about the router. this only works for the
	    // standalone_router case, when node service needs to be told the port
	    if (mInetPort != 0) {
		RouterInfoMessage* infoMsg = new RouterInfoMessage();
		infoMsg->mMessagePort = mInetPort;
		impl::Envelope env(infoMsg);
		ep->queueEnvelope(env);
	    }

            mThreadedNodeRouter.setServiceEndpoint(ep);
            ARRAS_DEBUG("Basic handshake succeeded for node service");

        } else {

            ARRAS_TRACE("New connection is not a service connection");
        } 

        return ep;
    });


    // set it in motion
    while (mRun) {
        try {
            // blocking wait for activity, no longer than 1 second
            server.poll(LISTEN_SERVER_POLL_TIMEOUT);

            //
            // destroy any endpoints which have disconnected or been kicked
            //
            mThreadedNodeRouter.destroyEndpoints();

        } catch (const PeerException& e) {
            // TODO: usually happens on a non-disconnect network exception; what to do?
            ARRAS_ERROR(log::Id("PeerException") <<
                        "Peer Exception in node main thread: " << std::string(e.what()));
            if (e.code() != PeerException::CONNECTION_CLOSED && e.code() != PeerException::CONNECTION_RESET) break;
        } catch (const std::exception& e) { 
            ARRAS_ERROR(log::Id("NodeRouterException") <<
                        "Exception in node main thread: " << std::string(e.what()));
            break;
        } catch (...) { 
            ARRAS_ERROR(log::Id("NodeRouterException") <<
                        "Unknonw exception in node main thread");
            break;
        }
    }
}

void
NodeRouter::stop()
{
    mRun = 0;

    // ListenServer took ownership of mNetwork and mIPC, so do not delete those here

    if (mThread.joinable()) mThread.join();
    if (mServiceToRouterThread.joinable()) mServiceToRouterThread.join();
}

void
NodeRouter::requestShutdown()
{
    mRequestShutdown = true;
}

void
NodeRouter::kickClient(const UUID& aSessId, const std::string& aReason, const std::string& statusJson)
{
    ARRAS_DEBUG(log::Session(aSessId.toString()) <<
                "Disconnecting client for reason: " << aReason);

    // disconnect the client for the given session
    RemoteEndpoint::Ptr epPtr = mThreadedNodeRouter.findClientPeer(UUID(aSessId));
    if (epPtr) {
    
        ClientRemoteEndpoint* ep = static_cast<ClientRemoteEndpoint*>(epPtr.get());

        sendSessionStatusToClient(statusJson, *ep);
        
        // try to make sure that messages are sent before shutting down the connection to the client
        // but don't wait more that 5 seconds. It shouldn't take nearly that long normally.
        ep->drain(std::chrono::milliseconds(SESSIONSTATUSMESSAGE_DRAIN_TIMEOUT));
        ep->flagForDestruction();   
        ARRAS_DEBUG(log::Session(aSessId.toString()) <<
                   "Disconnected client");
    } else {
        ARRAS_DEBUG(log::Session(aSessId.toString()) <<
                   "There was no client to disconnect");

        // clear any pending messages for this client
        mThreadedNodeRouter.clearStashedEnvelopes(aSessId);
    }
}

} // namespace service
} // namespace arras

