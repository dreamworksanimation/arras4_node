// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_SESSIONROUTINGDATAMESSAGE_H__
#define __ARRAS_SESSIONROUTINGDATAMESSAGE_H__

#include <message_api/ContentMacros.h>
#include <string>

namespace arras4 {
    namespace node {

	enum class SessionRoutingAction {
            Initialize,      // create routing data at session startup
            Update,          // update (modify) routing data for running session
            Delete,          // free routing data (mRoutingData unused)
            Acknowledge      // acknowledge receipt of routing data (router->service)
        };

        struct SessionRoutingDataMessage: public api::ObjectContent
        {
            ARRAS_CONTENT_CLASS(SessionRoutingDataMessage, "83ba0cb8-5af8-4ee1-8b6e-d0ca33deee41",0);
	    SessionRoutingDataMessage() : mAction(SessionRoutingAction::Initialize) {}
            SessionRoutingDataMessage(SessionRoutingAction action,
				      const api::UUID& sessId, 
				      const std::string& routing = "")
                : mAction(action), mSessionId(sessId), mRoutingData(routing) {}

            ~SessionRoutingDataMessage() {}
 
            void serialize(api::DataOutStream& to) const;
            void deserialize(api::DataInStream& from, unsigned version);

	    SessionRoutingAction mAction;
            api::UUID mSessionId;
            std::string mRoutingData;
        };
    } 
} 
#endif // __ARRAS_SESSIONROUTINGDATAMESSAGE_H__

