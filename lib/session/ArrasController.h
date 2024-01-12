// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_ARRAS_CONTROLLER_H__
#define __ARRAS4_ARRAS_CONTROLLER_H__

#include "SessionConfig.h"

#include <execute/ProcessController.h>

#include <shared_impl/MessageDispatcher.h>
#include <shared_impl/MessageHandler.h>
#include <network/IPCSocketPeer.h>

#include <atomic>
#include <condition_variable>
#include <mutex>

namespace arras4 {

    namespace impl {
	class ProcessManager;
    }

    namespace node {

class ComputationDefaults;
class EventHandler;
class ArrasSessions;

class ArrasController : 
    public impl::MessageHandler,
    public impl::DispatcherObserver,
    public impl::ProcessController
{
public:
    ArrasController(const api::UUID& nodeId, ArrasSessions& sessions);
    ~ArrasController();

    bool startRouter(const ComputationDefaults& defaults,
	             impl::ProcessManager& processManager);

    bool connectToRouter(const ComputationDefaults& defaults);
    bool initializeSession(const SessionConfig& config);  
    void updateSession(const api::UUID& sessionId,
		       api::ObjectConstRef data);
    void shutdownSession(const api::UUID& sessionId,
			 const std::string& reason);
    void signalEngineReady(const api::UUID& sessionId);

    // Events    
    void sessionOperationFailed(const api::UUID& sessionId,
				const std::string& opname,
				const std::string& message); 
    void sessionExpired(const api::UUID& sessionId,
			const std::string& message);
    void setEventHandler(EventHandler* handler) { mEventHandler = handler; }
    void handleEvent(const api::UUID& sessionId,
			 const api::UUID& compId,
			 api::ObjectConstRef eventData);

    // DispatcherObserver
    void onDispatcherExit(impl::DispatcherExitReason reason) override;

    // MessageHandler
    void handleMessage(const api::Message& message) override;
    void onIdle() override {}
  
    // ProcessController
    bool sendStop(const api::UUID& id, const api::UUID& sessionId) override;

    bool sendControl(const api::UUID& id, const api::UUID& sessionId,
                     const std::string& command, api::ObjectConstRef data=api::Object());
    
    // doesn't return until stopRunning() is called
    void run();
    void stopRunning();

    unsigned routerInetPort() { return mRouterInetPort; }

private:
    void kickClient(const api::UUID& sessionId, const std::string& kickReason,
		   const std::string& stoppedReason);

    api::UUID mNodeId;
    ArrasSessions& mSessions;
    network::IPCSocketPeer mRouterPeer;
    impl::MessageDispatcher mDispatcher;
    EventHandler* mEventHandler;

    api::UUID mRouterProcessId;

    std::atomic<unsigned> mRouterInetPort = {0}; // init to 0

    // following data is mutex locked
    std::mutex mMutex;
    std::map<std::string, bool> mRouterHasRoutingData;
    std::condition_variable mCondition;

    // true if arras controller is exiting or shutting down
    std::atomic<bool> mExiting;
};

}
}
#endif
