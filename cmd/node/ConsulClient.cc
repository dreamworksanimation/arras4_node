// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ConsulClient.h"
#include "ServiceError.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <http/HttpResponse.h>

#include <sys/stat.h> // stat for isValidScript method
#include <utility> //std::move

using namespace arras4::network;
using namespace arras4::api;

namespace arras4 {
    namespace node {

void ConsulClient::deregisterCheck(const std::string& name)
{
    ARRAS_DEBUG("(ConsulClient) De-registering check " << name);
    std::string endpoint =  "/v1/agent/check/deregister/" + name;
    doPut(endpoint, 60);
}

void ConsulClient::deregisterService(const std::string& serviceId)
{
    ARRAS_DEBUG("(ConsulClient) De-registering service " << serviceId);
    std::string endpoint = "/v1/agent/service/deregister/" + serviceId;
    doPut(endpoint, 60);
}

void
ConsulClient::registerCheck(const std::string& name,
                            const std::string& serviceId,
                            const std::string& path,                 
                            const unsigned short intervalSecs = 30)
{
    Object checkObj;
    checkObj["ID"] = name;
    checkObj["Name"] = name;
    checkObj["Interval"] = std::to_string(intervalSecs) + "s";
    // Define a timeout that's just short of the check interval.
    checkObj["Timeout"] = std::to_string(intervalSecs-1) + "s";
    checkObj["HTTP"] = path;
    checkObj["ServiceID"] = serviceId;
    checkObj["Status"] = "passing";

    ARRAS_DEBUG("(ConsulClient) Registering check: " << name);
    doPut("/v1/agent/check/register", checkObj, 60);
}

void
ConsulClient::registerService(const std::string& id,
                              const std::string& name,
                              const std::string& ipAddr,
                              const unsigned short port)
{
    // Initialize a service definition object.
    Object svcObj;
    svcObj["ID"] = id;
    svcObj["Name"] = name;
    svcObj["Address"] = ipAddr;
    svcObj["Port"] = port;

    // Submit the request.
    ARRAS_DEBUG("(ConsulClient) Registering service: " << name << ":" << id);
    doPut("/v1/agent/service/register", svcObj, 60);
}

std::string
ConsulClient::getCoordinatorUrl()
{
    std::string path = "/v1/kv/arras/services/coordinator?raw";
    api::Object respData = doGet(path, std::map<std::string, std::string>(), 60);
    
    if (!respData.isObject() ||
	!respData["ipAddress"].isString() ||
	!respData["port"].isIntegral() ||
	!respData["urlPath"].isString()) {
	throw ServiceError("Consul request 'GET " + path + "' returned invalid response body");
    }
    return "http://" + respData["ipAddress"].asString() + ":" + 
	std::to_string(respData["port"].asInt()) + respData["urlPath"].asString();
}


bool
ConsulClient::updateNodeInfo(api::ObjectConstRef nodeInfo)
{
    std::string nodeId;
    if (nodeInfo["id"].isString()) {
	nodeId = nodeInfo["id"].asString();
    } else {
	ARRAS_ERROR(log::Id("UpdateNodeInfoError") << 
		    "Cannot write nodeInfo to consul : missing 'id' field");
	return false;
    }
    const std::string& nodeInfoEndpoint = "/v1/kv/arras/services/nodes/" + nodeId + "/info";
    try {
	doPut(nodeInfoEndpoint,nodeInfo, 60);
	return true;
    } catch (std::exception& e) {
    	ARRAS_ERROR(log::Id("UpdateNodeInfoError") << 
		    "Failed to write nodeInfo to consul, for node id " <<
		    nodeId << ": " << e.what());
    }

    return false;
}

} // end namespace node
} // end namespace arras4
