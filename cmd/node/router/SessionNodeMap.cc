// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SessionNodeMap.h"

#include <exceptions/KeyError.h>
#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#if defined(JSONCPP_VERSION_MAJOR)
#define memberName name
#endif

namespace arras4 {
namespace node {

SessionNodeMap::SessionNodeMap(api::ObjectConstRef aRoutingData)
{
    api::ObjectConstRef nodes = aRoutingData["nodes"];
    ARRAS_INFO("Session node map: " << api::objectToString(nodes));
    for (api::ObjectConstIterator nodeIt = nodes.begin();
         nodeIt != nodes.end(); ++nodeIt) {
        NodeInfo info;
        info.hostname = (*nodeIt)["host"].asString();
        info.ip = (*nodeIt)["ip"].asString();
        info.port = static_cast<unsigned short>((*nodeIt)["tcp"].asInt());
   

        api::UUID nodeId(nodeIt.memberName());// memberName() is DEPRECATED in later jsoncpp versions
        info.nodeId = nodeId;
        mMap[nodeId] = info;

        api::ObjectConstRef nodeEntry = (*nodeIt)["entry"];
        if (nodeEntry.isBool() && nodeEntry.asBool()) {
            mEntryNodeId = nodeId;
        }
    }
}

// updates the node map, but only allows new nodes to be added
// mostly because nodeids are actually supposed to refer to
// a fixed machine without reuse (plus actually changing existing
// connections would be quite hard...)
// removing entries wouldn't help much either, since the connections
// may already exist...
void
SessionNodeMap::update(api::ObjectConstRef aRoutingData)
{
    std::lock_guard<std::mutex> lock(mUpdateMutex);
    api::ObjectConstRef nodes = aRoutingData["nodes"];
    for (api::ObjectConstIterator nodeIt = nodes.begin();
         nodeIt != nodes.end(); ++nodeIt) {
        api::UUID nodeId(nodeIt.memberName());// memberName() is DEPRECATED in later jsoncpp versions
        if (mMap.count(nodeId) == 0) {
            NodeInfo info;
            info.hostname = (*nodeIt)["host"].asString();
            info.ip = (*nodeIt)["ip"].asString();
            info.port = static_cast<unsigned short>((*nodeIt)["tcp"].asInt());
            info.nodeId = nodeId;
            mMap[nodeId] = info;
        }
    }
}

SessionNodeMap::~SessionNodeMap()
{
}

api::UUID
SessionNodeMap::getEntryNodeId() const
{
    // no lock needed since this cannot change
    return mEntryNodeId;
}

const SessionNodeMap::NodeInfo&
SessionNodeMap::getNodeInfo(const api::UUID& aNodeId) const
{
    std::lock_guard<std::mutex> lock(mUpdateMutex);
    auto iter = mMap.find(aNodeId);
    if (iter == mMap.end()) {
        std::string s = std::string("Failed to find node id ")+aNodeId.toString()+
            std::string(" in SessionNodeMap");
        throw impl::KeyError(s);
    }
    return iter->second;
}

bool
SessionNodeMap::findNodeInfo(const api::UUID& aNodeId, /*out*/NodeInfo& info) const
{
    std::lock_guard<std::mutex> lock(mUpdateMutex);
    auto iter = mMap.find(aNodeId);
    if (iter == mMap.end()) {
        return false;
    }

    info = iter->second;
    return true;
}

} // namespace service
} // namespace arras

