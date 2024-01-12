// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ClientRemoteEndpoint.h"
#include "RoutingTable.h"
#include "ThreadedNodeRouter.h"

#include <routing/Addresser.h>
#include <exceptions/InternalError.h>


#include <cstring>
#include <sys/time.h>

namespace arras4 {
namespace node {

ClientRemoteEndpoint::ClientRemoteEndpoint(network::Peer* aPeer,
                                           const api::UUID& aSessionId,
                                           ThreadedNodeRouter& aThreadedNodeRouter,
                                           const std::string& traceInfo)
    : RemoteEndpoint(aPeer, PeerManager::PEER_CLIENT, aSessionId, aSessionId, aThreadedNodeRouter,traceInfo)
{
}

ClientRemoteEndpoint::~ClientRemoteEndpoint()
{
}

void
ClientRemoteEndpoint::addressReceivedEnvelope()
{   
    const impl::Addresser* clientAddresser = mRoutingData->clientAddresser();
    if (clientAddresser == nullptr) {
        throw impl::InternalError("Null clientAddresser pointer in ClientRemoteEndpoint");
    }

    if (mLastEnvelope.metadata()->routingName() == "PingMessage") {
        // ping messages from the client go to all computations
        // regardless of filters
        clientAddresser->addressToAll(mLastEnvelope);
    } else {
        // address to the correct computations based on filters 
        clientAddresser->address(mLastEnvelope);
    }
}

} // namespace service
} // namespace arras


