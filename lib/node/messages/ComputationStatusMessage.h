// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_COMPUTATIONSTATUSMESSAGE_H__
#define __ARRAS_COMPUTATIONSTATUSMESSAGE_H__

#include <message_api/ContentMacros.h>
#include <string>

namespace arras4 {
    namespace node {

        struct ComputationStatusMessage : public api::ObjectContent
        {
            ARRAS_CONTENT_CLASS(ComputationStatusMessage, "3499f3aa-422c-4ed2-8789-53805231c8b5",0);
            ComputationStatusMessage() {}
            ~ComputationStatusMessage() {}
 
            void serialize(api::DataOutStream& to) const;
            void deserialize(api::DataInStream& from, unsigned version);

            api::UUID mSessionId;
            api::UUID mComputationId;

            std::string mStatus;
        };

    } 
} 
#endif // __ARRAS_CLIENTDISCONNECTEDMESSAGE_H__

