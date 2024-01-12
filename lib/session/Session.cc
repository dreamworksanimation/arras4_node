// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "Session.h"
#include "SessionError.h"
#include "ComputationConfig.h"
#include "ArrasController.h"

#include <http/http_types.h>

#include <chrono>
#include <sys/time.h>

using namespace arras4::network;

namespace {
    // time to wait for all running processes to terminate before giving up
    const std::chrono::milliseconds WAIT_FOR_SHUTDOWN_TIMEOUT(30000); // 30 seconds
}

namespace arras4 {
    namespace node {

Session::Session(const api::UUID& sessionId, 
                 const api::UUID& nodeId,
                 const ComputationDefaults& computationDefaults,
                 impl::ProcessManager& processManager,
                 ArrasController& arrasController)
    : mId(sessionId), mNodeId(nodeId),
      mComputationDefaults(computationDefaults),
      mLogLevel(3), mProcessManager(processManager), 
      mArrasController(arrasController), mState(SessionState::Free),
      mDeleteReason("Not Deleted")
{
    // initialize mLastActivitySecs to a reasonable value
    timeval now;
    gettimeofday(&now, nullptr);
    mLastActivitySecs = now.tv_sec;
}

// ~Session makes sure no operations are running for safety,
// but normally you should have already called syncShutdown
Session::~Session()
{
    // stop new operations
    {
	std::lock_guard<std::mutex> lock(mStateMutex);
	mShuttingDown = true;
    }
    if (mOperationThread.joinable())
        mOperationThread.join();
    // terminate any expiration
    stopExpiration();
}

Computation::Ptr Session::getComputation(const api::UUID& id)
{
    if (id.isNull()) return Computation::Ptr();
    std::lock_guard<std::mutex> lock(mComputationsMutex);
    return getComputation_wlock(id);
}

Computation::Ptr Session::getComputation_wlock(const api::UUID& id)
{
    std::map<api::UUID,Computation::Ptr>::iterator it = mComputations.find(id);
    if (it == mComputations.end()) {
        return Computation::Ptr();
    }
    return it->second;
}

SessionState Session::getState()
{
    std::lock_guard<std::mutex> lock(mStateMutex);
    return mState;
}

std::string Session::getDeleteReason()
{
    std::lock_guard<std::mutex> lock(mStateMutex);
    return mDeleteReason;
}

bool Session::isActive()
{
    return getState() != SessionState::Defunct;
}

bool Session::isAutoSuspend() const 
{ 
    return mComputationDefaults.autoSuspend; 
}

api::Object Session::getStatus()
{
    api::Object status;
    status["state"] = SessionState_string(getState());
    api::ObjectRef comps = status["computations"];
    std::lock_guard<std::mutex> lock(mComputationsMutex);
    for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
	Computation::Ptr cp = jt->second;
        comps[cp->name()] = cp->getStatus();
    }
    return status;
}

api::Object Session::getPerformanceStats()
{
    api::Object stats;
    api::ObjectRef comps = stats["computations"];
    std::lock_guard<std::mutex> lock(mComputationsMutex);
    for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
	Computation::Ptr cp = jt->second;
        cp->getPerformanceStats(comps[cp->name()]);
    }
    return stats;
}

long Session::getLastActivitySecs(bool includeComputations) const
{
    long ret = mLastActivitySecs;
    if (includeComputations) {
	std::lock_guard<std::mutex> lock(mComputationsMutex);
	for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
	    long cla = jt->second->getLastActivitySecs();
	    if (cla > ret) ret = cla;
	}
    }
    return ret;
}
    
void Session::checkIsFree()
{
     std::lock_guard<std::mutex> lock(mStateMutex);
     if (mState == SessionState::Busy)
         throw SessionError("Session is busy",HTTP_RESOURCE_CONFLICT);
     if (mState == SessionState::Defunct)
         throw SessionError("Session is defunct",HTTP_RESOURCE_CONFLICT);
}

void Session::signal(api::ObjectConstRef signalData)
{
    
    checkIsFree();
    std::string status;
    if (signalData["status"].isString()) {
        status = signalData["status"].asString();
	ARRAS_DEBUG(log::Session(mId.toString()) << 
		   "Session signal " << status);
    }

    if (status == "run") {
        // "run" goes to all computations
        signalAll(signalData);
	// may also need to update client routing
	if (!signalData["routing"].isNull())
	    mArrasController.updateSession(mId,signalData);
    } 
    else if (status == "engineReady") {
	// "engineReady" goes to client via router
	mArrasController.signalEngineReady(mId);
    } 
    else {
	ARRAS_WARN(log::Id("unknownSignal") <<
		   log::Session(mId.toString()) <<
		   "Unknown signal received : " << 
		   api::objectToString(signalData));
    }
    timeval now;
    gettimeofday(&now, nullptr);
    mLastActivitySecs = now.tv_sec;
	
}

void Session::signalAll(api::ObjectConstRef signalData)
{
    std::lock_guard<std::mutex> lock(mComputationsMutex);
    for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
        jt->second->signal(signalData);
    }
}

// the "async" operations start a background thread to modify the session.
// as they run, notifications, like "computation ready" and "computation
// terminated" will be sent back to Coordinator.
//
// Only one operation can be running at any time, to prevent interference.
// If you attempt to initiate an operation while one is already running,
// the a "busy" exception is thrown. This causes node to respond with a
// HTTP_RESOURCE_CONFLICT status to the requester (i.e. Coordinator).
// There are two deliberate policies here:
//     -- node responds promptly to the HTTP request (i.e. doesn't wait for
//        the session to be "not busy"
//     -- requests are not queued, because this would make it harder for 
//	  Coordinator to track asynchronous notification resulting from the
//        operation.
void Session::asyncUpdateConfig(SessionConfig::Ptr newConfig)
{
    // check session and node ids match
    if (mId != newConfig->sessionId())
        throw SessionError("Config session id did not match session object.");
    if (mNodeId != newConfig->nodeId())
        throw SessionError("Config node id did not match session object.");
 
    // check state
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
	if (mShuttingDown)
	    throw SessionError("Session is shutting down");
        if (mState == SessionState::Busy)
            throw SessionError("Session is busy and cannot be modified",
                               HTTP_RESOURCE_CONFLICT);
        if (mState == SessionState::Defunct)
            throw SessionError("Session is defunct and cannot be modified",
                               HTTP_RESOURCE_CONFLICT);
        mState = SessionState::Busy;
    }

    // start async modification thread
    if (mOperationThread.joinable())
      mOperationThread.join();
    mOperationThread = std::thread(&Session::updateProc,this,newConfig);

    timeval now;
    gettimeofday(&now, nullptr);
    mLastActivitySecs = now.tv_sec;
}
 
void Session::asyncDelete(const std::string& reason)
{
    {
        std::lock_guard<std::mutex> lock(mStateMutex);
	if (mShuttingDown)
	    throw SessionError("Session is shutting down");
        if (mState == SessionState::Busy)
            throw SessionError("Session is busy and cannot be deleted",
                               HTTP_RESOURCE_CONFLICT);
        if (mState == SessionState::Defunct)
            throw SessionError("Session is defunct and cannot be deleted",
                               HTTP_RESOURCE_CONFLICT);
        mState = SessionState::Busy;
    }

    // start async deletion thread
    if (mOperationThread.joinable())
      mOperationThread.join();	

    std::chrono::steady_clock::time_point endtime = 
	std::chrono::steady_clock::now() +
	WAIT_FOR_SHUTDOWN_TIMEOUT;

    mOperationThread = std::thread(&Session::deleteProc,this,reason,endtime);

    timeval now;
    gettimeofday(&now, nullptr);
    mLastActivitySecs = now.tv_sec;
}

// shutdown is a synchronous operation, used when the node itself is shutting down.
// it waits for any current operation to complete, then stops all computations.
// node cannot fully shutdown until all sessions are shutdown (although there is
// a timeout)
// sessions are shut down sequentially, so that we don't have too many
// terminations running at once.
void Session::syncShutdown(const std::string& reason)
{
    ARRAS_DEBUG(log::Session(mId.toString()) << "Shutting down session");
    std::chrono::steady_clock::time_point endtime = 
	std::chrono::steady_clock::now() +
	WAIT_FOR_SHUTDOWN_TIMEOUT;
    {
	std::unique_lock<std::mutex> lock(mStateMutex);
	
        // prevent new operations from starting
	mShuttingDown = true;
	
        // wait for any running operations to complete
	while (mState == SessionState::Busy) {
	    std::cv_status cvs = mOperationComplete.wait_until(lock,endtime);
	    if (cvs == std::cv_status::timeout)
		throw SessionError("Session shutdown took too long");
	}

	// we can now release the lock, because mShuttingDown blocks
	// any new operations from starting
    }
    
    // cleanup current operation thread, just to be tidy...
    if (mOperationThread.joinable())
	mOperationThread.join();

    // run deleteProc to shutdown computations
    deleteProc(reason,endtime);
    ARRAS_DEBUG(log::Session(mId.toString()) << "Have shut down session");
}
    
void Session::updateProc(SessionConfig::Ptr newConfig)
{    
    try {
	applyNewConfig(*newConfig);
    } catch (std::exception& ex) {
	mArrasController.sessionOperationFailed(mId,"create/modify",ex.what());
    } catch (...) {
	mArrasController.sessionOperationFailed(mId,"create/modify","Unknown exception");
    }
    {
	std::lock_guard<std::mutex> lock(mStateMutex);
	if (mState == SessionState::Busy) {
	    mState = SessionState::Free;
	}
    }
    // continue shutdown if it was waiting on us
    mOperationComplete.notify_all();
}

void Session::deleteProc(std::string reason,
			 std::chrono::steady_clock::time_point endtime)
{
    try {
	std::lock_guard<std::mutex> lock(mComputationsMutex);
	for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
	    jt->second->shutdown();
	}

	for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
	    
	    Computation::Ptr comp = jt->second;
	    bool didShutdown = comp->waitUntilShutdown(endtime);
	    if (!didShutdown) {
		ARRAS_ERROR(log::Id("cantStopComp") <<
			    log::Session(mId.toString()) <<
			    "Cannot stop computation " << comp->name() << " [" << comp->id().toString());
	    }
	}

	arrasController().shutdownSession(mId, reason);

    } catch (std::exception& ex) {
	mArrasController.sessionOperationFailed(mId, "delete", ex.what());
    } catch (...) {
	mArrasController.sessionOperationFailed(mId, "delete", "Unknown exception");
    }
    {
	std::lock_guard<std::mutex> locks(mStateMutex);
	mState = SessionState::Defunct;
	mDeleteReason = reason;
    }
    // continue shutdown if it was waiting on us
    mOperationComplete.notify_all();
}
    
// this function runs in a per-session background thread 
// via modifyProc, and is not designed to run concurrently
// on the same session
void Session::applyNewConfig(const SessionConfig& newConfig)
{
    if (newConfig.logLevel() >= 0)
        mLogLevel = newConfig.logLevel();
    else
        mLogLevel = mComputationDefaults.logLevel; 

    // work out which computations we need to shutdown,
    // and which computation we need to start
    std::vector<Computation::Ptr> defunctComps;
    std::map<api::UUID,std::string> newComps;
    getConfigDelta(newConfig,defunctComps,newComps);
    
    // shutdown defunct computations
    for (auto comp : defunctComps) {
        comp->shutdown();
    }

    // wait for them to shutdown, so that their resources
    // are released and available for the new computations
    std::chrono::steady_clock::time_point endtime = 
        std::chrono::steady_clock::now() +
        WAIT_FOR_SHUTDOWN_TIMEOUT;

    for (auto comp : defunctComps) {

        bool didShutdown = comp->waitUntilShutdown(endtime);
        if (!didShutdown) {
            ARRAS_ERROR(log::Id("cantStopComp") <<
                        log::Session(mId.toString()) <<
                        "Cannot stop computation " << comp->name() << " [" << comp->id().toString());
            throw SessionError("Computations did not shutdown within timeout.");
        }
    }

    // start new computations     
    for (auto it = newComps.begin(); it != newComps.end(); ++it) {

        const api::UUID compId = it->first;
        const std::string compName = it->second;

	startNewComputation(compId,compName,newConfig);
    }
}

// fetch the lists of existing computations not in the
// new config, and new computations in the config that don't
// exist yet
void Session::getConfigDelta(const SessionConfig& newConfig,
                             std::vector<Computation::Ptr>& defunctComps,
                             std::map<api::UUID,std::string>& newComps) const
{
    std::lock_guard<std::mutex> lock(mComputationsMutex);
    const std::map<api::UUID,std::string>& configComps = newConfig.getComputations();
    for (auto it = configComps.begin(); it != configComps.end(); ++it) {
        if (mComputations.count(it->first) == 0)
            newComps[it->first] = it->second;
    }
    for (auto jt = mComputations.begin(); jt != mComputations.end(); ++jt) {
        if (configComps.count(jt->first) == 0)
            defunctComps.push_back(jt->second);
    }    
}

// see note for applyNewConfig
void Session::startNewComputation(const api::UUID& compId,
                                  const std::string& compName,
				  const SessionConfig& sessConfig)
{
    ARRAS_ATHENA_TRACE(0,log::Session(mId.toString()) <<
                       "{trace:comp} launch " << compId.toString() << 
                       " " << compName);

    ComputationConfig compConfig(compId,
				 mNodeId,
				 mId,
				 compName,
				 mComputationDefaults);

    api::ObjectConstRef definition = sessConfig.getDefinition(compName);
    if (definition.isNull()) {
	ARRAS_ERROR(log::Id("missingCompDefinition") <<
		    log::Session(mId.toString()) <<
		    "Cannot start computation " << compName << " [" << compId.toString() <<
		    "] because its definition is not present in the config");
	throw SessionError("Missing definition for "+compName);
    }
   
    const std::string& contextName = compConfig.fetchContextName(definition);
    api::Object nullObj;
    api::ObjectConstRef context = contextName.empty() ? nullObj : sessConfig.getContext(contextName); 
    if (context.isNull() && !contextName.empty()) {
	ARRAS_ERROR(log::Id("missingContext") <<
		    log::Session(mId.toString()) <<
		    "Cannot start computation " << compName << " [" << compId.toString() <<
		    "] because the context '" << contextName << "' does not exist");
	throw SessionError("Missing named context for "+compName);
    }
    compConfig.setDefinition(definition,context,mLogLevel);

    api::ObjectConstRef routing = sessConfig.getRouting();
    compConfig.addRouting(routing);

    try {
	compConfig.applyPackaging(mProcessManager,
				  definition,
	                          context);
    } catch (SessionError &e) {
        throw SessionError("Cannot start computation "+compName+ " : " + e.what());
    }

    bool ok = compConfig.writeExecConfigFile();
    if (!ok) {
        // config has already logged the reason
        throw SessionError("Cannot start computation "+compName+ " : failed to save config file");
    }

    Computation::Ptr compPtr = std::make_shared<Computation>(compId,compName,*this);
    impl::SpawnArgs& spawnArgs = compConfig.spawnArgs();
    spawnArgs.observer = compPtr;
    ok = compPtr->start(spawnArgs);
    if (!ok) {
        // computation has already logged the reason
        throw SessionError("Cannot start computation "+compName);
    }

    std::lock_guard<std::mutex> lock(mComputationsMutex); 
    mComputations[compId] = compPtr;    
}

void Session::setExpirationTime(const  std::chrono::steady_clock::time_point& expiry,
				const std::string& message)
{
    // terminate any existing expiration
    stopExpiration();
    // start a proc that will terminate session at the given
    // time, as long a mExpirationSet remains true
    {
	std::unique_lock<std::mutex> lock(mExpirationMutex);
	mExpirationSet = true;
    }
    mExpirationThread = std::thread(&Session::expirationProc,this,expiry,message);
}

void Session::stopExpiration()
{
    // cause expirationProc to exit without terminating the session
    {
	std::unique_lock<std::mutex> lock(mExpirationMutex);
	mExpirationSet = false;
    }
    // make expirationProc exit promptly and join it
    if (mExpirationThread.joinable()) {
	mExpirationCondition.notify_all();
	mExpirationThread.join();
    }
}

void Session::expirationProc(std::chrono::steady_clock::time_point expiry,
			     std::string message)
{
    std::unique_lock<std::mutex> lock(mExpirationMutex);
    while (mExpirationSet) {
	std::cv_status cvs = mExpirationCondition.wait_until(lock,expiry);
        if (cvs == std::cv_status::timeout)
            break;
    }
    if (mExpirationSet) {
	mArrasController.sessionExpired(mId, message);
    }
}

}
}
