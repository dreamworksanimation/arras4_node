// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_COMPUTATION_DEFAULTS_H__
#define __ARRAS4_COMPUTATION_DEFAULTS_H__

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#include <string>

namespace arras4 {
    namespace node {

// specifies default values for process config parameters
// generally values beginning "def" are defaults that a
// individual computation can override. Others
// are values that apply to all computations and
// may be settable in Node commandline options.
//
class ComputationDefaults
{
public:

    bool useCgroups = false;
    bool loanMemory = false;

    bool enforceMemory = false;
    unsigned defMemoryMb = 2048;
    unsigned minMemoryMb = 0;
    unsigned maxMemoryMb = 4000000;

    bool enforceCores = false;
    unsigned defCores = 0;
    unsigned minCores = 0;
    unsigned maxCores = 1024;

    bool cleanupProcessGroup = true;
    bool autoSuspend = false; // see Session.h

    bool defDisableChunking = false;
    size_t defMinChunkingSize = 0;
    size_t defChunkSize = 0;

    std::string defPackagingSystem{"rez1"};
    std::string packagePathOverride;

    bool colorLogging = true;
    int logLevel = log::Logger::LOG_INFO;
    std::string athenaEnv{"prod"};
    std::string athenaHost{"localhost"};
    int athenaPort = 514;
    std::string ipcName;

    // path to information needed by breakpad
    std::string breakpadPath;

    // not a computation default, but this
    // is the most convenient place to put it
    unsigned clientConnectionTimeoutSecs = 30;

};

}
}
#endif
