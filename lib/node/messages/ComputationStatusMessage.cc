// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ComputationStatusMessage.h"

namespace arras4 {
namespace node {

ARRAS_CONTENT_IMPL(ComputationStatusMessage);

void 
ComputationStatusMessage::serialize(api::DataOutStream& to) const
{
    to << mSessionId;
    to << mComputationId;
    to << mStatus;
}

void
ComputationStatusMessage::deserialize(api::DataInStream& from, unsigned)
{
    from >> mSessionId;
    from >> mComputationId;
    from >> mStatus;
}

}
}

