// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "RoutingTable.h"
#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#define AUTO_LOCK(m) std::lock_guard<std::mutex> __LOCK(m)
#define AUTO_RLOCK(rm) std::lock_guard<std::recursive_mutex> __LOCK(rm)

namespace arras4 {
namespace node {

RoutingTable::RoutingTable()
{

}

RoutingTable::~RoutingTable()
{

}

SessionRoutingData::Ptr
RoutingTable::sessionRoutingData(const api::UUID& aSessionId)
const
{
    AUTO_LOCK(mMutex);
    const auto it = mRoutingDataWeak.find(aSessionId);

    // does the entry exist all
    if (it == mRoutingDataWeak.end()) return SessionRoutingData::Ptr();

    // lock() will return a default constructed SessionRoutingData::Ptr
    // if the object expired
    return it->second.lock();
}

//
// Initially both a weak a shared ptr are kept
// The shared_ptr is kept so that the object will stay around
// long enough for other objects to grab it
//
void
RoutingTable::addSessionRoutingData(const api::UUID& aSessionId, 
                                    SessionRoutingData::Ptr data)
{
    AUTO_LOCK(mMutex); 
    mRoutingDataWeak.insert(RouteTableWeak::value_type(aSessionId,data));
    mRoutingData.insert(RouteTable::value_type(aSessionId,data));
}

//
// Discard the shared_ptr so it can go away when all users are gone
//
void
RoutingTable::releaseSessionRoutingData(const api::UUID& aSessionId)
{
    AUTO_LOCK(mMutex);
    mRoutingData.erase(aSessionId);
}

//
// Discard the weak_ptr
//
void
RoutingTable::deleteSessionRoutingData(const api::UUID& aSessionId)
{
    AUTO_LOCK(mMutex);

    // remove it from the shared table
    mRoutingData.erase(aSessionId);

    // remove it from the 
    const auto it = mRoutingDataWeak.find(aSessionId);
    if (it != mRoutingDataWeak.end()) {
        if (!it->second.expired()) {
            ARRAS_WARN(log::Id("routingDataInUse") <<
                       log::Session(aSessionId.toString()) <<
                       "delete of SessionRoutingData when pointer still in use");
        }
        mRoutingDataWeak.erase(it);
    }
}

//
// Dig through all of the session node map objects to find NodeInfo data for
// a node UUID. Return true if the node was found.
bool
RoutingTable::findNodeInfo(const api::UUID& aNodeId, /*out*/SessionNodeMap::NodeInfo& info) const
{
    AUTO_LOCK(mMutex); 
    for (auto iter: mRoutingDataWeak) {
        SessionRoutingData::Ptr data = iter.second.lock();
        if (data != nullptr) {
            if (data->nodeMap().findNodeInfo(aNodeId, info)) return true;
        }
    }
    return false;
} 

}
} 

