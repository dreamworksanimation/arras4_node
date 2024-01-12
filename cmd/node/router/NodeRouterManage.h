// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_NODEROUTERMANAGE_H__
#define __ARRAS_NODEROUTERMANAGE_H__

#include <message_api/UUID.h>
#include <string>

namespace arras4 {
namespace node {

class NodeRouter;
NodeRouter* createNodeRouter(const api::UUID& aNodeId, unsigned short aNetSocket, unsigned short aIpcSocket);
pid_t forkNodeRouter(const api::UUID& aNodeId, unsigned short aNetSocket, unsigned short aIpcSocket);
void destroyNodeRouter(NodeRouter* nodeRouter);
void requestRouterShutdown(NodeRouter* nodeRouter);
void waitForServiceDisconnected(NodeRouter* nodeRouter);

} // end namespace node
} // end namespace arras4

#endif

