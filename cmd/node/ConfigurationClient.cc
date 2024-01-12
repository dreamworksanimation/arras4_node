// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ConfigurationClient.h"
#include "ServiceError.h"

#include <arras4_log/Logger.h>
#include <http/HttpResponse.h>


namespace {
    const std::string ARRAS_CONFIG_PATH = "serve/jose/arras/endpoints";
}

using namespace arras4::network;

namespace arras4 {
    namespace node {


 std::string ConfigurationClient::getServiceUrl(const std::string& service,
					       const std::string& environment, 
					       const std::string& datacenter)
{
    
    std::string path = ARRAS_CONFIG_PATH;
    if (!datacenter.empty()) {
       path = path + '/' + datacenter;
       if (!environment.empty()) {
           path = path + '/' + environment;
           if (!service.empty()) {
               path = path + '/' + service;
           }
       }
    }

    api::Object respData = doGet(path);

    if (!respData.isObject() ||
	!respData["url"].isString()) {
	throw ServiceError("Configuration Service request 'GET " + path + "' returned invalid response body");
    }
    
    return respData["url"].asString();
}

} // end of namespace node 
} // end of namespace arras4

