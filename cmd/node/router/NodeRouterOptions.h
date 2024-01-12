// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_NODEROUTEROPTIONS_H__
#define __ARRAS_NODEROUTEROPTIONS_H__

#include <message_api/UUID.h>
#include <string>

namespace arras4 {
namespace node {


struct NodeRouterOptions {
    unsigned short mNetPort;
    std::string mIpcName;
    api::UUID mNodeId;
    std::string mCoordinatorHost;
    unsigned short mCoordinatorPort = 0;
    std::string mCoordinatorEndpoint;
    bool mProfiling = false;
};

} // end namespace node
} // end namespace arras4

#endif

