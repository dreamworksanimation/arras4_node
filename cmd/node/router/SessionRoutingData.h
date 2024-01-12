// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_SESSION_ROUTING_DATA_H__
#define __ARRAS_SESSION_ROUTING_DATA_H__

#include <message_api/messageapi_types.h>
#include <message_api/UUID.h>
#include <message_api/Object.h>

#include <memory>

/** SessionRoutingData holds the per-session routing information that node
 *  requires.
 *
 *   - SessionNodeMap is the host information for each node in the session,
 *   used to establish a new connection the first time a particular node
 *   needs to be contacted. It also lists which node is the entry node for
 *   the computation.
 *
 *   - Adddresser clientAddresser is present if this node is the entry
 *   node for the computation. It contains the information needed to
 *   address messages that the client sends to the entry computation.
 *
**/

namespace arras4 {
 
    namespace impl {
         class Addresser;
    }

    namespace node {

        class SessionNodeMap;

        class SessionRoutingData
        {
        public:
            SessionRoutingData(const api::UUID& aSessionId,
                               const api::UUID& aNodeId,
                               api::ObjectConstRef aRoutingData);

            ~SessionRoutingData();
       
	    // can update both the node map and client addresser
            // after construction. Available as separate functions,
            // since you might typically need two passes :
            // -- update every nodemap first
            // -- then update all addressers (client and computation)
            // see SessionNodeMap.h for more on node map update
            void updateNodeMap(api::ObjectConstRef aRoutingData);
            void updateClientAddresser(api::ObjectConstRef aRoutingData);

            const api::UUID& sessionId() const { return mSessionId; }
            const api::UUID& nodeId() const { return mNodeId; }
            const SessionNodeMap& nodeMap() const { return *mNodeMap; }
                   // always valid 

            bool isEntryNode() const { return mClientAddresser != nullptr; }
	    impl::Addresser* clientAddresser() const { return mClientAddresser; }
	           // returns nullptr if this node is not the entry node
              
            typedef std::shared_ptr<SessionRoutingData> Ptr;
            typedef std::weak_ptr<SessionRoutingData> WeakPtr;

        private:

            api::UUID mSessionId;
            api::UUID mNodeId;
            impl::Addresser* mClientAddresser; // may be null
            SessionNodeMap* mNodeMap;    // always valid
        };

    } 
} 

#endif // __ARRAS_SESSION_ROUTING_DATA_H__

