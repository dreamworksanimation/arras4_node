// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_ROUTEMESSAGE_H__
#define __ARRAS_ROUTEMESSAGE_H__

#include <message_api/messageapi_types.h>

namespace arras4 {
    namespace impl {
        class Envelope;
    }
}

namespace arras4 {
namespace node {

void
routeMessage(const impl::Envelope& aMessage,
             SessionRoutingData::Ptr aRoutingData,
             ThreadedNodeRouter& aThreadedNodeRouter);

// sends a message to the local client. Only call this if this is the entry node
// for the session
void
sendToLocalClient(const api::UUID& sessionId,
                  const impl::Envelope& aMessage,
                  ThreadedNodeRouter& aThreadedNodeRouter);

// send a message to a local computation. Only to be used if this is the
// host node for the computation
void
sendToLocalComputation(const api::UUID& sessionId,
                       const api::UUID& computationId,
                       const impl::Envelope& aMessage,
                       ThreadedNodeRouter& aThreadedNodeRouter);
} 
}

#endif // __ARRAS_ROUTEMESSAGE_H__
