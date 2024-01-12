// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "RemoteEndpoint.h"
#include "RouteMessage.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <message_impl/Envelope.h>
#include <network/SocketPeer.h>
#include <core_messages/ControlMessage.h>
#include <core_messages/SessionStatusMessage.h>
#include <shared_impl/RegistrationData.h>

#include <map>
#include <unistd.h>

using namespace arras4::api;
using namespace arras4::impl;

namespace {

void
parseDestinationAddressLists(
        /*in*/ const UUID& aLocalNodeId,
        /*in*/ const AddressList& aTo,
        /*out*/ std::map<UUID, AddressList>& aIpcLists,  // mapped by computation id
        /*out*/ std::map<UUID, AddressList>& aNodeLists, // mapped by node id
        /*out*/ bool& aToClient
        )
{
    aIpcLists.clear();
    aNodeLists.clear();
    aToClient = false;

    for (const auto& to : aTo) {
       
        // first thing -- if it's got no node ID, it's for the client
        // (normally expect only one of these, but easier to be consistent)
        if (to.node == UUID::null) {
            aToClient = true;
        }

        // then, if it's for this node, add it to the IPC list
        else if (to.node == aLocalNodeId && to.computation != UUID::null) {
            AddressList& addrs = aIpcLists[to.computation];
            addrs.push_back(to);
        }

        // otherwise, it's for a remote node, so put it in the list for that node
        else if (to.node != aLocalNodeId) {
            AddressList& addrs = aNodeLists[to.node];
            addrs.push_back(to);
        }
    }
}

} // end anonymous namespace

namespace arras4 {
namespace node {

// send a message to the local client and listeners. Only to be used if this is the
// entry node for the session
void
sendToLocalClient(const UUID& sessionId,
                  const Envelope& envelope,
                  ThreadedNodeRouter& aThreadedNodeRouter)
{
    RemoteEndpoint::Ptr client = aThreadedNodeRouter.findClientPeer(sessionId);

    if (client) {
        ARRAS_TRACE("[routeMessage]     forwarding to client");
        client->queueEnvelope(envelope);
    } else {
        // if we are supposed to have the client, and we didn't find them, then
        // they have not connected yet and we should stash messages for them until they do connect
        aThreadedNodeRouter.stashEnvelope(sessionId, envelope);
    }
}

// send a message to a local computation. Only to be used if this is the
// host node for the computation
void
sendToLocalComputation(const UUID& sessionId,
                       const UUID& computationId,
                       const Envelope& envelope,
                       ThreadedNodeRouter& aThreadedNodeRouter)
{
    RemoteEndpoint::Ptr dest = aThreadedNodeRouter.findIpcPeer(computationId);     
    if (dest) {
        dest->queueEnvelope(envelope);
    }    
    else {
        // must be a bug, since the computation was supposed to be local
        ARRAS_ERROR(log::Id("computationNotFound") <<
                    log::Session(sessionId.toString()) <<
                    "Could not find IPC endpoint for local computation id " <<
                    computationId.toString());
    }
}

// route a message to its correct destinations
void
routeMessage( const Envelope& envelope,
              SessionRoutingData::Ptr aRoutingData,
              ThreadedNodeRouter& aThreadedNodeRouter)
{
    // all destinations will be within the same session
    const UUID& sessionId = aRoutingData->sessionId();

    // this node
    const UUID& nodeId = aRoutingData->nodeId();

    std::map<UUID, AddressList> nodeLists;
    std::map<UUID, AddressList> ipcLists;
    bool toClient;

    // parseDestinationAddressLists() will split out the addresses into their proper places
    const AddressList& destinations = envelope.to();
    parseDestinationAddressLists(nodeId, destinations, ipcLists, nodeLists, toClient);

    if (toClient) {     

        if (aRoutingData->isEntryNode()) {
            // client is local to this node, so just send it
            sendToLocalClient(sessionId, envelope, aThreadedNodeRouter);
        
        } else {

            // it needs to go to a different node, find the 'entry' node and
            // send it there
            UUID entryNodeId = aRoutingData->nodeMap().getEntryNodeId();
            Address entryNodeAddr;
            entryNodeAddr.session = sessionId;
            nodeLists[entryNodeId].push_back(entryNodeAddr);
        }
    }

    // send messages along to IPC destinations
    for (const auto& a : ipcLists) {
        sendToLocalComputation(sessionId,a.first,envelope,aThreadedNodeRouter);
    }

    // and then remote destinations
    for (const auto& a : nodeLists) {
        RemoteEndpoint::Ptr dest = aThreadedNodeRouter.findNodePeer(a.first);
        if (!dest) {

            // this is the evil double check pattern that computer scientists
            // tell us to never do but that on Intel processors with compilers
            // that we actually use this is a useful optimization. The theoretical
            // problem is that 
            std::lock_guard<std::mutex> lock(aThreadedNodeRouter.mNodeConnectionMutex);
            dest = aThreadedNodeRouter.findNodePeer(a.first);
            if (!dest) {
                ARRAS_DEBUG("Connecting from node '" << aRoutingData->nodeId().toString() <<
                            "' to node '" << a.first.toString() << "'");
                const SessionNodeMap::NodeInfo& nodeInfo = aRoutingData->nodeMap().getNodeInfo(a.first);
                std::string traceInfo("N:"+nodeId.toString()+" N:"+a.first.toString());
                RemoteEndpoint* ep = RemoteEndpoint::createNodeRemoteEndpoint(a.first, nodeInfo, 
                                                                              aThreadedNodeRouter,traceInfo);
                dest = aThreadedNodeRouter.trackNode(a.first, ep);

            }
        }

        if (dest) {
            dest->queueEnvelope(envelope,a.second);
        } else {
            // TODO: warn? fail? except?
            ARRAS_ERROR(log::Id("nodeNotFound") <<
                        "Could not find destination node for message, node ID " << a.first.toString());
        }
    }
}

}
}

