// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ClientConnectionStatusMessage.h"

namespace arras4 {
namespace node {

ARRAS_CONTENT_IMPL(ClientConnectionStatus);

void 
ClientConnectionStatus::serialize(api::DataOutStream& to) const
{
    to << mSessionId;
    to << mReason;
    to << mSessionStatus;
}

void
ClientConnectionStatus::deserialize(api::DataInStream& from, unsigned)
{
    from >> mSessionId;
    from >> mReason;
    from >> mSessionStatus;
}

}
}

