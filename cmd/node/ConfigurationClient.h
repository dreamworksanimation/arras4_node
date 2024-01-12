// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_CONFIGURATION_CLIENT_H__
#define __ARRAS4_CONFIGURATION_CLIENT_H__

#include "ServiceClient.h"
#include <string>

namespace arras4 {
namespace node {

class ConfigurationClient : public ServiceClient
{
public:

    ConfigurationClient(const std::string& url) : ServiceClient(url) {}

    // get the full url of a service endpoint. Returns
    // an empty string if the service is unknown or the request fails
    std::string getServiceUrl(const std::string& service,
			      const std::string& environment=std::string(),
			      const std::string& dataCenter=std::string());
};

} 
} 

#endif // __ARRAS4_CONFIGURATION_CLIENT_H__
