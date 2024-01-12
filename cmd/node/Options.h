// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_NODESERVICE_OPTIONS_H__
#define __ARRAS4_NODESERVICE_OPTIONS_H__

#include <string>
#include "PreemptionMonitor.h"

namespace {
     unsigned constexpr DEFAULT_SERVER_THREADS = 4;
}

namespace arras4 {
    namespace node {

class NodeOptions
{
public:
    
    // These are general options
    bool setMaxFDs = true;
    std::string maxNodeMemory; 
    unsigned numHttpServerThreads = 4;
    PreemptionMonitorType preemptionMonitorType = PreemptionMonitorType::None;
    bool profiling = false; 
    std::string userName;
    std::string nodeId;
    bool disableBanlist = false;

    // These are options that control the service connections
    std::string coordinatorHost; 
    unsigned coordinatorPort = 8087;
    std::string coordinatorEndpoint{"/coordinator/1"};
    bool noConsul = false;
    std::string consulHost; 
    unsigned consulPort = 8500;      
    std::string environment{"prod"};
    std::string dataCenter{"gld"};
    std::string ipcDir{"/tmp"};
    std::string configServiceUrl;


    // These are descriptions of the resources available on this node, 
    // that are sent to Coordinator 
    std::string exclusiveUser;
    std::string exclusiveProduction;
    std::string exclusiveTeam;
    bool overSubscribe = false; 
    unsigned cores = 0;
    std::string memory; 
    float hostRU = 0.0;
    std::string farmFullId;
};	 

}
}
#endif
