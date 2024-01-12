// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_SERVICE_CLIENT_H__
#define __ARRAS_SERVICE_CLIENT_H__

#include <message_api/Object.h>
#include <http/HttpRequest.h>

#include <string>
#include <map>

namespace arras4 {
namespace node {

class ServiceClient 
{
public:
    ServiceClient(const std::string& url);
    virtual ~ServiceClient();


    api::Object doGet(const std::string& path,
	              const std::map<std::string,std::string>& headers = 
		      std::map<std::string,std::string>(),
                      int timeout=0);
    void doPut(const std::string& path,
	       api::ObjectConstRef data = api::Object(),
               int timeout=0);

private:
    std::string mBaseUrl;
};

} 
}

#endif // __ARRAS_SERVICE_CLIENT_H__
