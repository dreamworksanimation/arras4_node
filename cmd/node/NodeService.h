// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_NODESERVICE_H__
#define __ARRAS4_NODESERVICE_H__

#include "UrlRouter.h"
#include "BanList.h"

#include <shared_impl/ThreadsafeQueue.h>

#include <httpserver/HttpServer.h>
#include <session/EventHandler.h>

#include <atomic>

using HSReq = arras4::network:: HttpServerRequest;
using HSResp = arras4::network:: HttpServerResponse;

using namespace arras4::network;

namespace arras4 {
    namespace node {

class ArrasNode;
class ArrasSessions;

// used to hold events on a queue so that
// they can be sent to Coordinator asynchronously
class EventObj
{
public:
    EventObj() {}
    EventObj(const api::UUID& aSessionId,
	     const api::UUID& aCompId,
	     api::ObjectConstRef aData) :
    sessionId(aSessionId), compId(aCompId), data(aData) 
    {}
    api::UUID sessionId;
    api::UUID compId;
    api::Object data;
};

class NodeService : public EventHandler
{
public:
    // mHttpPort will be initialized with the actual port.
    NodeService(unsigned numServerThreads,
		bool useBanList,
		const std::string& coordinatorBaseUrl,
                ArrasNode& node,
                ArrasSessions& sessions);
    ~NodeService();

    // EventHandler
    void handleEvent(const api::UUID& sessionId,
		     const api::UUID& compId,
		     api::ObjectConstRef eventData) override;
  
    void drainEvents(const std::chrono::microseconds& timeout);

    bool registerNode(api::ObjectConstRef nodeInfo);
    bool deregisterNode(const api::UUID& nodeId);

    unsigned httpPort() const { return mHttpPort; }

private:

    unsigned mHttpPort;
    network::HttpServer mHttpServer;
    std::string mCoordinatorBaseUrl;

    ArrasNode &mNode;
    ArrasSessions& mSessions;

    BanList   mBanList;

    UrlRouter mGetRouter;
    UrlRouter mPutRouter;   
    UrlRouter mPostRouter;
    UrlRouter mDeleteRouter;

    void GET_unhandled(const HSReq &req, HSResp &resp);
    void GET_health(const HSReq &req, HSResp &resp); 
    void GET_status(const HSReq &req, HSResp &resp);
    void GET_sessions(const HSReq &req, HSResp &resp);
    void GET_sessionStatus(const HSReq &req, HSResp &resp,
                           const std::string& sessionIdStr);
    void GET_sessionPerformance(const HSReq &req, HSResp &resp,
				const std::string& sessionIdStr);
    
    void PUT_unhandled(const HSReq &req, HSResp &resp);
    void PUT_sessionStatus(const HSReq &req, HSResp &resp,
                           const std::string& sessionIdStr);

    void PUT_registration(const HSReq &req, HSResp &resp);
    void PUT_status(const HSReq &req, HSResp &resp);
    void PUT_tags(const HSReq &req, HSResp &resp);
    void PUT_sessionsModify(const HSReq &req, HSResp &resp);
 
    void POST_unhandled(const HSReq &req, HSResp &resp);
    void POST_sessions(const HSReq &req, HSResp &resp);

    void DELETE_unhandled(const HSReq &req, HSResp &resp);
    void DELETE_session(const HSReq &req, HSResp &resp,
                        const std::string& sessionIdStr);
    void DELETE_tag(const HSReq &req, HSResp &resp,
                    const std::string& tagType);
    void DELETE_tags(const HSReq &req, HSResp &resp);

  
    
    // Outgoing notifications are queued and
    // then sent to Coordinator on a background thread
    impl::ThreadsafeQueue<EventObj> mEventQueue;
    std::atomic<bool> mRunThreads{true};
    std::thread mSendEventsThread;
    void sendEventsProc();
    void sendEvent(const EventObj& event);
    void notifyTerminated(const api::UUID& sessionId,
			  const api::UUID& compId,
			  api::ObjectConstRef eventData);
    void notifyReady(const api::UUID& sessionId,
		     const api::UUID& compId);
    void notifyTerminateSession(const api::UUID& sessionId,
				  api::ObjectConstRef eventData);
    // arras node to de-register with coordinator and orderly shutdown
    void notifyShutdownWithError(api::ObjectConstRef eventData);
};

}
}
#endif
