// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include <core_messages/ControlMessage.h>
#include <node/messages/ClientConnectionStatusMessage.h>
#include <node/messages/ComputationStatusMessage.h>
#include "PeerManager.h"
#include "RemoteEndpoint.h"
#include "ThreadedNodeRouter.h"
#include "SessionRoutingData.h"

using namespace arras4::api;

namespace arras4 {
namespace node {

ThreadedNodeRouter::ThreadedNodeRouter(const UUID& aNodeId) :
    mNodeId(aNodeId),
    mServiceEndpoint(nullptr),
    mServiceToRouterQueue(new impl::MessageQueue()),
    mServiceDisconnected(false)
{
}

void ThreadedNodeRouter::serviceDisconnected()
{
    std::unique_lock<std::mutex> lock(mServiceDisconnectedMutex);
    mServiceDisconnected = true;
    mServiceDisconnectedCondition.notify_all();
}

void ThreadedNodeRouter::waitForServiceDisconnected()
{
    std::unique_lock<std::mutex> lock(mServiceDisconnectedMutex);
    while (!mServiceDisconnected) {
        mServiceDisconnectedCondition.wait(lock);
    }
}

// Thread safety for this is provided by RoutingTable
SessionRoutingData::Ptr
ThreadedNodeRouter::sessionRoutingData(const UUID& aSessionId)
const
{
    return mRoutingTable.sessionRoutingData(aSessionId);
}

// Thread safety for this is provided by RoutingTable
SessionRoutingData::Ptr
ThreadedNodeRouter::addSessionRoutingData(const UUID& aSessionId,
                                          ObjectConstRef aRoutingData)
{
   SessionRoutingData::Ptr data(new SessionRoutingData(aSessionId,
                                                       mNodeId,
                                                       aRoutingData));
   mRoutingTable.addSessionRoutingData(aSessionId, data);
   return data;
}

void
ThreadedNodeRouter::destroyEndpoints()
{
    while (1) {
        RemoteEndpoint* ep = nullptr;

        { // this block exists to control the scope of the lock
            std::lock_guard<std::mutex> lock(mEndpointsToDeleteMutex);

            if (mEndpointsToDelete.empty()) break;
            ep = mEndpointsToDelete.front();
            mEndpointsToDelete.pop_front();

        }

        // the lock can't be held during the actual destruction because the
        // RemoteEndpoint might be waiting in queueEndpointsForDestruction()
        UUID id;
        // untracking will release the shared pointer held by PeerManager, causing
        // deletion once any in-progress message queueing is complete
        PeerManager::PeerType type = untrackPeer(ep, id);

        ARRAS_LOG_TRACE("Disconnect notification for remote node '%s'", id.toString().c_str());

        if (type == PeerManager::PEER_CLIENT) {
        } else if (type == PeerManager::PEER_NODE) {
            ARRAS_LOG_ERROR("Remote node '%s' disconnected", id.toString().c_str());
        } else {
            ARRAS_LOG_TRACE("Disconnect notification for host '%s', type %d", id.toString().c_str(), type);
        }
    }

}

void
ThreadedNodeRouter::notifyClientDisconnected(const UUID& aSessionId, const std::string& aReason)
{
    ClientConnectionStatus* disco = new ClientConnectionStatus;
    disco->mSessionId = aSessionId;
    disco->mReason = aReason;
    notifyService(disco);
}

//
// Sending the connected notification using the ClientDisconneded message
// The message should be renamed to something more generic
//
void
ThreadedNodeRouter::notifyClientConnected(const UUID& aSessionId)
{
    ClientConnectionStatus* disco = new ClientConnectionStatus;
    disco->mSessionId = aSessionId;
    disco->mReason = "connected";
    notifyService(disco);
}

void
ThreadedNodeRouter::notifyComputationStatus(const api::UUID& aSessionId, const api::UUID& aCompId,
                                                 const std::string& aStatus)
{
    ComputationStatusMessage* compStatus = new ComputationStatusMessage;
    compStatus->mSessionId = aSessionId;
    compStatus->mComputationId = aCompId;
    compStatus->mStatus= aStatus;
    notifyService(compStatus);
}


void
ThreadedNodeRouter::notifyHeartbeat(std::shared_ptr<const impl::ExecutorHeartbeat>& heartbeat, const std::string& sessionId, const std::string& compId)
{
    impl::Envelope envelope(heartbeat);
    Address address(sessionId, mNodeId.toString(), compId);
    envelope.metadata()->from() = address;
    notifyService(envelope);
}

void
ThreadedNodeRouter::notifyRouterShutdown()
{
    impl::Envelope envelope(new impl::ControlMessage("routershutdown"));
    notifyService(envelope);
}

void
ThreadedNodeRouter::notifyService(arras4::api::MessageContent* message) {
    arras4::impl::Envelope env(message);
    notifyService(env);
}

void
ThreadedNodeRouter::notifyService(impl::Envelope& env) {
    if (mServiceEndpoint) {
        mServiceEndpoint->queueEnvelope(env);
    } else {
        ARRAS_ERROR("Router has no service endpoint");
    }
}

} 
} 


