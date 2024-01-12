// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "pthread_create_interposer.h"
#include "RemoteEndpoint.h"
#include "RouteMessage.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <arras4_athena/AthenaLogger.h>

#include <network/InetSocketPeer.h>
#include <network/SocketPeer.h>

#include <shared_impl/MessageQueue.h>
#include <shared_impl/RegistrationData.h>

#include <message_impl/messaging_version.h>
#include <message_impl/PeerMessageEndpoint.h>

#include <core_messages/ControlMessage.h>
#include <core_messages/PongMessage.h>

#include <exceptions/InternalError.h>
#include <exceptions/ShutdownException.h>

#include <boost/format.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>
#include <unistd.h>
#include <sys/time.h>
#include <poll.h>

// interval between sending stats
constexpr unsigned long long SEND_STATS_INTERVAL_SECS = 30;

using namespace std::placeholders;
using namespace arras4::api;
using namespace arras4::impl;
using namespace arras4::network;

namespace arras4 {
namespace node {

std::string
RemoteEndpoint::describe() const
{
    if (mPeerType == PeerManager::PEER_CLIENT) {
        return "client";
    } else if (mPeerType == PeerManager::PEER_NODE) {
        return "node("+mUUID.toString()+")";
    } else if (mPeerType == PeerManager::PEER_IPC) {
        return "computation("+mUUID.toString()+")";
    } else if (mPeerType == PeerManager::PEER_SERVICE) {
        return "service";
    }
    return "peer("+mUUID.toString()+")";
}

void
RemoteEndpoint::disconnect()
{
    if (mPeerType == PeerManager::PEER_CLIENT) {
        ARRAS_DEBUG(log::Session(mSessionId.toString()) <<
                   "Client disconnected");

        mThreadedNodeRouter.notifyClientDisconnected(mSessionId, "clientDroppedConnection");
    } else if (mPeerType == PeerManager::PEER_SERVICE) {
        ARRAS_DEBUG("arras4_node has disconnected. Shutting down arras4_noderouter.");
        mThreadedNodeRouter.serviceDisconnected();
    }
    flagForDestruction();
}

// sendThread() simply gets messages off of the queue and sends them. We
// already know that the message needs to go out on the socket associated
// with this RemoveEndpoint.
void
RemoteEndpoint::sendThread()
{
    // set a thread specific prefix for log messages from this thread 
    std::string threadName = PeerManager::peerTypeName(mPeerType) + " EP sendThread";
    log::Logger::instance().setThreadName(threadName.c_str());

    while (1) {
        impl::Envelope envelope;        
        bool shouldDisconnect = false;
        try {  
            mMessageQueue->pop(envelope);
            if (mShutdown) return;
            sendEnvelope(envelope);
        } 

        catch (const impl::ShutdownException &) {
            // RemoteEndpoint destructor shuts down the message queue, causing this thread to exit
            ARRAS_DEBUG(log::Session(mSessionId.toString()) <<
                       "[RemoteEndpoint::sendThread] send queue was shutdown, terminating send thread");
            return;
        } 

        catch (const PeerDisconnectException&){
            ARRAS_WARN(log::Id("warnDisconnect") << 
                                log::Session(mSessionId.toString()) <<
                                describe() << "disconnected from node during message send");
            shouldDisconnect = true;
        } 

        catch (const PeerException& e) {
            PeerException::Code code = e.code(); 
            if (code == PeerException::CONNECTION_RESET) {
                ARRAS_WARN(log::Id("warnConnectionReset") << 
                           log::Session(mSessionId.toString()) <<
                           "The connection to " << describe() << " was reset during message send");
            } else if (code == PeerException::CONNECTION_CLOSED) {
                ARRAS_WARN(log::Id("warnConnectionClosed") << 
                           log::Session(mSessionId.toString()) <<
                           "The connection to " << describe() << " was closed during message send");
            } else {
                ARRAS_ERROR(log::Id("peerExceptionSend") << 
                            log::Session(mSessionId.toString()) <<
                            "PeerException (code " << (int) code << ") sending message to " <<
                            describe() << std::string(e.what()));
            }
            shouldDisconnect = true;
        } 

        catch (const std::exception& e) {
            ARRAS_ERROR(log::Id("sendException") << 
                        log::Session(mSessionId.toString()) <<
                        "Exception during message send: " << std::string(e.what()));
        
            shouldDisconnect = true;
        }
        
        catch (...) {
            ARRAS_WARN(log::Id("warnSendException") << 
                       log::Session(mSessionId.toString()) <<
                       "Unknown exception caught while sending message");
            shouldDisconnect = true;
        }

        if (shouldDisconnect) {
            disconnect();
            return; // exit thread
        }
    }
}

void
RemoteEndpoint::sendThreadWithConnect()
{

    if (mPeerType == PeerManager::PEER_NODE) {
        InetSocketPeer* peer = new InetSocketPeer();

        try {
            peer->connect(mNodeInfo.ip.c_str(), mNodeInfo.port);

            // register with the other node
            // RegistrationData is always initialized using the same #defines but they need
            // to be provided here to make sure they are baked at compile time rather than
            // link time.
            impl::RegistrationData regData(ARRAS_MESSAGING_API_VERSION_MAJOR,
                                           ARRAS_MESSAGING_API_VERSION_MINOR,
                                           ARRAS_MESSAGING_API_VERSION_PATCH);
            regData.mType = impl::REGISTRATION_NODE;
            regData.mNodeId = mThreadedNodeRouter.getNodeId();
            peer->send(&regData, sizeof(regData));
        } catch (const PeerException& e) {
            ARRAS_ERROR(log::Id("connectError") <<
                        "Error when connecting to remote node " <<
                        mNodeInfo.nodeId.toString() << ": " << std::string(e.what()));
            flagForDestruction();
        }

        if (mUUID < mThreadedNodeRouter.mNodeId) {
            // this node is higher so just use the connection
            {
                std::unique_lock<std::mutex> lock(mPeerSetMutex);
                setPeerInternal(peer);
                mPeerSetCondition.notify_all();
            }
        } else {
            // this node is lower valued that the remote node so the Peer will
            // only be temporary to negotiate the connection. The remote node
            // will be responsible for setting up the real connection.
            
            {
                std::unique_lock<std::mutex> lock(mPeerSetMutex);
                while (mPeer == nullptr) {
                    mPeerSetCondition.wait(lock);
                    if (mShutdown) return;
                }
            }
        }
    }

    sendThread();
}

// timeout in milliseconds of reads
const int ENDPOINT_POLL_TIMEOUT = 1000;

int
RemoteEndpoint::onEndpointActivity()
{
    // anything we do here that is on a disconnected or otherwise invalid endpoint, will
    // cause an exception to be raised, so let that bubble up the stack to be handled by
    // callers
    receiveEnvelope();

    if (mPeerType == PeerManager::PEER_SERVICE) {
        mThreadedNodeRouter.pushServiceToRouterQueue(mLastEnvelope);

    } else if (mLastEnvelope.classId() == impl::ControlMessage::ID) {
        // ControlMessage are not routed
        if (mPeerType == PeerManager::PEER_CLIENT) { 
            auto cm = mLastEnvelope.contentAs<impl::ControlMessage>();
            // Note that this relies on the fact that receive() always deserializes Control messages
            if (cm && cm->command() == "disconnect") {
                mThreadedNodeRouter.notifyClientDisconnected(mSessionId, "clientShutdown");
            }
        } else if (mLastEnvelope.to().size() == 1
                   && mLastEnvelope.to().front().computation.isNull()
                   && mLastEnvelope.to().front().node == mThreadedNodeRouter.getNodeId()) {
            ARRAS_ERROR(log::Id("badControlMessage") <<
                        log::Session(mSessionId.toString()) <<
                        "Unexpected control message from " << describe());
        }
       
    } else if (mLastEnvelope.classId() == impl::ExecutorHeartbeat::ID) {
        // ExecutorHeartbeat are not routed
        // These should only come from IPC connections. Ignore anything else
        if (mPeerType == PeerManager::PEER_IPC) {
            // note this relies on the fact that receive() always deserializes ExecutorHeartbeat
            auto heartbeat = mLastEnvelope.contentAs<impl::ExecutorHeartbeat>();

            // forward heartbeats to NodeService. need to add a from address because the computation doesn't
            mThreadedNodeRouter.notifyHeartbeat(heartbeat, mSessionId.toString(), mUUID.toString());
            
            // send stats to stats log, if it's time
            sendStats(heartbeat);
        }
    } else {
        if (mPeerType == PeerManager::PEER_NODE) {
            // can't use cached routing information, get it for the session
            const UUID& sessionId = mLastEnvelope.to().front().session;
            SessionRoutingData::Ptr routingData = mThreadedNodeRouter.sessionRoutingData(sessionId);
            if (routingData == nullptr) {
                ARRAS_WARN("Received message for unknown session(" << sessionId.toString() << ") from " << describe());
            } else {
                routeMessage(mLastEnvelope, routingData, mThreadedNodeRouter);
            }
        } else {
            routeMessage(mLastEnvelope, mRoutingData, mThreadedNodeRouter);
        }
    }

    disposeEnvelope();
    return 0;
}

void
RemoteEndpoint::receiveThread()
{ 
    // set a thread specific prefix for log messages from this thread
    std::string sessionIdStr = mSessionId.toString();
    std::string threadName = PeerManager::peerTypeName(mPeerType) + " EP receiveThread";
    log::Logger::instance().setThreadName(threadName);

    {
        std::unique_lock<std::mutex> lock(mPeerSetMutex);
        while (mPeer == nullptr) {
            mPeerSetCondition.wait(lock);
            if (mShutdown) return;
        }
    }

    while (1) { 

        struct pollfd pfd;
        pfd.fd = fd();
        pfd.events = POLLIN;

        int r = ::poll(&pfd, 1, ENDPOINT_POLL_TIMEOUT);

        if (r < 0) {
            disconnect();
            return; // exit thread
        }

        // exit the thread when asked to shutdown
        if (mShutdown) return;

        if (r == 1) {
            bool shouldDisconnect = false;
            // disconnect, reset and close can all happen during a
            // node shutdown, so they are not logged as errors
            try {
                onEndpointActivity();
            } catch (const PeerDisconnectException&){
                ARRAS_WARN(log::Id("warnDisconnected") <<
                           log::Session(sessionIdStr) <<
                           describe() << " disconnected from node");
                shouldDisconnect = true;
            } catch (const PeerException& e) {
                PeerException::Code code = e.code();
                if (code == PeerException::CONNECTION_RESET) {
                    ARRAS_WARN(log::Id("warnConnectionReset") <<
                               log::Session(sessionIdStr) <<
                               "The connection to " << describe() << " was reset");
                } else if (code == PeerException::CONNECTION_CLOSED) {
                    ARRAS_WARN(log::Id("warnConnectionClosed") <<
                               log::Session(sessionIdStr) <<
                               "The connection to " << describe() << " was closed");
                } else {
                    ARRAS_ERROR(log::Id("peerExceptionReceive") <<
                                log::Session(sessionIdStr) <<
                                "PeerException (code " << (int)code << "while receiving message from " <<
                                describe() << ": "  << std::string(e.what()));
                }
                shouldDisconnect = true;
            } catch (const std::exception& e) {
                ARRAS_ERROR(log::Id("receiveException") <<
                            log::Session(sessionIdStr) <<
                            "Exception while receiving message from " <<
                            describe() << ": "  << std::string(e.what()));
                shouldDisconnect = true;
            } catch (...) {
                ARRAS_ERROR(log::Id("receiveException") <<
                            log::Session(sessionIdStr) <<
                            "Unknown exception while receiving message from " << describe());
                shouldDisconnect = true;
            }

            if (shouldDisconnect) {
                disconnect();
                return; // exit thread
            }
        }
   }
}

RemoteEndpoint::RemoteEndpoint(
    Peer* aPeer,
    const PeerManager::PeerType aType,
    const UUID& aUuid,
    const UUID& aSessionId,
    ThreadedNodeRouter& aThreadedNodeRouter,
    const std::string& traceInfo) :
    mPeerType(aType),
    mUUID(aUuid),
    mShutdown(false),
    mFlaggedForDestruction(false), 
    mTraceInfo(traceInfo),
    mThreadedNodeRouter(aThreadedNodeRouter),
    mSessionId(aSessionId)
{
    // for now, only supporting SocketPeer derivatives of Peer
    SocketPeer* p = dynamic_cast<SocketPeer*>(aPeer);
    if (p == nullptr) {
        throw impl::InternalError("Instance of Peer must be of type SocketPeer");
    }

    setPeerInternal(p); // no other threads exist yet so no need to lock or notify

    if (aType == PeerManager::PEER_IPC) {
        initStatsTime();
    }
        
    // If mSessionId is valid then it must exist in the table but if it is invalid
    // than simply don't accept incoming messages, only send them
    if (mSessionId.valid()) {
       mRoutingData = mThreadedNodeRouter.sessionRoutingData(mSessionId);
       if (!mRoutingData) {
           throw impl::InternalError("Missing routing data in RemoteEndpoint");
       }
    }
    
    std::string queueName = PeerManager::peerTypeName(mPeerType) + " Endpoint["+mUUID.toString() +"]";
    mMessageQueue = std::unique_ptr<impl::MessageQueue>(new impl::MessageQueue(queueName));
    set_thread_stacksize(KB_256);
    // mRoutingData will never be used for PEER_NODE connections
    if (mRoutingData || (aType == PeerManager::PEER_NODE) || (aType == PeerManager::PEER_SERVICE)) {
        mReceiveThread = std::thread(&RemoteEndpoint::receiveThread, this);
    }
    mSendThread = std::thread(&RemoteEndpoint::sendThread, this);
    set_thread_stacksize(0);
}

RemoteEndpoint::RemoteEndpoint(
    const PeerManager::PeerType aType,
    const UUID& aUuid,
    const SessionNodeMap::NodeInfo& aNodeInfo,
    ThreadedNodeRouter& aThreadedNodeRouter,
    const std::string& traceInfo) :
    mNodeInfo(aNodeInfo),
    mPeerType(aType),
    mUUID(aUuid),
    mShutdown(false),
    mFlaggedForDestruction(false), 
    mTraceInfo(traceInfo),
    mThreadedNodeRouter(aThreadedNodeRouter)
{
    if (aType == PeerManager::PEER_IPC) {
        initStatsTime();
    }

    std::string queueName = PeerManager::peerTypeName(mPeerType) +" RemoteEndpoint["+mUUID.toString() +"]";
    mMessageQueue = std::unique_ptr<impl::MessageQueue>(new impl::MessageQueue(queueName));
    set_thread_stacksize(KB_256);
    mReceiveThread = std::thread(&RemoteEndpoint::receiveThread, this);
    mSendThread = std::thread(&RemoteEndpoint::sendThreadWithConnect, this);
    set_thread_stacksize(0);
}

RemoteEndpoint*
RemoteEndpoint::createNodeRemoteEndpoint(
    const UUID& aUUID,
    const SessionNodeMap::NodeInfo& aNodeInfo,
    ThreadedNodeRouter& aThreadedNodeRouter,
    const std::string& traceInfo)
{
    RemoteEndpoint* endpoint = new RemoteEndpoint(PeerManager::PEER_NODE, aUUID, 
                                                  aNodeInfo, aThreadedNodeRouter,traceInfo);
    return endpoint;
}



RemoteEndpoint::~RemoteEndpoint()
{
    // close down the send and receive threads
    mShutdown = true;

    {
        // make sure no-one is still waiting for the peer to be set
        std::lock_guard<std::mutex> lock(mPeerSetMutex);
        mPeerSetCondition.notify_all();
    }
 
    // shutdown the socket so the thread can't be hung in the a read, write, or the queue pop_front
    if (mPeer != nullptr) mPeer->threadSafeShutdown();
    mMessageQueue->shutdown();

    if (mSendThread.joinable()) mSendThread.join();
    if (mReceiveThread.joinable()) mReceiveThread.join();

    delete mPeer;
    delete mMessageEndpoint;
}

void
RemoteEndpoint::receiveEnvelope()
{
    // message is read as OpaqueContent to avoid deserialization cost
    mLastEnvelope = mMessageEndpoint->getEnvelope();

    // these three message types are handled directly by RemoteEndpoint,
    // and must always be fully deserialized (see onEndpointActivity)
    if ((mLastEnvelope.classId() == impl::ControlMessage::ID) || 
        (mLastEnvelope.classId() == impl::ExecutorHeartbeat::ID) ||
        (mLastEnvelope.classId() == impl::PongMessage::ID) ||
        (mPeerType == PeerManager::PEER_SERVICE)) {
        MessageReader::deserializeContent(mLastEnvelope);
    }

    // give subclass (specifically ClientRemoteEndpoint)
    // the chance to address the incoming envelope
    addressReceivedEnvelope();
}

void
RemoteEndpoint::disposeEnvelope()
{ 
    // allows memory to be freed, if data is not in use elsewhere
    mLastEnvelope.clear();
}

void
RemoteEndpoint::queueEnvelope(const Envelope& anEnvelope)
{
    try {
        mMessageQueue->push(anEnvelope);
    } catch (const impl::ShutdownException&) {
        // if queue has been shutdown, if means this RemoteEndpoint
        // is closing : simply fail to deliver the message
        ARRAS_DEBUG("Message undelivered due to endpoint shutdown: " << anEnvelope.describe());
    }
}

void
RemoteEndpoint::queueEnvelope(const Envelope& anEnvelope,
                             const api::AddressList& aTo)
{
    Envelope env(anEnvelope);
    env.to() = aTo;
    queueEnvelope(env);
}

void
RemoteEndpoint::sendEnvelope(const Envelope& envelope)
{
    mMessageEndpoint->putEnvelope(envelope);
}

void
RemoteEndpoint::clear()
{
    mLastEnvelope.clear();
}

bool
RemoteEndpoint::drain(const std::chrono::milliseconds& timeout)
{
    return !mMessageQueue->waitUntilEmpty(std::chrono::duration_cast<std::chrono::microseconds>(timeout));
}

void
RemoteEndpoint::close()
{
    mPeer->shutdown();
}

void
RemoteEndpoint::setPeerInternal(SocketPeer* aPeer)
{
    // can't create the message endpoint until the peer is set, so
    // do it here. This must be called in a threadsafe context
    if (aPeer != mPeer) {
        mPeer = aPeer;
        delete mMessageEndpoint;
        mMessageEndpoint = new PeerMessageEndpoint(*aPeer,false,mTraceInfo);
    }
}

void
RemoteEndpoint::setPeer(Peer* aPeer)
{
    // if this is happening then it is expected a node to node connection is
    // being negotiated. If we have a lower valued node UUID we will create
    // an initial connection only long enough to notify the higher valued
    // UUID that we want to connect. Once the higher values UUID mode connects
    // back, we don't really care whether that message makes it through since
    // we have the permanent connection.
    if (mPeerType == PeerManager::PEER_NODE) {
        if (mPeer) {
            ARRAS_ERROR(log::Id("badSetPeer") <<
                        "RemoteEndpoint::setPeer: unexpected non-null mPeer");
            delete mPeer;
        }
        ARRAS_ERROR(log::Id("badSetPeer") << "RemoteEndpoint::setPeer: setting mPeer");
        std::lock_guard<std::mutex> lock(mPeerSetMutex);
        setPeerInternal(dynamic_cast<SocketPeer*>(aPeer));
        mPeerSetCondition.notify_all();
    }
}

int
RemoteEndpoint::fd()
{
    return mPeer->fd();
}

void 
RemoteEndpoint::initStatsTime()
{
    // set the initial stats time to now + (0-31) seconds,
    // to spread out stats among the computations
    struct timeval now;
    gettimeofday(&now, nullptr);
    // we already have a good-enough random number
    // in the form of the computation UUID, so we
    // can avoid calling a RNG by xor-ing the bytes 
    // together and using mod 32
    // we already rely heavily elsewhere 
    // on UUID being just a 16-byte array...
    const unsigned char* uuidBytes = reinterpret_cast<const unsigned char*>(&mUUID);
    unsigned char hash = 0;
    for (unsigned i = 0; i < 16; i++)
        hash ^= uuidBytes[i];
    mNextStatsTime = now.tv_sec + (hash & 0x1f);
}

void 
RemoteEndpoint::sendStats(const impl::ExecutorHeartbeat::ConstPtr& aHeartbeat)
{ 
    // send stats taken from the heartbeat message to the stats log,
    // if it's time to do so
    if ((aHeartbeat->mTransmitSecs < mNextStatsTime) ||
        (mNextStatsTime == 0)) {
        return;
    }

    log::AthenaLogger* logger = dynamic_cast<log::AthenaLogger*>(&log::Logger::instance());
    if (!logger) {
        ARRAS_WARN(log::Id("warnNotAthena") <<
                   "Default logger is not an AthenaLogger : cannot log stats");
        mNextStatsTime = 0;
        return;
    }

    // format transmit time
    time_t t = (time_t)aHeartbeat->mTransmitSecs;
    tm* date = localtime(&t);
    std::string transmitTime = boost::str(boost::format("%04d-%02d-%02dT%02d:%02d:%02d")
                                          % (date->tm_year+1900) % date->tm_mon % date->tm_mday
                                          % date->tm_hour % date->tm_min % date->tm_sec);

    // send heartbeat stats out to the stats logger, and reset the next time
    Object stats;

    stats["type"] = "ArrasComputationStats/0.0";
    stats["session"] = mSessionId.toString();
    stats["computation"] = mUUID.toString();
    stats["time"] = transmitTime;
    stats["threads"] = aHeartbeat->mThreads;
    stats["hyperthreaded"] = aHeartbeat->mHyperthreaded;
    stats["CpuUsage5Sec"] = aHeartbeat->mCpuUsage5SecsCurrent;
    stats["CpuUsage60Sec"] = aHeartbeat->mCpuUsage60SecsCurrent;
    stats["CpuUsageTotal"] = aHeartbeat->mCpuUsageTotalSecs;
    // jsoncpp needs us to specify int or long long... 
    stats["SentMessages5Sec"] = (unsigned)aHeartbeat->mSentMessages5Sec;
    stats["SentMessages60Sec"] = (unsigned)aHeartbeat->mSentMessages60Sec;
    stats["SentMessagesTotal"] = (unsigned)aHeartbeat->mSentMessagesTotal;
    stats["ReceivedMessages5Sec"] = (unsigned)aHeartbeat->mReceivedMessages5Sec;
    stats["ReceivedMessages60Sec"] = (unsigned)aHeartbeat->mReceivedMessages60Sec;
    stats["ReceivedMessagesTotal"] = (unsigned)aHeartbeat->mReceivedMessagesTotal;
    stats["MemoryUsageBytes"] = (unsigned)aHeartbeat->mMemoryUsageBytesCurrent;
    stats["Status"] = aHeartbeat->mStatus;
    
    std::string statsStr = api::objectToString(stats);
    logger->logStats(statsStr);
    ARRAS_DEBUG(log::Session(mSessionId.toString()) <<
                "Sent stats to athena for computation " << mUUID.toString());
  
    struct timeval now;
    gettimeofday(&now, nullptr);
    mNextStatsTime = now.tv_sec + SEND_STATS_INTERVAL_SECS;
}

} 
}


