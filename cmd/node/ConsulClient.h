// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_CONSUL_CLIENT_H__
#define __ARRAS4_CONSUL_CLIENT_H__

#include "ServiceClient.h"
#include <message_api/Object.h>
#include <http/HttpRequest.h>

#include <memory>
#include <string>
#include <vector>

namespace arras4 {
    namespace node {

class ConsulClient : public ServiceClient 
{
public:

    ConsulClient(const std::string& url) : ServiceClient(url) {}

    void deregisterCheck(const std::string& name);
    void deregisterService(const std::string& serviceId);
   
    void registerCheck(const std::string& name,
                       const std::string& serviceId,
                       const std::string& path,
                       const unsigned short intervalSecs);

    void registerService(const std::string& id,
                         const std::string& name,
                         const std::string& ipAddr,
                         const unsigned short port);

    std::string getCoordinatorUrl();

    bool updateNodeInfo(api::ObjectConstRef nodeInfoData);

};

} // end namespace node
} // end namespace arras4

#endif // __ARRAS4_CONSUL_CLIENT_H__
