// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ListenServer.h"
#include "RemoteEndpoint.h"
#include <exceptions/InternalError.h>
#include <arras4_log/Logger.h>
#include <network/SocketPeer.h>
#include <network/IPCSocketPeer.h>
#include <network/InetSocketPeer.h>
#include <sys/select.h>

#include <algorithm>
#include <array>
#include <memory>
#include <poll.h>

namespace {
// Maximum number of new peers per socket per call of ListenServer::poll().
constexpr std::size_t MAX_NEW_PEERS = 32;
}
namespace arras4 {
namespace node {



void
ListenServer::addEndpointConnectFilter(EndpointConnectedFilter aFilter)
{
    mEndpointConnectFilters.push_back(aFilter);
}

void
ListenServer::addAcceptor(network::Peer* aPeer)
{
    if (aPeer == nullptr) {
        throw impl::InternalError("Null pointer passed to ListenServer::addAcceptor");
    }

    // for now, they all need to be SocketPeer instances
    network::SocketPeer* sp = dynamic_cast<network::SocketPeer*>(aPeer);
    if (sp == nullptr) {
        throw ListenServerException("Unsupported acceptor peer type");
    }

    mAcceptors.push_back(sp);
}

namespace {

    // since (unlike select), the poll() function doesn't easily
    // map file descriptors to their event flags, we need a utility
    // to do this for us
    typedef std::map<int,pollfd*> PollfdMap;
    bool hasReadSet(int fd,PollfdMap& map) {
        PollfdMap::iterator it = map.find(fd);
        if (it != map.end()) {
            return (it->second->revents & POLLIN) != 0;
        }
        return false;
    }
}

void
ListenServer::poll(int aTimeoutMs)
{
    // construct an array of file descriptors
    // for use with poll(). We also need a map
    // of file descriptor to pfd struct to recover
    // the result
    size_t fdCount = mAcceptors.size();
    struct pollfd pfds[fdCount];
    PollfdMap pollfdMap;

    size_t index = 0;
    for (auto a : mAcceptors) {
        pfds[index].fd = a->fd();
        pfds[index].events = POLLIN;
        pollfdMap[a->fd()] = &pfds[index];
        index++;
    }

    // we're not strictly enforcing the timeout : if poll()
    // ends early because of a signal, that's ok...
    int r = ::poll(pfds, fdCount, aTimeoutMs);

    if (r < 0) {
        throw impl::InternalError("ListenServer failed to select()");
    }

    // Declaring the peers array outside of the for-loop as an allocation
    // optimization. During each iterations the acceptAll() method
    // overwrites the contents of the array, and sets nPeers to the
    // number of new peers found.
    //
    // Note: it is possible that the first iteration would find 3
    // new peers while in the second iteration only 1 new peer
    // is found. In this situation peers[0] would contain this new peer,
    // and nPeers would be set to 1, while peers[1] and peers[2] would
    // still contain the peers from the previous iteration.
    // While this isn't ideal in practice this isn't a problem so long
    // as the nPeers variable is respected.
    std::array<network::Peer*, MAX_NEW_PEERS> peers;
    for (auto a : mAcceptors) {
        if (hasReadSet(a->fd(),pollfdMap)) {

            network::Peer** ppPeers = peers.data();
            int nPeers = static_cast<int>(peers.size());

            // this should throw if the accept fails for any reason
            a->acceptAll(ppPeers, nPeers);

            std::string peerType;
            if (dynamic_cast<network::IPCSocketPeer*>(a)) {
                peerType = "IPC";
            } else if (dynamic_cast<network::InetSocketPeer*>(a)) {
                peerType = "Inet";
            } else {
                peerType = "Unknown";
            }

            ARRAS_LOG_DEBUG("[ListenServer](%s) %d new peers", peerType.c_str(), nPeers);

            // if we introduced any new remote endpoints, process them now
            for (int i=0; i<nPeers; ++i) {

                // invoke the filter chain on the new peers; the first one to
                // return a non-null RemoteEndpoint* wins
                RemoteEndpoint* ep = nullptr;
                ConnectFilterContext* ctx = nullptr;

                network::Peer*& peer = peers[i];

                for (auto& f : mEndpointConnectFilters) {
                    try {
                        ep = nullptr;
                        if (peer) {
                            ep = f(peer, &ctx);
                            if (ep) {
                                // the endpoint took ownership of the peer
                                peer = nullptr;

                                // no need to keep searching
                                break;
                            }
                        }
                    } catch (const std::exception& e) {
                        ARRAS_LOG_ERROR("[ListenServer] (accept) %s", e.what());

                        // if the filter did not swallow the exception, then
                        // stop processing the connection immediately and terminate it
                        break;
                    }
                }

                delete ctx;

                // shut down the peer if someone didn't take ownership
                if (peer != nullptr) {
                    peer->shutdown();
                    delete peer;
                    peer = nullptr;
                }
            }
        }
    }
}

}
}

