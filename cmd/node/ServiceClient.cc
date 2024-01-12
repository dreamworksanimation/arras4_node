// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ServiceClient.h"
#include "ServiceError.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <http/HttpResponse.h>

#include <cassert>
#include <sstream>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

namespace {
    const std::string USER_AGENT = "Node Service";
}

using namespace arras4::network;

namespace arras4 {
namespace node {

ServiceClient::ServiceClient(const std::string& url) :
	mBaseUrl(url)
{}

ServiceClient::~ServiceClient()
{
}

api::Object
ServiceClient::doGet(const std::string& path,
		     const std::map<std::string,std::string>& headers,
                     int timeout)
{
    std::string url = mBaseUrl + path;
    HttpRequest req(url);
    for (const auto& sp : headers) {
	req.addHeader(sp.first,sp.second);
    }
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);

    std::string responseString("[NO DATA]");
    ARRAS_DEBUG("(ServiceClient) GET " << url);
    const HttpResponse &resp = req.submit(timeout);
    auto responseCode = resp.responseCode();
    resp.getResponseString(responseString); 
    
    if (responseCode < HTTP_OK ||
	responseCode >= HTTP_MULTIPLE_CHOICES) {
	throw ServiceError("(ServiceClient) Request 'GET " + url + 
			   "' returned unacceptable status code " +
			   std::to_string(responseCode) + 
			   "(response body: '" + responseString + "')");
    }
    api::Object responseBody;
    try {
	api::stringToObject(responseString,responseBody);
    } catch (api::ObjectFormatError&) {
	throw ServiceError("(ServiceClient) Request 'GET " + url + 
			   "' returned invalid JSON response '" +
			   responseString + "'");
    }
    return responseBody;
}
       
void ServiceClient::doPut(const std::string& path,
			  api::ObjectConstRef data, int timeout)
{
    std::string url = mBaseUrl + path;
    HttpRequest req(url,PUT);
    req.setContentType(APPLICATION_JSON);
    req.setUserAgent(USER_AGENT);

    std::string body = api::objectToString(data);

    // The following is a workaround.
    //
    // Delete Transfer-Encoding and specify Content-Length for PUT requests so
    // the request can be processed correctly. The libcurl library always sends
    // PUT requests chunked but Consul does not provide the expected 100
    // response for certain PUT requests (specifically, session create) tho
    // other PUT requests appear to work correctly.
    //
    req.addHeader("Transfer-Encoding", ""); // No Transfer-Encoding in header
    req.addHeader("Content-Length", std::to_string(body.length()));

    ARRAS_DEBUG("(ServiceClient) PUT " << url);
    const HttpResponse &resp = req.submit(body, timeout);
    auto responseCode = resp.responseCode();

    if (responseCode < HTTP_OK ||
	responseCode >= HTTP_MULTIPLE_CHOICES) {

	std::string responseString("[NO DATA]");
	resp.getResponseString(responseString); 

	throw ServiceError("(ServiceClient) Request 'PUT " + url +
			   "' returned unacceptable status code " +
			   std::to_string(responseCode) + "(response body: '" +
			   responseString + "')");
    }
}

} 
} 
