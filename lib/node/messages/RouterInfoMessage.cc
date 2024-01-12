// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "RouterInfoMessage.h"

namespace arras4 {
namespace node {

ARRAS_CONTENT_IMPL(RouterInfoMessage);

void 
RouterInfoMessage::serialize(api::DataOutStream& to) const
{
    to << mMessagePort;
}

void
RouterInfoMessage::deserialize(api::DataInStream& from, unsigned)
{
    from >> mMessagePort;
}

}
}

