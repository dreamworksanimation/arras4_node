// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "UrlRouter.h"
#include "BanList.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#include <httpserver/HttpServerRequest.h>
#include <httpserver/HttpServerResponse.h>
#include <http/http_types.h>

#include <map>

namespace arras4 {
    namespace node {

        namespace urlrouter {

// wraps generic handler parameters, for convenience
// in passing them around
class Params
{
public:
    Params(const network::HttpServerRequest & aReq,
           network::HttpServerResponse & aResp)
        : req(aReq), resp(aResp) {}

    const network::HttpServerRequest & req; 
    network::HttpServerResponse & resp;
    StrVec variables;
};

// generic handler signature
using Handler = std::function<void (Params&)>;

// UrlRouter holds a tree of Nodes, each Node representing a path element.
// A path is handled by descending the tree for each element, then calling the
// handler on the final Node (if there is one)
class Node
{
public:
    Node() {}
    using Ptr = std::shared_ptr<Node>;

    // recursively add a split path to the tree, attaching the handler when
    // the recursion terminates
    void add(const StrVec& elements,
             size_t index,
             Handler handler)
    {
        if (index == elements.size()) {
            mHandler = handler;
        } else {
            const std::string& element = elements[index];
            if (element == "*") {
                if (!mVariable) {
                    mVariable = std::make_shared<Node>();
                }
                mVariable->add(elements,index+1,handler);
            } else {
                Ptr p = std::make_shared<Node>();
                auto res = mConstants.emplace(element,p);
                res.first->second->add(elements,index+1,handler);
            }
        }
    }

    // recursively handle a split path. First tries to match the current element against
    // a constant. If that fails, and a variable is allowed at this point, collect the 
    // variable by appending the element  to the list of variables in 'params'. Then recurse.
    // returns true if a handler was found and called, otherwise false
    bool handle(const StrVec& elements,
                size_t index,
                Params& params) const
    {
        if (index == elements.size()) {
             if (mHandler)
                 mHandler(params);
             else 
                 return false;
        } else {
            const std::string& element = elements[index];
            auto it = mConstants.find(element);
            if (it != mConstants.end()) {
                return it->second->handle(elements,index+1,params);
            } 
            else if (mVariable) {
                params.variables.push_back(element);
                return mVariable->handle(elements,index+1,params);
            }
            return false;
        }
        return true;
    }

private:
    std::map<std::string,Ptr> mConstants;
    Ptr mVariable;
    Handler mHandler;
};

// split a path into elements at the '/' character, ignoring
// leading. trailing and multiple /s
StrVec split(const std::string& path)
{
    StrVec ret;
    if (path.empty()) return ret;

    std::string::size_type pos = 0;
    while (pos != std::string::npos) {
        pos = path.find_first_not_of('/',pos);
        if (pos == std::string::npos) break;
        std::string::size_type next = path.find_first_of('/', pos); 
        ret.push_back(path.substr(pos,next-pos));
        pos = next;
    }
    return ret;
}

} // end of namespace urlrouter

using namespace urlrouter;


UrlRouter::UrlRouter(urlrouter::Fun0 unmappedHandler) : 
    mRoot(new Node()), mUnmappedHandler(unmappedHandler) 
{}
    
UrlRouter::~UrlRouter()
{
    delete mRoot;
}

// support multiple handler signatures by using lambdas to convert to the generic
// handler

void UrlRouter::add(const std::string& path, Fun0 func)
{
    mRoot->add(split(path),0,
               [func](Params& p) { 
                   func(p.req,p.resp); });
}
 
void UrlRouter::add(const std::string& path, Fun1 func)
{
    mRoot->add(split(path),0,
               [func](Params& p) { 
                   if (!p.variables.empty()) func(p.req,p.resp,p.variables[0]); 
                  else func(p.req,p.resp,"");
              });
}

void UrlRouter::add(const std::string& path, Fun2 func)
{
    mRoot->add(split(path),0,
               [func](Params& p) {
                   if (p.variables.size() > 1) func(p.req,p.resp,p.variables[0],p.variables[1]);
                   else if (!p.variables.empty()) func(p.req,p.resp,p.variables[0],"");
                   else func(p.req,p.resp,"","");
               });
}
     
void UrlRouter::add(const std::string& path, FunN func)
{
    mRoot->add(split(path),0,
               [func](Params& p) {
                   func(p.req,p.resp,p.variables);
               });
}

void UrlRouter::handle(const network::HttpServerRequest & aReq,
                       network::HttpServerResponse & aResp)
{
    std::string clientAddress;
    // check if request is banned due to excessive bad requests
    if (mBanList) {
	if (aReq.getClientAddress(clientAddress)) {
	    if (mBanList->isBanned(clientAddress)) {
		aResp.setResponseCode(network::HTTP_TOO_MANY_REQUESTS);
		return;
	    }
	} else {
	    ARRAS_WARN(log::Id("NoClientAddress") <<
		       "Unable to get client address from HTTP request: " << aReq.url());
	}
    }

    Params params(aReq,aResp);
    bool ok = mRoot->handle(split(aReq.url()),0,
                            params);
    if (!ok) {
        mUnmappedHandler(aReq,aResp);
	if (mBanList &&
	    (!clientAddress.empty()) &&
	    (aResp.responseCode() == network::HTTP_NOT_FOUND)) {
		mBanList->track(clientAddress);
	}
    }
}

}
}


