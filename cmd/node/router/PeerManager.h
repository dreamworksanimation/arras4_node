// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_PEERMANAGER_H__
#define __ARRAS_PEERMANAGER_H__

#include <message_api/messageapi_types.h>
#include <message_impl/Envelope.h>
#include <map>
#include <mutex>
#include <vector>
#include <memory>

// PeerManager is thread-safe storage of the various kinds of
// RemoteEndpoints. The thread safe only protects the integrity
// of the tables but it is still the responsibility of the calling
// code to make sure that an entry isn't removed before all need
// for the RemoteEndpoint is done.

namespace arras4 {
    namespace node {

        class RemoteEndpoint;
        typedef std::list<std::shared_ptr<RemoteEndpoint> > RemoteEndpointList;

class PeerManager
{
public:
    enum PeerType {
        PEER_NONE,
        PEER_CLIENT,
        PEER_NODE,
        PEER_IPC,
        PEER_LISTENER,
        PEER_SERVICE
    };

    static std::string peerTypeName(PeerType pt);

    PeerManager();
    ~PeerManager();

    std::shared_ptr<RemoteEndpoint> trackClient(const api::UUID& aId,  RemoteEndpoint* aPeer);
    std::shared_ptr<RemoteEndpoint> trackNode(const api::UUID& aId, RemoteEndpoint*  aPeer);
    std::shared_ptr<RemoteEndpoint> trackIpc(const api::UUID& aId, RemoteEndpoint*  aPeer);
    std::shared_ptr<RemoteEndpoint> trackListener(const api::UUID& aId, RemoteEndpoint*  aPeer);
    std::shared_ptr<RemoteEndpoint> findClientPeer(const api::UUID& aId) const;
    std::shared_ptr<RemoteEndpoint> findNodePeer(const api::UUID& aId) const;
    std::shared_ptr<RemoteEndpoint> findIpcPeer(const api::UUID& aId) const;
    RemoteEndpointList getListeners(const api::UUID& aId) const;
    PeerType findPeer(const RemoteEndpoint* aEndpoint, api::UUID& aId) const;
    PeerType destroyPeer(const RemoteEndpoint*  aPeer, api::UUID& aId);

    // stash messages pending for not-yet-connected clients (these will be
    // sent automatically for any new client when trackClient(...) is called
    // with the RemoteEndpoint)
    void stashEnvelope(const api::UUID& aSessionId, const impl::Envelope& anEnvelope);
    // clear any stashed messages for a client that did not make it in time
    void clearStashedEnvelopes(const api::UUID& aSessionId);

private:
    typedef std::map<api::UUID, std::shared_ptr<RemoteEndpoint> > PeerTable;
    PeerTable mClients;
    PeerTable mNodes;
    PeerTable mIpc;
    typedef std::map<api::UUID, RemoteEndpointList > ListenerTable;
    ListenerTable mListeners;

    typedef std::vector<impl::Envelope> Envelopes;
    typedef std::map<api::UUID /* session ID */, Envelopes> PendingEnvelopes;
    PendingEnvelopes mPendingEnvelopes;

    bool eraseIfFound(PeerTable& aHaystack, const RemoteEndpoint* aNeedle, api::UUID& aId);
    bool eraseIfFound(ListenerTable& aHaystack, const RemoteEndpoint* aNeedle, api::UUID& aId);
    bool findPeer(const PeerTable& aHaystack, const RemoteEndpoint* aNeedle, api::UUID& aId) const;
    bool findPeer(const ListenerTable& aHaystack, const RemoteEndpoint* aNeedle, api::UUID& aId) const;

    // thread-safety
    mutable std::mutex mMutex;
};


} 
}

#endif // __ARRAS_PEERMANAGER_H__

