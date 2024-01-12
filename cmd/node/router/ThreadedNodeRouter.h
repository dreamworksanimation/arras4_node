// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_THREADEDNODEROUTER_H__
#define __ARRAS_THREADEDNODEROUTER_H__

// this is NodeRouter state which will be used by multiple threads
// at the same time

#include "PeerManager.h"
#include "RoutingTable.h"
#include "SessionRoutingData.h"

#include <arras4_log/Logger.h>
#include <core_messages/ExecutorHeartbeat.h>
#include <message_api/messageapi_types.h>
#include <message_api/Object.h>
#include <shared_impl/MessageQueue.h>
#include <message_impl/Envelope.h>


#include <list>
#include <memory>
#include <mutex>

namespace arras4 {
namespace node {

class RemoteEndpoint;

class ThreadedNodeRouter {

  public:
    ThreadedNodeRouter(const api::UUID& aNodeId);

    // These are made thread safe by RoutingTable internal locks
    // It is the responsibility of the caller to know that the
    // SessionRoutingData::Ptr is referenced somewhere else before 
    // doing a release on it it or it could be removed before it is used.
    SessionRoutingData::Ptr sessionRoutingData(const api::UUID& aSessionId) const;
    SessionRoutingData::Ptr addSessionRoutingData(const api::UUID& aSessionId,api::ObjectConstRef aRoutingData);
    void releaseSessionRoutingData(const api::UUID& aSessionId) {
        mRoutingTable.releaseSessionRoutingData(aSessionId);
    }
    void deleteSessionRoutingData(const api::UUID& aSessionId) {
        mRoutingTable.deleteSessionRoutingData(aSessionId);
    }

    // These are made thread safe by PeerManager internal locks
    std::shared_ptr<RemoteEndpoint> trackNode(const api::UUID& aId,RemoteEndpoint* aRemoteEndpoint) {
        return mPeerManager.trackNode(aId, aRemoteEndpoint);
    }
    std::shared_ptr<RemoteEndpoint> trackIpc(const api::UUID& aId, RemoteEndpoint* aRemoteEndpoint) {
        return mPeerManager.trackIpc(aId, aRemoteEndpoint);
    }
    PeerManager::PeerType findPeer(const RemoteEndpoint* aRemoteEndpoint, api::UUID& aId) const {
        return mPeerManager.findPeer(aRemoteEndpoint, aId);
    }
    std::shared_ptr<RemoteEndpoint> findIpcPeer(const api::UUID& aId) const {
        return mPeerManager.findIpcPeer(aId);
    }
    std::shared_ptr<RemoteEndpoint> findClientPeer(const api::UUID& aId) const {
        return mPeerManager.findClientPeer(aId);
    }
    std::shared_ptr<RemoteEndpoint> findNodePeer(const api::UUID& aId) const {
        return mPeerManager.findNodePeer(aId);
    }
    PeerManager::PeerType untrackPeer(const RemoteEndpoint* aRemoteEndpoint, api::UUID& aId) {
        return mPeerManager.destroyPeer(aRemoteEndpoint, aId);
    }

    // these aren't thread safe but should only be called by the main thread
    std::shared_ptr<RemoteEndpoint> trackClient(const api::UUID& aId,  RemoteEndpoint* aRemoteEndpoint) {
        return mPeerManager.trackClient(aId, aRemoteEndpoint);
    }
    void clearStashedEnvelopes(const api::UUID& aSessionId) {
        mPeerManager.clearStashedEnvelopes(aSessionId);
    }
    void stashEnvelope(const api::UUID& aSessionId, const impl::Envelope& anEnvelope) {
        mPeerManager.stashEnvelope(aSessionId, anEnvelope);
    }

    // this is thread safe because mNodeId is only set during construction of
    // ThreadedNodeRouter
    const api::UUID& getNodeId() const {
        return mNodeId;
    }
    // this isn't a threading problem because it is set once during setup
    const RemoteEndpoint* getServiceEndpoint() const {
        return mServiceEndpoint;
    }
    void setServiceEndpoint(RemoteEndpoint* aEndpoint) {
        mServiceEndpoint = aEndpoint;
    }

    void destroyEndpoints();

    void notifyClientDisconnected(const api::UUID& aSessionId, const std::string& aReason);
    void notifyClientConnected(const api::UUID& aSessionId);
    void notifyComputationStatus(const api::UUID& aSessionId, const api::UUID& aCompId,
                                 const std::string& aStatus);
    void notifyHeartbeat(std::shared_ptr<const impl::ExecutorHeartbeat>& heartbeat, const std::string& sessionId, const std::string& comp);
    void notifyRouterShutdown();
    void notifyService(arras4::api::MessageContent* message);
    void notifyService(impl::Envelope& env);

    bool findNodeInfo(const api::UUID& aNodeId, SessionNodeMap::NodeInfo& info) const {
        return mRoutingTable.findNodeInfo(aNodeId, info);
    }

    // manage messages from  NodeService
    void pushServiceToRouterQueue(impl::Envelope& env) {
        mServiceToRouterQueue->push(env);
    }
    arras4::impl::Envelope popServiceToRouterQueue(unsigned int microseconds) {
        static std::chrono::microseconds timeout(microseconds);
        arras4::impl::Envelope env;
        mServiceToRouterQueue->pop(env,timeout);
        return env;
    }

    std::mutex mNodeConnectionMutex;

    void serviceDisconnected();
    void waitForServiceDisconnected();

  private:
    RoutingTable mRoutingTable;
    PeerManager mPeerManager;
    const api::UUID mNodeId;

    // the set of RemoteEndpoints which need to be untracked and deleted
    std::list<RemoteEndpoint*> mEndpointsToDelete;
    mutable std::mutex mEndpointsToDeleteMutex;

    // this should only be called by flagForDestruction()
    void queueEndpointForDestruction(RemoteEndpoint* aEndpoint) {
        std::lock_guard<std::mutex> lock(mEndpointsToDeleteMutex);
        mEndpointsToDelete.push_back(aEndpoint);
    }
    friend class RemoteEndpoint;

    RemoteEndpoint* mServiceEndpoint;

    std::unique_ptr<arras4::impl::MessageQueue> mServiceToRouterQueue;

    bool mServiceDisconnected;
    std::mutex mServiceDisconnectedMutex;
    std::condition_variable mServiceDisconnectedCondition;
};

} // end namespace node
} // end namespace arras4

#endif // __ARRAS_THREADEDNODEROUTER_H__

