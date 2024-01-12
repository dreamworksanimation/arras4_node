// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_COMPUTATION_H__
#define __ARRAS4_COMPUTATION_H__

#include <message_api/UUID.h>
#include <message_api/Object.h>
#include <core_messages/ExecutorHeartbeat.h>

#include <execute/Process.h>

#include <memory>
#include <atomic>
#include <mutex>

namespace arras4 {
    namespace node {

class Session;

class Computation : public impl::ProcessObserver
{

public:
    // throws SessionError
    Computation(const api::UUID& id,
                const std::string& name,
                Session& session);
    ~Computation();

    using Ptr = std::shared_ptr<Computation>;

    const api::UUID& id() const;
    const api::UUID& sessionId() const;
    const std::string& name() const;
    
    bool start(const impl::SpawnArgs& spawnArgs);
    void shutdown(); 
    void signal(api::ObjectConstRef signalData); 
    bool waitUntilShutdown(const std::chrono::steady_clock::time_point& endTime);

    api::Object getStatus();

    // ProcessObserver
    void onTerminate(const api::UUID& id, const api::UUID& sessionId, impl::ExitStatus status) override;
    void onSpawn(const api::UUID& id, const api::UUID& sessionId, pid_t pid) override;

    // stats
    void onHeartbeat(impl::ExecutorHeartbeat::ConstPtr heartbeat);
    void getPerformanceStats(api::ObjectRef obj);
    long getLastActivitySecs() const { return mLastActivitySecs; }

private:

    impl::Process::Ptr mProcess;
    std::atomic<bool> mSentGo{false};
    std::atomic<bool> mTerminationExpected{false};
    Session& mSession;

    std::mutex mStatsMutex;
    impl::ExecutorHeartbeat::ConstPtr mLastHeartbeat;
    float mCpuUsage5SecsMax{0};
    float mCpuUsage60SecsMax{0};
    unsigned long long mMemoryUsageBytesMax{0};
    long mLastSentMessagesSecs{0};
    int mLastSentMessagesMicroSecs{0};
    long mLastReceivedMessagesSecs{0};
    int mLastReceivedMessagesMicroSecs{0};
    long mLastActivitySecs{0};
};

}
}
#endif
