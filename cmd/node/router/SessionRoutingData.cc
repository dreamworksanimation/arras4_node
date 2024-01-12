// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SessionRoutingData.h"
#include "SessionNodeMap.h"

#include <routing/ComputationMap.h>
#include <routing/Addresser.h>

namespace arras4 {
namespace node {

SessionRoutingData::SessionRoutingData(const api::UUID& aSessionId,
                                       const api::UUID& aNodeId,
                                       api::ObjectConstRef aRoutingData)
    : mSessionId(aSessionId),
      mNodeId(aNodeId)
{
    mNodeMap = new SessionNodeMap(aRoutingData[aSessionId.toString()]);

    if (aNodeId == mNodeMap->getEntryNodeId()) {
	mClientAddresser = new impl::Addresser();
        updateClientAddresser(aRoutingData);
    } else {
        mClientAddresser = nullptr;
    }
}

void 
SessionRoutingData::updateNodeMap(api::ObjectConstRef aRoutingData)
{
    mNodeMap->update(aRoutingData[mSessionId.toString()]);
}

void 
SessionRoutingData::updateClientAddresser(api::ObjectConstRef aRoutingData)
{
    if (mClientAddresser == nullptr)
        return;
    api::ObjectConstRef messageFilter = aRoutingData["messageFilter"];
    impl::ComputationMap compMap(mSessionId,aRoutingData[mSessionId.toString()]["computations"]);
    mClientAddresser->update(api::UUID::null,compMap,messageFilter);
}


SessionRoutingData::~SessionRoutingData()
{
    delete mNodeMap;
    delete mClientAddresser;
}

} // namespace service
} // namespace arras

