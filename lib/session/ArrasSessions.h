// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_ARRAS_SESSIONS_H__
#define __ARRAS4_ARRAS_SESSIONS_H__

#include "Session.h"
#include "ArrasController.h"

#include <message_api/UUID.h>
#include <message_api/Object.h>
#include <mutex>
#include <map>
#include <memory>
#include <atomic>

namespace arras4 {
    namespace node {

class ComputationDefaults;
class ProcessManager;

class ArrasSessions
{
public:
    ArrasSessions(impl::ProcessManager& processManager,
                  const ComputationDefaults& defaults,
                  const api::UUID& nodeId);

    Session::Ptr getSession(const api::UUID& id);
    Computation::Ptr getComputation(const api::UUID& sessionId,const api::UUID& id);

    std::vector<api::UUID> activeSessionIds() const;
    api::Object getStatus(const api::UUID& sessionId);
    api::Object getPerformance(const api::UUID& sessionId);

    void signalSession(const api::UUID& sessionId,
                       api::ObjectConstRef signalData);
    api::Object createSession(api::ObjectConstRef definition);
    api::Object modifySession(api::ObjectConstRef definition);
    void deleteSession(const api::UUID& id,
                       const std::string& reason);

    void setClosed(bool closed) { mClosed = closed; }
    
    std::shared_ptr<ArrasController> getController() { return mController; }

    // doesn't return until stopRunning() is called
    void run();         
    void stopRunning();

    // synchronously shut down all sessions
    void shutdownAll(const std::string& reason);

    // get last activity time (epoch secs) on any session
    long getLastActivitySecs(bool includeComputations) const;
    // collect idle times (now - last activity) in seconds
    // per session and and an overall value
    void getIdleStatus(api::ObjectRef out) const;

private: 

    Session::Ptr getSession_wlock(const api::UUID& id);

    impl::ProcessManager& mProcessManager;
    const ComputationDefaults& mDefaults;
    api::UUID mNodeId;
    std::shared_ptr<ArrasController> mController;
    std::atomic<bool> mClosed{false};
    long mStartTimeSecs = 0;

    mutable std::mutex mSessionsMutex;
    std::map<api::UUID,Session::Ptr> mSessions;
};  

}
}
#endif



    
    
