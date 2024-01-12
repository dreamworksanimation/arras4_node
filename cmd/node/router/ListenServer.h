// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_NODE_LISTENSERVER_H__
#define __ARRAS_NODE_LISTENSERVER_H__

#include <exception>
#include <functional>
#include <string>
#include <vector>

namespace arras4 {
    namespace api {
          class UUID;
    }
}
namespace arras4 {

    namespace network {
        class Peer;
        class SocketPeer;
    }

    namespace node {

        class RemoteEndpoint;

        class ListenServerException : public std::exception
        {
        public:
            ListenServerException(const std::string& aDetail)
                : mMessage(aDetail) {}
            ~ListenServerException() throw() {}
            const char* what() const throw() { return mMessage.c_str(); }

        private:
            std::string mMessage;
        };

        class ListenServer
        {
        public:
            ListenServer() {}
            ~ListenServer() {}

            // provide callback code with a context, on which they can hang any
            // data that they need to track during new-connection filtering.
            // This context will be live until the new-connection filtering
            // process completes, at which time it will be deleted.
            struct ConnectFilterContext {
                ConnectFilterContext() {}
                virtual ~ConnectFilterContext() {}
            };

            // when a new remote endpoint connects, ListenServer will call all
            // registered EndpointConnectedFilter instances, in the order they
            // were registered. Each filter can alter the RemoteEndpoint in any
            // way supported by that class, and can veto the connection for any
            // reason by returning "false" when invoked. Otherwise, the filter
            // should return "true" to OK the connection.
            // The ConnectFilterContext** will point to a pointer to an object
            // that lives while this particular connection is being filtered;
            // any callback can (re)set this at any time; if the pointer is
            // valid at the end of the connection filtering, it will be deleted.
            typedef std::function<RemoteEndpoint*(network::Peer*, ConnectFilterContext**)> EndpointConnectedFilter;
            void addEndpointConnectFilter(EndpointConnectedFilter aFilter);

            // add an "accepting" endpoint, which does nothing but accept
            // incoming connections (and invoke the filter chain for each).
            // ListenServer TAKES OWNERSHIP of these instances' lifetimes.
            // Acceptor must be configured and ready to accept connections.
            void addAcceptor(network::Peer* aPeer);

            // check for activity on each acceptor ; invokes filters
            // for incoming connections, aTimeoutMs < 0 indicates "wait forever";
            // any other value indicates "wait for that number of ms before returning,
            // whether or not any activity has been detected".
            // This method can throw InternalErrorException and PeerException.
            void poll(int aTimeoutMs = -1);

        private:
            std::vector<EndpointConnectedFilter> mEndpointConnectFilters;
            std::vector<network::SocketPeer*> mAcceptors;
   
        };

    } 
}

#endif // __ARRAS_NODE_LISTENSERVER_H__

