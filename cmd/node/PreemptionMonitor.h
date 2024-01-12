// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __PREEMPTION_MONITOR_H__
#define __PREEMPTION_MONITOR_H__

#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace arras4 {
    namespace node {

class ArrasNode;

enum class PreemptionMonitorType
{
    None, AWS, Azure
};

class PreemptionMonitor
{
public:
    virtual ~PreemptionMonitor();

    // creates a pre-emption monitor, starts the monitor thread, and returns the monitor object
    // delete the object to stop monitoring (before node is deleted)
    static PreemptionMonitor* start(PreemptionMonitorType type,
	                            ArrasNode* node);
    
protected:

    PreemptionMonitor(ArrasNode* node) : mNode(node) {}
    void run();
    virtual void threadProc() = 0;

    ArrasNode* mNode;
    std::thread mThread;
    std::atomic<bool> mRun;
    std::mutex mStopMutex;
    std::condition_variable mStopCondition;

};

}
}
#endif // _PREEMPTION_MONITOR_CLIENT_H__
