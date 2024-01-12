// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_CLIENTREMOTEENDPOINT_H__
#define __ARRAS_CLIENTREMOTEENDPOINT_H__

#include "RemoteEndpoint.h"

namespace arras4 {
    namespace node {

class ClientRemoteEndpoint : public RemoteEndpoint
{
public:
    ClientRemoteEndpoint(
        network::Peer* aPeer,
        const api::UUID& aSessionId,
        ThreadedNodeRouter& aThreadedNodeRouter,
        const std::string& traceInfo);
         
    ~ClientRemoteEndpoint();

protected:
    // take the opportunity to synthesize a "to" address list for messages originating from client
    void addressReceivedEnvelope();
};

} 
} 

#endif // __ARRAS_CLIENTREMOTEENDPOINT_H__

