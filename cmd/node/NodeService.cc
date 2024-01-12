// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "NodeService.h"
#include "ArrasNode.h"

#include <message_api/Object.h>
#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#include <session/OperationError.h>
#include <session/ArrasSessions.h>

#include <http/http_types.h>
#include <http/HttpRequest.h>
#include <httpserver/HttpServerRequest.h>
#include <httpserver/HttpServerResponse.h>

#include <shared_impl/ThreadsafeQueue_impl.h>

#include <boost/algorithm/string/replace.hpp>

#include <thread>
#include <chrono>
namespace arras4 {
    namespace impl {

template class ThreadsafeQueue<Envelope>;


}
}
using namespace arras4::network;
using namespace std::placeholders;

namespace arras4 {
namespace node {

namespace {

const std::string UNKNOWN_EXCEPTION_THROWN("Unknown exception thrown in server");
    
const std::string USER_AGENT("Node Service");

const std::string NODE_API_VERSION("4.5");

arras4::api::Object getPayload(const HttpServerRequest &req)
{
    api::Object payload;
    try {
        std::string dataStr;
        if (req.getDataString(dataStr)) {
            api::stringToObject(dataStr,payload);
        }
    } catch (api::ObjectFormatError&) {
        // fall through with null payload object
    }
    return payload;
}

// We can't send a header value containing newlines..
std::string replaceNewlines(const std::string& s)
{
    std::string ret(s);
    boost::algorithm::replace_all(ret,"\n","\\n");
    return ret;
}

}

NodeService::NodeService(unsigned numServerThreads,
			 bool useBanlist,
			 const std::string& coordBaseUrl,
                         ArrasNode& node,
                         ArrasSessions& sessions)
    : mHttpServer(0,numServerThreads),
      mCoordinatorBaseUrl(coordBaseUrl),
      mNode(node),
      mSessions(sessions),
      mGetRouter(std::bind(&NodeService::GET_unhandled,this,_1,_2)),
      mPutRouter(std::bind(&NodeService::PUT_unhandled,this,_1,_2)),
      mPostRouter(std::bind(&NodeService::POST_unhandled,this,_1,_2)),
      mDeleteRouter(std::bind(&NodeService::DELETE_unhandled,this,_1,_2)),
      mEventQueue("send-events")
{
    mHttpPort = mHttpServer.getListenPort();
    ARRAS_INFO("NodeService listening on HTTP port " << mHttpPort);

    if (useBanlist) {
	mGetRouter.setBanList(&mBanList);
    }

    mHttpServer.GET += std::bind(&UrlRouter::handle,&mGetRouter,_1,_2);
    mHttpServer.PUT += std::bind(&UrlRouter::handle,&mPutRouter,_1,_2);
    mHttpServer.POST += std::bind(&UrlRouter::handle,&mPostRouter,_1,_2);
    mHttpServer.DELETE += std::bind(&UrlRouter::handle,&mDeleteRouter,_1,_2);

    // Can't use std::bind with the overloads of add(), because it will match against all of them...
    mGetRouter.add("node/1/health",[this](const HSReq &req, HSResp &resp) { GET_health(req,resp); });
    mGetRouter.add("node/1/status",[this](const HSReq &req, HSResp &resp) { GET_status(req,resp); });
    mGetRouter.add("node/1/sessions",[this](const HSReq &req, HSResp &resp) { GET_sessions(req,resp); });
    mGetRouter.add("node/1/sessions/*/status",[this](const HSReq &req, HSResp &resp,const std::string& s) 
                   { GET_sessionStatus(req,resp,s); });
    mGetRouter.add("node/1/sessions/*/performance",[this](const HSReq &req, HSResp &resp,const std::string& s) 
                   { GET_sessionPerformance(req,resp,s); });
    // prevent browsers getting banned for requesting "favicon.ico"
    mGetRouter.add("favicon.ico",[this](const HSReq &req, HSResp &resp) { GET_unhandled(req,resp); });

    mPutRouter.add("sessions/modify",[this](const HSReq &req, HSResp &resp) { PUT_sessionsModify(req,resp);});
    mPutRouter.add("node/1/sessions/modify",[this](const HSReq &req, HSResp &resp) { PUT_sessionsModify(req,resp);});
    mPutRouter.add("sessions/*/status",[this](const HSReq &req, HSResp &resp,const std::string& s) 
                   { PUT_sessionStatus(req,resp,s); });
    mPutRouter.add("node/1/sessions/*/status",[this](const HSReq &req, HSResp &resp,const std::string& s) 
                   { PUT_sessionStatus(req,resp,s); });
    mPutRouter.add("registration",[this](const HSReq &req, HSResp &resp) { PUT_registration(req,resp); });
    mPutRouter.add("status", [this](const HSReq &req, HSResp &resp) { PUT_status(req,resp); });
    mPutRouter.add("node/tags",[this](const HSReq &req, HSResp &resp) { PUT_tags(req,resp); });
    
    mPostRouter.add("sessions",[this](const HSReq &req, HSResp &resp) { POST_sessions(req,resp); });
    mPostRouter.add("node/1/sessions",[this](const HSReq &req, HSResp &resp) { POST_sessions(req,resp); });

    mDeleteRouter.add("sessions/*",[this](const HSReq &req, HSResp &resp,const std::string& s) 
                      { DELETE_session(req,resp,s); });
    mDeleteRouter.add("node/1/sessions/*",[this](const HSReq &req, HSResp &resp,const std::string& s) 
                      { DELETE_session(req,resp,s); });
    mDeleteRouter.add("node/tag/*",[this](const HSReq &req, HSResp &resp,const std::string& s)
                     { DELETE_tag(req,resp,s); }); 
    mDeleteRouter.add("node/tags",[this](const HSReq &req, HSResp &resp)
                     { DELETE_tags(req,resp); });

    mSendEventsThread = std::thread(&NodeService::sendEventsProc,this);
}

NodeService::~NodeService()
{
    mRunThreads = false;
    mEventQueue.shutdown();
    if (mSendEventsThread.joinable())
	mSendEventsThread.join();
}

void NodeService::GET_unhandled(const HSReq &req, HSResp &resp)
{
    std::string err("Unsupported GET endpoint: ");
    err += req.url();
    ARRAS_WARN(log::Id("unknownGETEndpoint") << err);
    resp.setResponseCode(HTTP_NOT_FOUND);
    resp.setResponseText(err);
}

void NodeService::GET_health(const HSReq &, HSResp &resp)
{
    Json::Value body;
    try {
        mNode.checkHealth();
        body["status"] = "UP";
        resp.setContentType("application/json");
        resp.setResponseCode(HTTP_OK);
        resp.write(body.toStyledString());
    } catch (OperationError& err) {
	ARRAS_ERROR(log::Id("NodeHealthCheckFailed") <<
		    "Node health check failed : " << err.what());
        body["status"] = "DOWN";
        body["info"] = err.what();
        resp.setResponseCode(err.httpCode());
        resp.setContentType("application/json");
        resp.setResponseText(body.toStyledString());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}
  
// GET status is the same as a health check, except it returns
// the session and node idle times as well
void NodeService::GET_status(const HSReq &, HSResp &resp)
{
    Json::Value body;
    try {
        mNode.checkHealth();
        body["status"] = "UP";
	mSessions.getIdleStatus(body);
	mBanList.getSummary(body);
	body["apiVersion"] = NODE_API_VERSION;
        resp.setContentType("application/json");
        resp.setResponseCode(HTTP_OK);
        resp.write(body.toStyledString());
    } catch (OperationError& err) {
	ARRAS_ERROR(log::Id("NodeHealthCheckFailed") <<
		    "Node health check failed : " << err.what());
        body["status"] = "DOWN";
        body["info"] = err.what();
        resp.setResponseCode(err.httpCode());
        resp.setContentType("application/json");
        resp.setResponseText(body.toStyledString());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

// return a list of active session ids
void NodeService::GET_sessions(const HSReq &, HSResp &resp)
{
    try {
        std::vector<api::UUID> sessionList =  mSessions.activeSessionIds();
        std::string sessions = "[ ";
        for (auto const &item : sessionList) {
            if (sessions != "[ ") sessions += ", ";
            sessions += "\"" + item.toString() + "\"";
        }
        sessions += " ]";
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write(sessions);
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

// return a JSON object describing the state of a session
void NodeService::GET_sessionStatus(const HSReq &, HSResp &resp,
                                    const std::string& sessionIdStr)
{
    try {
        api::UUID id(sessionIdStr);
        api::Object status = mSessions.getStatus(id);
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write(api::objectToString(status));
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

// return a JSON object describing the state of a session
void NodeService::GET_sessionPerformance(const HSReq &, HSResp &resp,
                                    const std::string& sessionIdStr)
{
    try {
        api::UUID id(sessionIdStr);
        api::Object status = mSessions.getPerformance(id);
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write(api::objectToString(status));
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}
void NodeService::PUT_unhandled(const HSReq &req, HSResp &resp)
{  
    std::string err("Unsupported PUT endpoint: ");
    err += req.url();
    ARRAS_WARN(log::Id("unknownPUTEndpoint") << err);
    resp.setResponseCode(HTTP_BAD_REQUEST);
    resp.setResponseText(err);
}
 
// signal a session, e.g. to start running
void NodeService::PUT_sessionStatus(const HSReq &req, HSResp &resp,
                                    const std::string& sessionIdStr)
{
    try {
        api::UUID id(sessionIdStr);
        api::Object payload = getPayload(req);
        mSessions.signalSession(id,payload);
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write("{ \"success\": \"true\"}");
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

// with payload["status"] = "unregistered", release the node
// actually the same as PUT_status, because ArrasNode::setStatus
// supports status=unregistered
void NodeService::PUT_registration(const HSReq &req, HSResp &resp)
{
    PUT_status(req,resp);
}

// used with payload["status"] = "shutdown" or "close", and
// additional explanation in other fields...
void NodeService::PUT_status(const HSReq &req, HSResp &resp)
{
    try {
        api::Object payload = getPayload(req);
        mNode.setStatus(payload); 
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write("{ \"success\": \"true\"}");
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

// update tags "exclusive_user","exclusive_team, 
// "exclusive_production", "over_subscribe"
void NodeService::PUT_tags(const HSReq &req, HSResp &resp)
{ 
     try {
        api::Object payload = getPayload(req);
        mNode.updateTags(payload);
        resp.setResponseCode(HTTP_OK);
        resp.setContentType("application/json");
	resp.write("{ \"success\": \"true\"}");
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

void NodeService::PUT_sessionsModify(const HSReq &req, HSResp &resp)
{
    try {
        api::Object payload = getPayload(req);
        api::Object reply = mSessions.modifySession(payload);
        std::string replyStr = api::objectToString(reply);
        resp.setResponseCode(HTTP_OK);
        resp.setContentType("application/json");
        resp.write(replyStr);
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

void NodeService::POST_unhandled(const HSReq &req, HSResp &resp)
{ 
    std::string err("Unsupported POST endpoint: ");
    err += req.url();
    ARRAS_WARN(log::Id("unknownPOSTEndpoint") << err);
    resp.setResponseCode(HTTP_BAD_REQUEST);
    resp.setResponseText(err);
}

void NodeService::POST_sessions(const HSReq &req, HSResp &resp)
{
    try { 
        api::Object payload = getPayload(req);
        api::Object reply = mSessions.createSession(payload);
        std::string replyStr = api::objectToString(reply);
        resp.setResponseCode(HTTP_OK);
        resp.setContentType("application/json");
        resp.write(replyStr);
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

void NodeService::DELETE_unhandled(const HSReq &req, HSResp &resp)
{ 
    std::string err("Unsupported DELETE endpoint: ");
    err += req.url();
    ARRAS_WARN(log::Id("unknownDELETEEndpoint") << err);
    resp.setResponseCode(HTTP_BAD_REQUEST);
    resp.setResponseText(err);
}

void NodeService::DELETE_session(const HSReq &req, HSResp &resp,
                                 const std::string& sessionIdStr)
{
    try { 
        api::UUID id(sessionIdStr);
        std::string reasonStr;
        req.header("X-Session-Delete-Reason",reasonStr);
	ARRAS_DEBUG("NodeService received DELETE session " << 
		    sessionIdStr << " reason: " << reasonStr);
        mSessions.deleteSession(id,reasonStr); 
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write("{ \"success\": \"true\"}");
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
    
}

void NodeService::DELETE_tag(const HSReq &, HSResp &resp,
                             const std::string& tag)
{  
    try { 
	api::Object tags(Json::arrayValue);
	tags.append(tag);
	mNode.deleteTags(tags);  
        resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write("{ \"success\": \"true\"}");
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

void NodeService::DELETE_tags(const HSReq &req, HSResp &resp)
{  
    try {	
	api::Object payload = getPayload(req);
	mNode.deleteTags(payload);
	resp.setContentType("application/json"); 
        resp.setResponseCode(HTTP_OK);
        resp.write("{ \"success\": \"true\"}");
    } catch (OperationError& err) {
        resp.setResponseCode(err.httpCode());
        resp.setResponseText(err.what());
    } catch (...) {
        resp.setResponseCode(HTTP_INTERNAL_SERVER_ERROR);
        resp.setResponseText(UNKNOWN_EXCEPTION_THROWN);
    }
}

// Register/deregister node with Coordinator
bool NodeService::registerNode(api::ObjectConstRef nodeInfo)
{
    std::string url = mCoordinatorBaseUrl + "/nodes";

    HttpRequest req(url, POST);
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);
    std::string body = api::objectToString(nodeInfo);

    std::string id = "[UNKNOWN]";
    if (nodeInfo["id"].isString()) id = nodeInfo["id"].asString();
    ARRAS_INFO("Registering Node ID " + id + " with Coordinator");
     const HttpResponse &resp = req.submit(body);
    auto responseCode = resp.responseCode();

    if (responseCode < HTTP_OK ||
	responseCode >= HTTP_MULTIPLE_CHOICES) {
	std::string responseString("[NO DATA]");
	resp.getResponseString(responseString); 
	ARRAS_ERROR(log::Id("NodeRegisterFail") <<
		    "(NodeService) Node Registration ('POST " + url +
		    "') returned unacceptable status code " +
		    std::to_string(responseCode) + "(response body: '" +
		    responseString + "')");
	return false;
    }
    return true;
}

bool NodeService::deregisterNode(const api::UUID& nodeId)
{
    std::string url = mCoordinatorBaseUrl + "/nodes/" + nodeId.toString();

    HttpRequest req(url, DELETE);
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);

    ARRAS_INFO("Deregistering Node ID " + nodeId.toString() + " from Coordinator");
    const HttpResponse &resp = req.submit();
    auto responseCode = resp.responseCode();

    if (responseCode < HTTP_OK ||
	responseCode >= HTTP_MULTIPLE_CHOICES) {
	std::string responseString("[NO DATA]");
	resp.getResponseString(responseString); 
	ARRAS_ERROR(log::Id("NodeDeregisterFail") <<
		    "(NodeService) Node Deregistration ('DELETE " + url +
		    "') returned unacceptable status code " +
		    std::to_string(responseCode) + "(response body: '" +
		    responseString + "')");
	return false;
    }
    return true;
}

// Outgoing notifications are queued so that caller doesn't
// have to wait for http reply
void NodeService::handleEvent(const api::UUID& sessionId,
			      const api::UUID& compId,
			      api::ObjectConstRef eventData)
{
    mEventQueue.push(EventObj(sessionId,compId,eventData));
}

void NodeService::drainEvents(const std::chrono::microseconds& timeout)
{
    mEventQueue.waitUntilEmpty(timeout);
}

void NodeService::sendEventsProc()
{
    log::Logger::instance().setThreadName("nodeservice-eventhandler");
    while (mRunThreads) {
	EventObj event;
        try {     
            bool popped = mEventQueue.pop(event);
            if (popped)
		sendEvent(event);
        } catch (impl::ShutdownException&) {
            // queue has been unblocked to give us a chance to exit
        } catch (std::exception& e) {
            ARRAS_ERROR(log::Id("HandleEventFail") <<
			"Error handling event in NodeService: " << e.what());
        } catch (...) {
            ARRAS_ERROR(log::Id("HandleEventFail") <<
			"Unknown error handling event in NodeService");
        }
    }
}

void NodeService::sendEvent(const EventObj& event)
{
    std::string detail;
    if (event.compId.isNull()) {
	if (!event.sessionId.isNull()) 
	    detail = " for session " + event.sessionId.toString();
    } else {
	detail = " for computation " + event.compId.toString();
    }
    

    api::ObjectConstRef eventData = event.data;
    std::string eventType;
    if (eventData["eventType"].isString()) {
	eventType = eventData["eventType"].asString();
    } else {
	ARRAS_ERROR(log::Id("BadEventType") <<
		    log::Session(event.sessionId.toString()) <<
		    "Missing 'eventType' string in event data: " <<
		    api::objectToString(eventData));
	return;
    }

    ARRAS_DEBUG("Sending event " << eventType << detail);
	
    if (eventType == "computationTerminated") {
	notifyTerminated(event.sessionId, event.compId, eventData);
    } else if (eventType == "computationReady") {
	notifyReady(event.sessionId, event.compId);
    } else if (eventType == "sessionClientDisconnected") {
	notifyTerminateSession(event.sessionId, eventData);
    } else if (eventType == "sessionOperationFailed") {
	notifyTerminateSession(event.sessionId, eventData);
    } else if (eventType == "sessionExpired") {
	notifyTerminateSession(event.sessionId, eventData);
    } else if (eventType == "shutdownWithError") {
        notifyShutdownWithError(eventData);
    } else {
	ARRAS_WARN(log::Id("UnknownEventType") <<
		   log::Session(event.sessionId.toString()) <<
		   "Unknown 'eventType' : " << eventType);
    }
}

// eventType: "computationTerminated"
// reason (optional): <string>
// maps to DELETE /sessions/<sessId>/hosts/<compId>
void NodeService::notifyTerminated(const api::UUID& sessionId,
				   const api::UUID& compId,
				   api::ObjectConstRef eventData)
{
    // workaround for ARRAS-3567 : it isn't safe to send a delete
    // too soon after session is created
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // end

    std::string url = mCoordinatorBaseUrl;
    url += "/sessions/" + sessionId.toString() + 
	"/computations/" + compId.toString();

    // Initialize the request object.
    HttpRequest req(url, DELETE);
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);

    if (eventData["reason"].isString()) {
	req.addHeader("X-Host-Delete-Reason",
		      replaceNewlines(eventData["reason"].asString()));
    }
    const HttpResponse &resp = req.submit();
    std::string respStr("None"); 
    auto responseCode = resp.responseCode();
    resp.getResponseString(respStr); 

    if (responseCode < HTTP_OK ||
        responseCode >= HTTP_MULTIPLE_CHOICES) {
	ARRAS_WARN(log::Id("BadEventResponse") <<
		   log::Session(sessionId.toString()) <<
		   "Coordinator returned unexpected response to DELETE .../hosts/: " <<
		   "code: " << responseCode << " text: " << respStr);
    }
		   	
}

// eventType: "computationReady"
// maps to PUT /sessions/<sessId>/hosts/<compId> 
// with body { "status": "ready" }
void NodeService::notifyReady(const api::UUID& sessionId,
			      const api::UUID& compId)
{
    std::string url = mCoordinatorBaseUrl;
    url += "/sessions/" + sessionId.toString() + 
	"/hosts/" + compId.toString();

    // Initialize the request object.
    HttpRequest req(url, PUT);
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);

    std::string body = "{ \"status\": \"ready\" }";
    const HttpResponse &resp = req.submit(body);
    std::string respStr("None"); 
    auto responseCode = resp.responseCode();
    resp.getResponseString(respStr); 

    if (responseCode < HTTP_OK ||
        responseCode >= HTTP_MULTIPLE_CHOICES) {
	ARRAS_WARN(log::Id("BadEventResponse") <<
		   log::Session(sessionId.toString()) <<
		   "Coordinator returned unexpected response to PUT .../hosts/: " <<
		   "code: " << responseCode << " text: " << respStr);
    }
}

// used by a number of events that cause termination of the session
// eventType: "sessionClientDisconnected"
// eventType: "sessionOperationFailed"
// eventType: "sessionExpired"
void NodeService::notifyTerminateSession(const api::UUID& sessionId,
					 api::ObjectConstRef data)
{
    // workaround for ARRAS-3567 : it isn't safe to send a delete
    // too soon after session is created
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // end

    std::string url = mCoordinatorBaseUrl;
    url += "/sessions/" + sessionId.toString();

    // Initialize the request object.
    HttpRequest req(url, DELETE);
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);
 
    // not currently used by Coordinator
    req.addHeader("X-Arras-Event-Type",
		  replaceNewlines(data["eventType"].asString()));

    if (data["reason"].isString()) {
	req.addHeader("X-Session-Delete-Reason",
		      replaceNewlines(data["reason"].asString()));
    } else {
	req.addHeader("X-Session-Delete-Reason",
		      replaceNewlines(data["eventType"].asString()));
    }

    const HttpResponse &resp = req.submit();
    std::string respStr("None"); 
    auto responseCode = resp.responseCode();
    resp.getResponseString(respStr); 
    ARRAS_DEBUG("Sent DELETE session request to Coordinator");
    if (responseCode < HTTP_OK ||
        responseCode >= HTTP_MULTIPLE_CHOICES) {
	ARRAS_WARN(log::Id("BadEventResponse") <<
		   log::Session(sessionId.toString()) <<
		   "Coordinator returned unexpected response to DELETE .../sessions/: " <<
		   "code: " << responseCode << " text: " << respStr);
    }		   	
}

// eventType: "shutdownWithError"
// reason (optional): <string>
// arras node to de-register with coordinator and orderly shutdown.
void NodeService::notifyShutdownWithError(api::ObjectConstRef eventData)
{
    if (eventData.isMember("reason") && eventData["reason"].isString()) {
        ARRAS_ERROR(log::Id("ShutdownWithError") << "reason: "
            << eventData["reason"].asString());
    }
    ARRAS_ERROR(log::Id("ShutdownWithError") << "event data: " <<
        api::objectToString(eventData));
    ARRAS_ERROR(log::Id("ShutdownWithError") << "orderly shutdown node.");
    mNode.stopRunning();
}

}
}
