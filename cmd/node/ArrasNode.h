// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_ARRAS_NODE_H__
#define __ARRAS4_ARRAS_NODE_H__

#include "Options.h"
#include "HardwareFeatures.h"

#include <session/ComputationDefaults.h>
#include <execute/ProcessManager.h>

#include <message_api/Object.h>
#include <message_api/UUID.h>
#include <shared_impl/Platform.h>

#include <string>
#include <thread>
#include <mutex>
#include <memory>

namespace arras4 {
    namespace node {

class ArrasSessions;
class NodeService;

class ArrasNode 
{
public:
    ArrasNode();

    // throws NodeError and ServiceError
    void initialize();

    ~ArrasNode();

    // these throw OperationError when they fail
    void checkHealth();
    void setStatus(api::ObjectConstRef status); // shutdown/close/unregistered
    void updateTags(api::ObjectConstRef tags);
    void deleteTags(api::ObjectConstRef tags);

    // doesn't return until stopRunning() is called
    void run();
    void stopRunning();

    ComputationDefaults& computationDefaults() { return mComputationDefaults; }
    NodeOptions& nodeOptions() { return mOptions; }

private:

    void initLogging();
    void fetchInetInterfaces();
    void findServices();
    void calcResources();
    void setMaxFDs();
    void registerNode();
    void deregisterNode();
    void buildNodeInfo();
    void fetchRezVersionInfo(api::ObjectRef info);

    void updateTagsProc(api::Object tags);
    void deleteTagsProc(api::Object tags);

    std::mutex mRunMutex;
    bool mRun;
    std::condition_variable mRunCondition;

    std::mutex mNodeInfoMutex;
    bool mNodeInfoIsUpdating{false};
    std::thread mNodeInfoUpdateThread;
    
    // Option set on the command line
    ComputationDefaults mComputationDefaults;
    NodeOptions mOptions;

    void checkIpcSocket();
    void checkDisk();

    api::UUID mNodeId;
    std::string mHostIpAddr;
    std::string mHostName;

    std::string mConsulUrl;
    std::string mCoordinatorUrl;

    // node registration with Consul service
    std::string mConsulServiceId;
    std::string mConsulHealthCheckName;
    bool mIsRegistered = false;

    // sent to Coordinator to register the node
    api::Object mInetInterfaces;
    api::Object mNodeInfo;

    // resources
    unsigned long mNodeMemory = 0;
    unsigned mNodeCores = 0;
    unsigned long mComputationsMemory = 0;
    unsigned mComputationsCores= 0;
    std::vector<HardwareFeatures::ProcessorInfo> mProcessors;
    arras4::impl::PlatformInfo mPlatformInfo;

    std::unique_ptr<impl::ProcessManager> mProcessManager;
    std::unique_ptr<ArrasSessions> mSessions;
    std::unique_ptr<NodeService> mNodeService;
};

}
}
#endif
    
    
