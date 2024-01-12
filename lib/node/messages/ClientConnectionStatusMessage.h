// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_CLIENTCONNECTEDSTATUSMESSAGE_H__
#define __ARRAS_CLIENTCONNECTEDSTATUSMESSAGE_H__

#include <message_api/ContentMacros.h>
#include <string>

namespace arras4 {
    namespace node {

        struct ClientConnectionStatus: public api::ObjectContent
        {
            ARRAS_CONTENT_CLASS(ClientConnectionStatus, "0d66b113-49a7-4d81-bb93-925b9440ed4c",0);
            ClientConnectionStatus() {}
            ~ClientConnectionStatus() {}
 
            void serialize(api::DataOutStream& to) const;
            void deserialize(api::DataInStream& from, unsigned version);

            api::UUID mSessionId;

            // The reason client disconnected. One of
            //     clientShutdown - the client intentionally disconnected
            //     clientDroppedConnection - the client disappeared unexpectedly
            //     clientConnectionTimeout - the client didn't connect before timeout
            //     prematureMessage - a message was sent to a session before the routing information was available
            std::string mReason;

            // when coming from NodeService this will have the json session status
            std::string mSessionStatus;
        };

    } 
} 
#endif // __ARRAS_CLIENTCONNECTEDSTATUSMESSAGE_H__

