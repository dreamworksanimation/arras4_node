// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SessionConfig.h"

#if defined(JSONCPP_VERSION_MAJOR)
#define memberName name
#endif

namespace arras4 {
    namespace node {
 
SessionConfig::SessionConfig(api::ObjectConstRef desc,
                             const api::UUID& nodeId) :
    mNodeId(nodeId), mDesc(desc), mDefinitions(nullptr),
    mRouting(nullptr)
{
    // desc is the data sent by coordinator to node describing the
    // required session configuration.
    // structure is
    // <nodeid>:
    //     "config":
    //          "computations": # may only list needed definitions
    //              <compname_1>: definition for compname_1
    //              <compname_2>:...
    //          "sessionId": <sessionId>,
    //          "contexts": {
    //               <contextname>: context...
    //          }
    // "routing":
    //    <sessionId>:
    //         "nodes":...
    //         "computations": # always lists all comps
    //             <compname_1>: 
    //                   "compId": <compId>
    //                   "nodeId": <nodeId>
    //             ...
    //         "engine": ...
    //         "clientData":...

    api::ObjectConstRef nodeConfig = mDesc[nodeId.toString()]["config"];

    // object holding computation definitions by name
    api::ObjectConstRef def = nodeConfig["computations"];
    if (def.isNull() || !def.isObject()) {
        throw std::runtime_error("Session definition has no config object for this node");
    } else {
        mDefinitions = &def;
    }

    if (nodeConfig["sessionId"].isString()) {
        mSessionId = api::UUID(nodeConfig["sessionId"].asString());
    } else {
        throw std::runtime_error("Session definition has no session id");
    }
    
    // contexts
    api::ObjectConstRef ctxs = nodeConfig["contexts"];
    if (ctxs.isObject() && !ctxs.isNull()) {
	mContexts = &ctxs;
    }

    // session default log level
    if (nodeConfig["logLevel"].isIntegral()) {
        mLogLevel = nodeConfig["logLevel"].asInt();
    } else {
        mLogLevel = -1; // means "not set"
    }

    api::ObjectConstRef routing = mDesc["routing"];
    if (routing.isNull() || !routing.isObject()) {
        throw std::runtime_error("Session definition has no routing object");
    } else {
        mRouting = &routing;
    }

    // collect a list of all the computations that are on this node,
    // using the list of all computations under "routing"
    api::ObjectConstRef comps = routing[mSessionId.toString()]["computations"];
    if (comps.isNull() || !comps.isObject()) {
         throw std::runtime_error("Session definition has no computation list");
    }
    
    for (api::ObjectConstIterator cIt = comps.begin();
         cIt != comps.end(); ++cIt) {

        std::string compName = cIt.memberName();
        api::ObjectConstRef info = *cIt;

        if (!info.isObject() || 
            !info["nodeId"].isString() ||
            !info["compId"].isString())
            throw std::runtime_error("Session definition has invalid computation list");

        if (mNodeId == api::UUID(info["nodeId"].asString())) {
            api::UUID compId(info["compId"].asString());
            if (compId.isNull())
                throw std::runtime_error("Session definition has invalid entry in computation list");
            mComputations[compId] = compName;

            // ** JOSE-11811 : temporarily send back 'hostId' field as well
            //    until coordinator is updated to not need it
            mResponse[compName]["hostId"] = compId.toString();
            mResponse[compName]["compId"] = compId.toString();
            mResponse[compName]["nodeId"] = mNodeId.toString();
        }
    }
    // check if this is the entry node
    api::ObjectConstRef thisNode = routing[mSessionId.toString()]["nodes"][mNodeId.toString()];
    if (thisNode["entry"].isBool())
	mThisIsEntryNode = thisNode["entry"].asBool();
}

const std::map<api::UUID,std::string>& SessionConfig::getComputations() const
{
    return mComputations;
}
    
api::ObjectConstRef SessionConfig::getDefinition(const std::string& name) const
{
    return (*mDefinitions)[name];
}
    
api::ObjectConstRef SessionConfig::getRouting() const
{
    return *mRouting;
}
 
api::ObjectConstRef SessionConfig::getResponse() const   
{
    return mResponse;
}

api::ObjectConstRef SessionConfig::getContext(const std::string& name) const
{
    if (mContexts) return (*mContexts)[name];
    return api::emptyObject;
}
	
api::Object err(const std::string& s,
		size_t p,
		const std::string& m,
		bool &ok)
{
    ARRAS_ERROR("Error parsing '" << s << "' [pos " << p << "] : " << m);
    ok = false;
    return api::Object();
}



}
}

