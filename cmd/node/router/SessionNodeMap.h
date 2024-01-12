// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_SESSION_NODE_MAP_H__
#define __ARRAS_SESSION_NODE_MAP_H__

#include <message_api/messageapi_types.h>
#include <message_api/UUID.h>
#include <message_api/Object.h>

#include <map>
#include <memory>
#include <string>
#include <mutex>

/** SessionNodeMap records the network host information for all the nodes used
 * by a given session. This is used when initiating a connection to another node.
 *
 * Note that the node routing code relies on the fact that this will be consistent
 * across different sessions, since ThreadedNodeRouter already has a connection for 
 * a given nodeId it will be used, even if created on behalf of a different session. 
 *
 * The main reason for storing the map on a per-session basis seems to be that
 * caching and thread-safety becomes easier to manage.
 *
 * The mapping is obtained from the routing data set to node when a session
 * is initiated. It can be updated later by calling update() : this is threadsafe
 * but will not update the identity of the entry node, since this wouldn't make much 
 * sense for an existing session.
 **/
namespace arras4 {
    namespace node {

        class SessionNodeMap
        {
        public:
            SessionNodeMap(api::ObjectConstRef aRoutingData);
            ~SessionNodeMap();

            // You can update the node map once the session is
            // running, but only to add new nodes. Changing the
            // info for existing nodes isn't intended to be a
            // legitimate change : atm we just ignore it.
            // Not sure about removing nodes yet...leaving
            // them shouldn't be harmful.
            void update(api::ObjectConstRef aRoutingData);

            api::UUID getEntryNodeId() const;

            struct NodeInfo {
                api::UUID nodeId;
                std::string hostname;
                std::string ip;
                unsigned short port;
            };

            const NodeInfo& getNodeInfo(const api::UUID& aNodeId) const;
            bool findNodeInfo(const api::UUID& aNodeId, /*out*/NodeInfo& info) const;
          
            typedef std::shared_ptr<SessionNodeMap> Ptr;
            typedef std::weak_ptr<SessionNodeMap> WeakPtr;

        private:
	    mutable std::mutex mUpdateMutex;
            api::UUID mEntryNodeId;
            std::map<api::UUID /*node ID*/, NodeInfo> mMap;
        };
} 
} 

#endif // __ARRAS_SESSION_NODE_MAP_H__

