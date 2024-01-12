// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_ROUTINGTABLE_H__
#define __ARRAS_ROUTINGTABLE_H__

#include "SessionRoutingData.h"
#include "SessionNodeMap.h"

#include <message_api/messageapi_types.h>

#include <mutex>

#define AUTO_LOCK(m) std::lock_guard<std::mutex> __LOCK(m)
#define AUTO_RLOCK(rm) std::lock_guard<std::recursive_mutex> __LOCK(rm)

// RoutingTable provides thread-safe access to session routing information.
// When a SessionRoutingData is initially added it is held by a shared_ptr so that
// it can be the only copy. After the session is set up
// releaseSessionRoutingData is called so that is becomes a weak_ptr so that
// the data can go away once all users of the object are destroyed
// 
// The weak_ptrs will end up hanging around until something tries to access
// it after the object is destroyed but this seems harmless unless node runs
// for weeks without restarting.
//
// Note that each SessionRoutingData has its own SessionNodeMap, mapping nodeIds
// to network host information. These may overlap, if multiple sessions share
// a node. The code assumes the sessions are consistent, and can look up
// node info for a given node id by using the first session it finds that
// uses that node

namespace arras4 {
    namespace node {

        // all methods in RoutingTable are thread-safe
        class RoutingTable
        {
        public:
            RoutingTable();
            ~RoutingTable();

            SessionRoutingData::Ptr sessionRoutingData(const api::UUID& aSessionId) const;
            void addSessionRoutingData(const api::UUID& aSessionId,
                                       SessionRoutingData::Ptr data);
            void releaseSessionRoutingData(const api::UUID& aSessionId);
            void deleteSessionRoutingData(const api::UUID& aSessionId);
            // look through all the sessions for the first that has info on the given node
            // return false if not found.
            bool findNodeInfo(const api::UUID& aNodeId, /*out*/SessionNodeMap::NodeInfo& info) const;

        private:
            typedef std::map<api::UUID /* session ID */, SessionRoutingData::WeakPtr> RouteTableWeak;
            typedef std::map<api::UUID /* session ID */, SessionRoutingData::Ptr> RouteTable;
            RouteTableWeak mRoutingDataWeak;
            RouteTable mRoutingData;

            // thread-safety
            mutable std::mutex mMutex;
        };

} 
} 

#endif // __ARRAS_ROUTINGTABLE_H__


