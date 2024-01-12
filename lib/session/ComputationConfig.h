// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS4_COMPUTATION_CONFIG_H__
#define __ARRAS4_COMPUTATION_CONFIG_H__

#include "ComputationDefaults.h"

#include <message_api/Object.h>
#include <message_api/UUID.h>

#include <execute/Process.h>
#include <execute/SpawnArgs.h>
#include <execute/ShellContext.h>

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

#include <string>

// ComputationConfig is used to generate spawn parameters for the
// execComp process, using the definition that Coordinator
// supplies for the computation

namespace arras4 {
    namespace node {

// some utils for reading the JSON config data
// is<T> checks if a value is convertible to type T
template<typename T> bool is(api::ObjectConstRef obj);
// getter<T> converts a value to type T
template<typename T> T getter(api::ObjectConstRef obj);
    
// generates the spawn settings for a computation process,
// given the Arras configuration data supplied by Coordinator
class ComputationConfig
{
public:

    ComputationConfig(const api::UUID& compId,
                      const api::UUID& nodeId,
                      const api::UUID& sessionId,
                      const std::string& name,
                      const ComputationDefaults& defaults);

    // call this first to get the context name from the 
    // computation definition supplied by Coordinator
    std::string fetchContextName(api::ObjectConstRef definition);

    // after retrieving the context via the name, 
    // call this next to set up the spawn arguments
    void setDefinition(api::ObjectConstRef definition,
		       api::ObjectConstRef context,
		       int sessionLogLevel);

    // modifies the spawn arguments to run the process under
    // a packaging system (i.e. rez). Generally changes program,
    // args and environment, and may run a background process
    // to resolved the rez environment
    // throws SessionError if something goes wrong
    void applyPackaging(impl::ProcessManager& procMan,
                        api::ObjectConstRef definition,
	                api::ObjectConstRef context);

    // add routing data to config. This is also sent by Coordinator
    // but is shared among all computations in a session. Call
    // after setDefinition()
    void addRouting(api::ObjectConstRef routingData);

    // write out the config file that execComp will read to access the
    // full computation config. This must be called after setupSpawn
    // and addRouting
    bool writeExecConfigFile();

    const api::UUID& id() const { return mId; }
    const api::UUID& sessionId() const { return mSessionId; }
    const std::string& name() const { return mName; }

    impl::SpawnArgs& spawnArgs() { return mSpawnArgs; }

private:

    // fetch a configuration param from JSON
    template<typename Pred,typename T> T getP(api::ObjectConstRef obj, const std::string& key,
                                             const T& def, Pred pred);
    // use is<T> as predicate (using a default arg prevents matching, not sure why...)
    template<typename T> T get(api::ObjectConstRef obj, const std::string& key,
                               const T& def);
    api::ObjectConstRef getObject(api::ObjectConstRef obj, const std::string& key);
    
    void applyNoPackaging(api::ObjectConstRef ctx);
    void applyCurrentEnvironment(api::ObjectConstRef ctx);
    void applyShellPackaging(impl::ShellType type, api::ObjectConstRef ctx);
    void applyRezPackaging(unsigned rezMajor, impl::ProcessManager& procMan, api::ObjectConstRef ctx);

    api::UUID mId;
    api::UUID mNodeId;
    api::UUID mSessionId;
    std::string mName;

    api::Object mExecConfig; // delivered to execComp in config file
    std::string mExecConfigFilePath; // path of config file

    impl::SpawnArgs mSpawnArgs;
    const ComputationDefaults& mDefaults;
};

template<> int getter<int>(api::ObjectConstRef o);
template<> unsigned getter<unsigned>(api::ObjectConstRef o);
template<> bool getter<bool>(api::ObjectConstRef o);
template<> size_t getter<size_t>(api::ObjectConstRef o);
template<> std::string getter<std::string>(api::ObjectConstRef o);

template<> bool is<int>(api::ObjectConstRef o);
template<> bool is<size_t>(api::ObjectConstRef o);
template<> bool is<bool>(api::ObjectConstRef o);
template<> bool is<std::string>(api::ObjectConstRef o);

// additional predicates
inline bool isNum(api::ObjectConstRef obj) { return obj.isNumeric(); }
inline bool isNonNeg(api::ObjectConstRef obj) { 
    return (obj.isDouble() && (obj.asDouble() >= 0.0)) || 
           (obj.isInt() && (obj.asInt() >= 0));
}

// get the value at key in obj, returning a default value (def) if key doesn't exist
// or has the wrong value type. The latter logs a warning. 
template<typename Pred,typename T>
T ComputationConfig::getP(api::ObjectConstRef obj,
                         const std::string& key,
                         const T& def,
                         Pred pred)
{
    if (obj.isMember(key)) {
        api::ObjectConstRef ret = obj[key];
        if (pred(ret)) {
            return getter<T>(ret);
        } else {
            ARRAS_WARN(log::Id("warnBadConfigVal") <<
                       log::Session(mSessionId.toString()) <<
                       "In config for " << mName << ": item " << 
                       key << " = " << api::valueToString(ret,true) <<
                       " is not valid. Using default value " << def);
        }
    }
    return def;
}
template<typename T>
T ComputationConfig::get(api::ObjectConstRef obj,
                         const std::string& key,
                         const T& def)
{
    return getP(obj,key,def,is<T>);
}

}
}
#endif
   
    
    
