// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0


#include "NodeRouter.h"
#include "NodeRouterManage.h"

#include <cstring>
#include <unistd.h>

namespace arras4 {

namespace node {

//
// these functions allow managing of the NodeRouter without having it's implementation
//
NodeRouter*
createNodeRouter(const api::UUID& aNodeId, unsigned short aInetSocket, unsigned short aIpcSocket)
{
    NodeRouter* router = new NodeRouter(aNodeId, aInetSocket, aIpcSocket);
    router->start();
    return router;
}

pid_t
forkNodeRouter(const api::UUID& aNodeId, unsigned short aInetSocket, unsigned short aIpcSocket)
{
    std::string executable= "arras4_noderouter";
    std::string nodeIdOption = std::string("--nodeid=") + aNodeId.toString();
    std::string inetSocketOption = std::string("--inet=") + std::to_string(aInetSocket);
    std::string ipcSocketOption = std::string("--ipc=") + std::to_string(aIpcSocket);

    pid_t pid = fork();

    if (pid > 0) return pid;

    if (pid < 0) {
        perror("arras4_node: While forking router");
        return pid;
    }

    std::vector<char*> args;
    args.push_back(strdup(executable.c_str()));
    args.push_back(strdup(nodeIdOption.c_str()));
    args.push_back(strdup(inetSocketOption.c_str()));
    args.push_back(strdup(ipcSocketOption.c_str()));
    args.push_back(nullptr);

    execvp(executable.c_str(), args.data());
    perror("arras4_node: While executing router");
    _exit(1);
}

void
destroyNodeRouter(NodeRouter* nodeRouter)
{
    // confirm that the service connection is shut down
    nodeRouter->mThreadedNodeRouter.waitForServiceDisconnected();

    nodeRouter->stop();
    delete nodeRouter;
}

void
requestRouterShutdown(NodeRouter* nodeRouter)
{
    nodeRouter->requestShutdown();
}

void
waitForServiceDisconnected(NodeRouter* nodeRouter)
{
    nodeRouter->mThreadedNodeRouter.waitForServiceDisconnected();
}

} // end namespace node

} // end namespace arras4

