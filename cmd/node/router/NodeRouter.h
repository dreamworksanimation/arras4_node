// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_NODEROUTER_H__
#define __ARRAS_NODEROUTER_H__

#include "NodeRouterOptions.h"
#include "RoutingTable.h"
#include "ThreadedNodeRouter.h"

#include <message_api/messageapi_types.h>

#include <network/SocketPeer.h>
#include <network/IPCSocketPeer.h>
#include <network/InetSocketPeer.h>
#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

/**
 * @brief NodeRouter manages two endpoints (socket and IPC) and moves instances of arras::network::Message between them
 *        based on information in the Message address envelope, and the address information of clients when they
 *        connect to an endpoint.
 */

#define AUTO_LOCK(m) std::lock_guard<std::mutex> __LOCK(m)
#define AUTO_RLOCK(rm) std::lock_guard<std::recursive_mutex> __LOCK(rm)

namespace arras4 {

    namespace node {

        class PeerManager;

        class RemoteEndpoint;
        class ListenServer;

        class NodeRouter
        {
        public:
            NodeRouter(const api::UUID& aNodeId, unsigned short aInetSocket, unsigned short aIPCSocket);
            ~NodeRouter();

            void start();
            void stop();
            void requestShutdown();

            // tear down the indicated session
            void cleanupSession(const api::UUID& aSessId);

            void sendSessionStatusToClient(const std::string& aSessionStatusJson,
                                           RemoteEndpoint& aEndPoint);

            // not temp
            void kickClient(const api::UUID& aSessId, const std::string& aReason, const std::string& statusJson);

            SessionRoutingData::Ptr putSessionRoutingData(const api::UUID& aSessionId,api::ObjectConstRef aRoutingData);
            SessionRoutingData::Ptr getSessionRoutingData(const api::UUID& aSessionId);

            ThreadedNodeRouter mThreadedNodeRouter;

	    // called by standalone_router so that NodeRouter knows the port it is listening on
	    // -- for other uses of NodeRouter this remains at 0
	    void setInetPort(unsigned port) { mInetPort = port; }

        private:

            // our node's unique ID
            const api::UUID& getNodeId() {
                return mThreadedNodeRouter.getNodeId();
            }

            // traffic to and from the IP network
	    unsigned mInetPort = 0; // only set for standalone_router use case
            network::SocketPeer* mNetwork = nullptr;
            network::SocketPeer* mIPC = nullptr;

            // listen/select thread
            std::thread mThread;

            // handler of messages from NodeService
            std::thread mServiceToRouterThread;
            void serviceToRouterProc();

            // run/stop flag
            int mRun = 1;

            std::atomic<bool> mRequestShutdown;

            // new-peer-connected filters
            RemoteEndpoint* clientConnectedFilter(network::Peer*);
            RemoteEndpoint* ipcConnectedFilter(network::Peer*);
            RemoteEndpoint* nodeConnectedFilter(network::Peer*);

            // endpoint-activity callback
            void onEndpointActivity(RemoteEndpoint&);
            void threadProcOld();
            void threadProc();
        };
    } 
} 
#endif // __ARRAS_NODEROUTER_H__

