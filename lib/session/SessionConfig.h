// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_SESSION_CONFIG_H__
#define __ARRAS4_SESSION_CONFIG_H__

#include <message_api/Object.h>
#include <message_api/UUID.h>

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#include <string>
#include <memory>

namespace arras4 {
    namespace node {

// SessionConfig specifies a configuration for a session. It contains
// the data sent by Coordinator as part of a session creation (POST)
// or session modification (PUT) request
class SessionConfig
{
public:
    // pass the full description object received from Coordinator,
    // together with the current node id. Doesn't fully check desc for
    // correctness, but will throw std::runtime_error if pieces it needs to
    // parse are missing
    SessionConfig(api::ObjectConstRef desc,
                  const api::UUID& nodeId);

    using Ptr = std::shared_ptr<SessionConfig>;

    const api::UUID& sessionId() const { return mSessionId; }
    const api::UUID& nodeId() const { return mNodeId; }
    bool isThisEntryNode() const { return mThisIsEntryNode; }

    const std::map<api::UUID,std::string>& getComputations() const;
    api::ObjectConstRef getDefinition(const std::string& name) const;
    api::ObjectConstRef getRouting() const;
    api::ObjectConstRef getResponse() const;
    api::ObjectConstRef getContext(const std::string& name) const;

    int logLevel() const { return mLogLevel; }

private:
    api::UUID mSessionId;
    api::UUID mNodeId;

    // full description object
    api::Object mDesc;

    // session-wide log level (-1 means "not set")
    int mLogLevel = -1;

    // references to internal sections;
    const api::Object * mDefinitions;
    const api::Object * mRouting;
    const api::Object * mContexts;

    // response object
    api::Object mResponse;

    bool mThisIsEntryNode{false};
    // map of computations on this node
    // compId -> name
    std::map<api::UUID,std::string> mComputations;
};

}
}
#endif
