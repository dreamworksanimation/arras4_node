// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "PeerManager.h"
#include "RemoteEndpoint.h"

#define AUTO_LOCK(m) std::lock_guard<std::mutex> __LOCK(m)
#define AUTO_RLOCK(rm) std::lock_guard<std::recursive_mutex> __LOCK(rm)

using namespace arras4::api;

namespace arras4 {
namespace node {

/* static */ std::string 
PeerManager::peerTypeName(PeerType pt)
{
    switch (pt) {
    case PEER_CLIENT: return "Client";
    case PEER_NODE: return "Node";
    case PEER_IPC: return "Computation";
    case PEER_LISTENER: return "Listener";
    case PEER_SERVICE: return "Service";
    case PEER_NONE: return "None";
    }    
    return "Unknown Peer Type";   
}

PeerManager::PeerManager()
{

}

PeerManager::~PeerManager()
{

}

RemoteEndpoint::Ptr
PeerManager::trackClient(const UUID& aId,  RemoteEndpoint* aPeer)
{
    AUTO_LOCK(mMutex);
    RemoteEndpoint::Ptr peerPtr(aPeer);
    mClients[aId] = peerPtr;

    // deliver any messages that have been stashed for this client
    Envelopes& msgs = mPendingEnvelopes[aId];

    for (const auto& msg : msgs) {
        aPeer->queueEnvelope(msg);
    }
    return peerPtr;

}

RemoteEndpoint::Ptr
PeerManager::trackNode(const UUID& aId, RemoteEndpoint*  aPeer)
{
    AUTO_LOCK(mMutex);
    RemoteEndpoint::Ptr peerPtr(aPeer);
    mNodes[aId] = peerPtr;
    return peerPtr;
}

RemoteEndpoint::Ptr
PeerManager::trackIpc(const UUID& aId, RemoteEndpoint*  aPeer)
{
    AUTO_LOCK(mMutex);
    RemoteEndpoint::Ptr peerPtr(aPeer);
    mIpc[aId] = peerPtr;
    return peerPtr;
}

RemoteEndpoint::Ptr
PeerManager::trackListener(const UUID& aId, RemoteEndpoint*  aPeer)
{
    AUTO_LOCK(mMutex);
    RemoteEndpoint::Ptr peerPtr(aPeer);
    mListeners[aId].push_back(peerPtr);
    return peerPtr;
}

RemoteEndpoint::Ptr
PeerManager::findClientPeer(const UUID& aId) const
{
    AUTO_LOCK(mMutex);
    auto it = mClients.find(aId);
    if (it != mClients.end()) return it->second;

    return RemoteEndpoint::Ptr();
}

RemoteEndpoint::Ptr
PeerManager::findNodePeer(const UUID& aId) const
{
    AUTO_LOCK(mMutex);
    auto it = mNodes.find(aId);
    if (it != mNodes.end()) return it->second;

    return RemoteEndpoint::Ptr();
}

RemoteEndpoint::Ptr
PeerManager::findIpcPeer(const UUID& aId) const
{
    AUTO_LOCK(mMutex);
    auto it = mIpc.find(aId);
    if (it != mIpc.end()) return it->second;

    return RemoteEndpoint::Ptr();
}

// returns copy to avoid threading issues
RemoteEndpointList 
PeerManager::getListeners(const UUID& aId) const
{
    AUTO_LOCK(mMutex);
    auto it = mListeners.find(aId);
    if (it != mListeners.end()) return it->second;
    
    return RemoteEndpointList();
}

PeerManager::PeerType
PeerManager::findPeer(const RemoteEndpoint* aEndpoint, UUID& aId)
const
{
    // no need to lock since each of the called functions locks
    if (findPeer(mClients, aEndpoint, aId)) return PEER_CLIENT;
    else if (findPeer(mNodes, aEndpoint, aId)) return PEER_NODE;
    else if (findPeer(mIpc, aEndpoint, aId)) return PEER_IPC;
    else if (findPeer(mListeners, aEndpoint, aId)) return PEER_LISTENER;
    else return PEER_NONE;
}

// while the mutex prevents corruption of the tables it is the responsibilty
// of the caller to know that the RemoteEndpoint is no longer needed when
// this is called
PeerManager::PeerType
PeerManager::destroyPeer(const RemoteEndpoint*  aPeer, UUID& aId)
{
    if (eraseIfFound(mClients, aPeer, aId)) return PEER_CLIENT;
    else if (eraseIfFound(mNodes, aPeer, aId)) return PEER_NODE;
    else if (eraseIfFound(mIpc, aPeer, aId)) return PEER_IPC;
    else if (eraseIfFound(mListeners, aPeer, aId)) return PEER_LISTENER;
    else return PEER_NONE;
}

void
PeerManager::stashEnvelope(const UUID& aSessionId, const impl::Envelope& anEnvelope)
{
    AUTO_LOCK(mMutex);

    // need to check again for the client while locked
    // and either queue it or stash it
    auto it = mClients.find(aSessionId);
    if (it != mClients.end()) {
        it->second->queueEnvelope(anEnvelope);
    } else {
        Envelopes& msgs = mPendingEnvelopes[aSessionId];
        msgs.push_back(anEnvelope);
    }
}

void
PeerManager::clearStashedEnvelopes(const UUID& aSessionId)
{
    AUTO_LOCK(mMutex);
    mPendingEnvelopes.erase(aSessionId);
}

// while the mutex prevents corruption of the tables it is the responsibilty
// of the caller to know that the RemoteEndpoint is no longer needed when
// this is called
bool
PeerManager::eraseIfFound(PeerTable& aHaystack, const RemoteEndpoint* aNeedle, UUID& aId)
{
    bool found = false;
    AUTO_LOCK(mMutex);
    for (PeerTable::iterator it = aHaystack.begin(); it != aHaystack.end(); ++it) {
        if (it->second.get() == aNeedle) {
            aId = it->first;
            aHaystack.erase(it);
            found = true;
            break;
        }
    }

    return found;
}

bool
PeerManager::eraseIfFound(ListenerTable& aHaystack, const RemoteEndpoint* aNeedle, UUID& aId)
{
    bool found = false;
    AUTO_LOCK(mMutex);
    for (ListenerTable::iterator it = aHaystack.begin(); it != aHaystack.end(); ++it) {
        for (RemoteEndpointList::iterator jt = it->second.begin();
             jt != it->second.end(); ++jt) {
            if (jt->get() == aNeedle) {
                aId = it->first;
                it->second.erase(jt);
                found = true;
                break;
            }
        }
        if (found) {
            if (it->second.empty())
                aHaystack.erase(it);
            break;
        }
    }

    return found;
}

bool
PeerManager::findPeer(const PeerTable& aHaystack, const RemoteEndpoint* aNeedle, UUID& aId) const
{
    bool found = false;
    AUTO_LOCK(mMutex);
    for (auto& it : aHaystack) {
        if (it.second.get() == aNeedle) {
            aId = it.first;
            found = true;
            break;
        }
    }

    return found;
}

bool
PeerManager::findPeer(const ListenerTable& aHaystack, const RemoteEndpoint* aNeedle, UUID& aId) const
{ 
    bool found = false;
    AUTO_LOCK(mMutex);
    for (auto& it : aHaystack) {
        for (auto& jt : it.second) {
            if (jt.get() == aNeedle) {
                aId = it.first;
                found = true;
                break;
            }
        }
        if (found) break;
    }
    return found;
}

}
}

