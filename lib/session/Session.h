// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_SESSION_H__
#define __ARRAS4_SESSION_H__

#include "Computation.h"
#include "SessionConfig.h"

#include <message_api/UUID.h>
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <condition_variable>

namespace arras4 {
    namespace impl {
	class ProcessManager;
    }

    namespace node {

class ArrasController;
class ComputationDefaults;

// possible states of a session
enum class SessionState
{
    Free,       // session is not being modified
    Busy,       // session is currently being modified : you will not be able to initiate a new modification
    Defunct     // session has been deleted  
};

inline std::string SessionState_string(SessionState s)
{
    if (s==SessionState::Free) return "Free";
    if (s==SessionState::Busy) return "Busy";
    return "Defunct";
}

class Session
{
public:
    Session(const api::UUID& sessionId,
            const api::UUID& nodeId,
            const ComputationDefaults& computationDefaults,
            impl::ProcessManager& processManager,
            ArrasController& arrasController);

    ~Session();

    using Ptr = std::shared_ptr<Session>;
 
    Computation::Ptr getComputation(const api::UUID& id); // locks computations mutex
    SessionState getState();           // locks state mutex
    std::string getDeleteReason();     // locks state mutex
    bool isActive();                   // locks state mutex
    api::Object getStatus();           // locks comp and state mutexes
    api::Object getPerformanceStats(); // locks comp mutex
    void signal(api::ObjectConstRef signalData); 
    void asyncUpdateConfig(SessionConfig::Ptr newConfig); // locks comp and state mutexes
    void asyncDelete(const std::string& reason);
    void syncShutdown(const std::string& reason);

    // sessions can be set to expire (causing a "sessionExpiry" event) at a certain time,
    // unless they are deleted or "stopExpiration()" is called
    void setExpirationTime(const  std::chrono::steady_clock::time_point& expiry,
			   const std::string& message);
    void stopExpiration();

    // return time of last activity, in epoch seconds.
    // if "includeComputations" is true, messages to/from computations
    // count as activity. Otherwise, "activity" is operations or signals
    // acting on the session as a whole.
    long getLastActivitySecs(bool includeComputations) const;

    impl::ProcessManager& processManager() { return mProcessManager; }
    ArrasController& arrasController() { return mArrasController; }

    const api::UUID& id() const { return mId; }   

    // return true if this session has "autoSuspend" enabled
    // (i.e. computations are suspended using SIGSTOP when they
    // receive the "go" signal, for debugging purposes).
    // Currently "autoSuspend" is settable on the node itself
    bool isAutoSuspend() const;

private:

    Computation::Ptr getComputation_wlock(const api::UUID& id);
    void checkIsFree();
    void signalAll(api::ObjectConstRef signalData);

    void updateProc(SessionConfig::Ptr newConfig);
    void deleteProc(std::string reason,std::chrono::steady_clock::time_point endtime);
    void expirationProc(std::chrono::steady_clock::time_point expiry,
			std::string message);
    void applyNewConfig(const SessionConfig& newConfig);
    void getConfigDelta(const SessionConfig& newConfig,
                        std::vector<Computation::Ptr>& defunctComps,
                        std::map<api::UUID,std::string>& newComps) const;
    void startNewComputation(const api::UUID& compId,
			     const std::string& compName,
			     const SessionConfig& sessConfig);

    api::UUID mId;
    api::UUID mNodeId;
    const ComputationDefaults& mComputationDefaults;
    int mLogLevel;
    impl::ProcessManager& mProcessManager;
    ArrasController& mArrasController;

    // last activity on the session as a whole, in epoch secs
    long mLastActivitySecs = 0;

    std::thread mOperationThread; // protected by Busy state

    mutable std::mutex mStateMutex;
    SessionState mState;
    std::condition_variable mOperationComplete; // signalled whenever an operation ends
    std::string mDeleteReason;
    bool mShuttingDown{false};  // protected by state mutex

    mutable std::mutex mComputationsMutex;
    std::map<api::UUID,Computation::Ptr> mComputations;

    mutable std::mutex mExpirationMutex;
    bool mExpirationSet{false};
    std::condition_variable mExpirationCondition;
    std::thread mExpirationThread;
};

}
}
#endif
