// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ArrasSessions.h"

#include "ComputationDefaults.h"
#include "SessionError.h"

#include <http/http_types.h>
#include <execute/ProcessManager.h>

#include <unistd.h>
#include <sys/time.h>

using namespace arras4::network;

namespace arras4 {
    namespace node {

ArrasSessions::ArrasSessions(impl::ProcessManager& processManager,
                             const ComputationDefaults& defaults,
                             const api::UUID& nodeId) :
    mProcessManager(processManager), mDefaults(defaults),
    mNodeId(nodeId)
{
    mController = std::make_shared<ArrasController>(nodeId,*this);
    mProcessManager.setProcessController(mController);

    bool ok = mController->startRouter(defaults,
				       mProcessManager);
    if (!ok) {
	throw SessionError("Cannot start node router");
    }
    ok = false;
    for (int i = 0; i < 10 && !ok; i++)
    {
	if (i > 0) {
	    sleep(1);
	    ARRAS_DEBUG("Retrying router connect (" << i << ")");
	}
	ok = mController->connectToRouter(defaults);
    }
    if (!ok) {
        throw SessionError("Cannot connect to node router");
    }

    timeval now;
    gettimeofday(&now,nullptr);
    mStartTimeSecs = now.tv_sec;
}

void ArrasSessions::run()
{
    mController->run();
}

void ArrasSessions::stopRunning()
{
    mController->stopRunning();
}

Session::Ptr ArrasSessions::getSession(const api::UUID& id)
{
    std::lock_guard<std::mutex> lock(mSessionsMutex);
    return getSession_wlock(id);
}

Session::Ptr ArrasSessions::getSession_wlock(const api::UUID& id)
{
    std::map<api::UUID,Session::Ptr>::iterator it = mSessions.find(id);
    if (it == mSessions.end()) {
        return Session::Ptr();
    }
    return it->second;
}

Computation::Ptr ArrasSessions::getComputation(const api::UUID& sessionId, 
					       const api::UUID& id)
{
    Session::Ptr sp = getSession(sessionId);
    if (sp) 
	return sp->getComputation(id);

    return Computation::Ptr();
}

// get a list of active session ids. 
std::vector<api::UUID> ArrasSessions::activeSessionIds() const
{
    std::vector<api::UUID> ret;
    std::lock_guard<std::mutex> lock(mSessionsMutex);
    for (auto it = mSessions.begin(); it != mSessions.end(); ++it) {
        if (it->second->isActive())
            ret.push_back(it->second->id());
    }
    return ret;
}

// get status object for a session
// throws SessionError if session doesn't exist
api::Object ArrasSessions::getStatus(const api::UUID& sessionId)
{
    Session::Ptr session = getSession(sessionId);
    if (session)
        return session->getStatus();
    throw SessionError("Session does not exist",HTTP_NOT_FOUND);
}

// get performance stats  for a session
// throws SessionError if session doesn't exist
api::Object ArrasSessions::getPerformance(const api::UUID& sessionId)
{
    Session::Ptr session = getSession(sessionId);
    if (session)
        return session->getPerformanceStats();
    throw SessionError("Session does not exist",HTTP_NOT_FOUND);
}

// send a signal to a session, described by a object.
// signal types are "run" and "engineReady", indicated by
// the "status" field in the object. Additional data may be
// present
// throws SessionError if session doesn't exist
void ArrasSessions::signalSession(const api::UUID& sessionId,
                                   api::ObjectConstRef signalData)
{
    Session::Ptr session = getSession(sessionId);
    if (session)
        session->signal(signalData);
    else
        throw SessionError("Session does not exist",HTTP_NOT_FOUND);
}

// create a new session, given its definition
// throws SessionError if session already exists or if
// creation fails.
api::Object ArrasSessions::createSession(api::ObjectConstRef definition)
{
    if (mClosed)
	throw SessionError("Node is closed : cannot accept new sessions",
			   HTTP_RESOURCE_CONFLICT);

    SessionConfig::Ptr config = std::make_shared<SessionConfig>(definition,mNodeId);
    const api::UUID& id = config->sessionId();
    ARRAS_ATHENA_TRACE(0,log::Session(id.toString()) <<
		       "{trace:session} create " << id.toString());

    Session::Ptr session;
    {
        std::lock_guard<std::mutex> lock(mSessionsMutex);
        session = getSession_wlock(id);
        if (session) 
            throw SessionError("Session already exists",HTTP_RESOURCE_CONFLICT);
        session = std::make_shared<Session>(id, mNodeId, mDefaults,
                                            mProcessManager,*mController);
        mSessions[id] = session;
    }
    bool ok = mController->initializeSession(*config);
    if (!ok) {
        std::lock_guard<std::mutex> lock(mSessionsMutex);
        mSessions.erase(id);
        throw SessionError("Failed to initialize session with node router",
                           HTTP_INTERNAL_SERVER_ERROR);
    }
    if (config->isThisEntryNode()) {
	ARRAS_DEBUG(log::Session(id.toString()) <<
		   "This node is session entry node");
	// on the entry node, session is set to expire after a certain length of time.
	// expiry is interrupted by the client connecting to the session, and
	// so it's only sessions that the client doesn't connect to in reasonable time
	// that expire and get terminated
	auto expiry = std::chrono::steady_clock::now() +  std::chrono::seconds(mDefaults.clientConnectionTimeoutSecs);
	session->setExpirationTime(expiry,"Client failed to connect");
    }
    try {
	ARRAS_DEBUG(log::Session(id.toString()) <<
		   "About to spawn computations");
        session->asyncUpdateConfig(config);
    } catch (...) {
        std::lock_guard<std::mutex> lock(mSessionsMutex);
        mSessions.erase(id);
        throw;
    }
    return config->getResponse();
}

api::Object ArrasSessions::modifySession(api::ObjectConstRef definition)
{
    if (mClosed)
	throw SessionError("Node is closed : cannot modify sessions",
			   HTTP_RESOURCE_CONFLICT);

    SessionConfig::Ptr config = std::make_shared<SessionConfig>(definition,mNodeId);
    const api::UUID& id = config->sessionId();  
    ARRAS_ATHENA_TRACE(0,log::Session(id.toString()) <<
		       "{trace:session} modify " << id.toString());
    Session::Ptr session = getSession(id);
    if (!session) 
        throw SessionError("Session doesn't exist",
                           HTTP_NOT_FOUND);
    session->asyncUpdateConfig(config);
    return config->getResponse();
}

// delete a session : 'reason' is arbitrary text used for logging
// throws SessionError if session doesn't exist 
void ArrasSessions::deleteSession(const api::UUID& id,
                                  const std::string& reason)
{
    std::lock_guard<std::mutex> lock(mSessionsMutex);
    Session::Ptr session = getSession_wlock(id);
    if (!session)
        throw SessionError("Session doesn't exist",
                           HTTP_NOT_FOUND); 
    ARRAS_ATHENA_TRACE(0,log::Session(id.toString()) <<
		       "{trace:session} delete " << id.toString());

    session->asyncDelete(reason);
    // session cannot be removed from mSessions until async
    // operation completes : currently we don't bother and
    // keep the information indefinitely, marked by
    // state "Defunct"
}

long ArrasSessions::getLastActivitySecs(bool includeComputations) const
{
    long ret = 0;
    std::lock_guard<std::mutex> lock(mSessionsMutex);
    for (auto it = mSessions.begin(); it != mSessions.end(); ++it) {
        long sla = it->second->getLastActivitySecs(includeComputations);
	if (sla > ret) ret = sla;
    }
    return ret; 
}

void ArrasSessions::getIdleStatus(api::ObjectRef out) const
{
    timeval now;
    gettimeofday(&now, nullptr);
    long mostRecent = mStartTimeSecs;
    int index = 0;
    std::lock_guard<std::mutex> lock(mSessionsMutex);
    for (auto it = mSessions.begin(); it != mSessions.end(); ++it) {
        long sla = it->second->getLastActivitySecs(true);
	if (sla > mostRecent) mostRecent = sla;
	out["sessions"][index]["id"] = it->second->id().toString();
	out["sessions"][index]["idletime"] = static_cast<int>(now.tv_sec - sla);
    }
    out["idletime"] =  static_cast<int>(now.tv_sec - mostRecent);
}

void ArrasSessions::shutdownAll(const std::string& reason)
{
    ARRAS_DEBUG("Shutting down all sessions");
    mClosed = true;
    std::lock_guard<std::mutex> lock(mSessionsMutex);
    for (const auto& item : mSessions) {
	try {
	    item.second->syncShutdown(reason);
	} catch (SessionError& se) {
	    ARRAS_WARN(log::Id("SessionShutdownFailed") <<
		       log::Session(item.first.toString()) <<
		       "Failed to shutdown session : " << se.what());
	}
    }
    ARRAS_DEBUG("Have shut down all sessions");
}

}
}
