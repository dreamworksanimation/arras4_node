// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SessionRoutingDataMessage.h"

namespace arras4 {
namespace node {

ARRAS_CONTENT_IMPL(SessionRoutingDataMessage);

void 
SessionRoutingDataMessage::serialize(api::DataOutStream& to) const
{
    to << static_cast<int>(mAction);
    to << mSessionId.toString();
    to << mRoutingData;
}

void
SessionRoutingDataMessage::deserialize(api::DataInStream& from, unsigned)
{
    int action;
    from >> action;
    mAction = static_cast<SessionRoutingAction>(action);
    std::string sessionId;
    from >> sessionId;
    mSessionId = api::UUID(sessionId);
    from >> mRoutingData;
}

}
}

