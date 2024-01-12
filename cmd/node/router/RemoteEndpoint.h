// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_REMOTEENDPOINT_H__
#define __ARRAS_REMOTEENDPOINT_H__

#include "PeerManager.h"
#include "ThreadedNodeRouter.h"

#include <arras4_log/LogEventStream.h>

#include <message_api/messageapi_types.h>
#include <message_impl/Envelope.h>

#include <condition_variable>
#include <shared_impl/MessageQueue.h>
#include <core_messages/ExecutorHeartbeat.h>

#include <mutex>
#include <thread>

#include <network/network_types.h>

namespace arras4 {
    namespace impl {
        class PeerMessageEndpoint;
    }
}

namespace arras4 {
    namespace node {

        class SessionRoutingData;

        class RemoteEndpoint
        {
        public:
            
            typedef std::shared_ptr<RemoteEndpoint> Ptr;

            RemoteEndpoint(
                network::Peer* aPeer,
                const PeerManager::PeerType aType,
                const api::UUID& aUUID,
                const api::UUID& aSession,
                ThreadedNodeRouter& aThreadedNodeRouter,
                const std::string& traceInfo);

            // factory for NODE connections
            static RemoteEndpoint* createNodeRemoteEndpoint(
                const api::UUID& aUUID,
                const SessionNodeMap::NodeInfo& aNodeInfo,
                ThreadedNodeRouter& mThreadedNodeRouter,
                const std::string& traceInfo);

            virtual ~RemoteEndpoint();

            // pull in a message and its addressing envelope
            void receiveEnvelope();

            // clean up the message data
            void disposeEnvelope();

            // retrieve the most recently-received message (call receiveEnvelope() first)
            const impl::Envelope& lastEnvelope() const { return mLastEnvelope; }

            // queue Envelope to for sending by the sender thread
            void queueEnvelope(const impl::Envelope& envelope,
                               const api::AddressList& aTo);
            void queueEnvelope(const impl::Envelope& aMessage);

            // call this when done with the current message, 
            // to prevent caching large data unnecessarily (idempotent)
            void clear();

            // close the connection to the RemoteEndpoint (idempotent)
            void close();

            // wait for all the messages in the queue to be sent. This is helpful when
            // you need to make sure that something is sent particularly before shutting down
            // the connection
            bool drain(const std::chrono::milliseconds& timeout = std::chrono::milliseconds::zero());

            void flagForDestruction() {
                std::lock_guard<std::mutex> lock(mPeerSetMutex);
                ARRAS_TRACE(log::Session(mSessionId.toString()) <<
                            "RemoteEndpoint::flagForDestruction");
                if (!mFlaggedForDestruction) {
                    mThreadedNodeRouter.queueEndpointForDestruction(this);
                    mFlaggedForDestruction = true;
                }
            }

            void setPeer(network::Peer* aPeer);

            const api::UUID& sessionId() { return mSessionId; }

            // string description of the peer : e.g. "Computation(xxx)" or "Node(yyy)"
            std::string describe() const;

        protected:

            void sendEnvelope(const impl::Envelope& anEnvelope);

            // overridden by subclass (ClientRemoteEndpoint) 
            // to address a message just received
            // (in normal cases, a message is already addressed, 
            // but not when it comes from the client)
            virtual void addressReceivedEnvelope() {}
            
            friend class ListenServer;
            // support for select() in ListenServer
            int fd();
 
            impl::PeerMessageEndpoint* mMessageEndpoint = nullptr;
             
            // cache the most recent message received
            impl::Envelope mLastEnvelope;

            // host entry for NODE connections
            SessionNodeMap::NodeInfo mNodeInfo; // host info for node connection

        private:
            // used by the createNodeRemoteEndpoint factory function
            RemoteEndpoint(
                const PeerManager::PeerType aType,
                const api::UUID& aUuid,
                const SessionNodeMap::NodeInfo& aNodeInfo,
                ThreadedNodeRouter& aThreadedNodeRouter,
                const std::string& traceInfo);

            std::thread mReceiveThread;
            std::thread mSendThread;
            std::unique_ptr<impl::MessageQueue> mMessageQueue;
           
            const PeerManager::PeerType mPeerType;
            const api::UUID mUUID;

            // the send and receive threads can decide the RemoteEndpoint needs to
            // be destroyed while the main thread could be trying to destroy it based
            // on a kick. To prevent collisions on the destruction they get queued for
            // destruction and this flag prevents it from being queued more than once.
            
            mutable std::mutex mPeerSetMutex;
            std::condition_variable mPeerSetCondition;
            network::SocketPeer* mPeer = nullptr; /* protected by mutex, changes require conditional notification */
            void setPeerInternal(network::SocketPeer* aPeer);

            std::atomic<bool> mShutdown; 
            std::atomic<bool> mFlaggedForDestruction; 
          
            int onEndpointActivity();
            void receiveThread();
            void sendThread();
            void sendThreadWithConnect();

            // queue this object for destruction
            void disconnect();

            // time (secs from epoch) that next stats message 
            // should be sent (only applies to IPC connections)
            unsigned long long mNextStatsTime;

            // send heartbeat stats to the stats logger
            void initStatsTime();
            void sendStats(const impl::ExecutorHeartbeat::ConstPtr& aHeartbeat);

            // "traceInfo" is a string output in trace messages to indicate the 
            // identity of the sending and receiving processes: "C:<compid>","N:<nodeid>","client"
            std::string mTraceInfo;

        protected:
            SessionRoutingData::Ptr mRoutingData;
            ThreadedNodeRouter& mThreadedNodeRouter;
            api::UUID mSessionId;
        };

}
}

#endif // __ARRAS_REMOTEENDPOINT_H__

