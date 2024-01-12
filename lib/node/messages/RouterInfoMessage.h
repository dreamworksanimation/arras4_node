// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_ROUTERINFOMESSAGE_H__
#define __ARRAS_ROUTERINFOMESSAGE_H__

#include <message_api/ContentMacros.h>
#include <string>

namespace arras4 {
    namespace node {

        struct RouterInfoMessage : public api::ObjectContent
        {
            ARRAS_CONTENT_CLASS(RouterInfoMessage, "4b08de9e-da0c-4cc4-a069-0d6f55d07d22",0);
            RouterInfoMessage() {}
            ~RouterInfoMessage() {}
 
            void serialize(api::DataOutStream& to) const;
            void deserialize(api::DataInStream& from, unsigned version);

            int mMessagePort = 0;
        };

    } 
} 
#endif //__ARRAS_ROUTERINFOMESSAGE_H__

