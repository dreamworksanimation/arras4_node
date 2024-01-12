// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ComputationConfig.h"
#include "SessionError.h"
#include <execute/RezContext.h>

#include <fstream>



namespace arras4 {
    namespace node {

ComputationConfig::ComputationConfig(const api::UUID& compId,
                                     const api::UUID& nodeId,
                                     const api::UUID& sessionId,
                                     const std::string& name,
                                     const ComputationDefaults& defaults) :
    mId(compId), mNodeId(nodeId), mSessionId(sessionId),
    mName(name),
    mExecConfigFilePath("/tmp/exec-" + name + "-" + compId.toString()),
    mDefaults(defaults)
{
}

// get an object by key from JSON config data. Returns an empty object if the
// key doesn't exist or value is not an object
api::ObjectConstRef ComputationConfig::getObject(api::ObjectConstRef obj,
                                                 const std::string& key)
{
    api::ObjectConstRef ret = obj[key];
    if (ret.isObject())
        return ret;
    ARRAS_WARN(log::Id("warnBadConfigVal") <<
               log::Session(mSessionId.toString()) <<
               "In config for " << mName << ": item " << 
               key << " should be an object");
    return api::emptyObject;
}

// Setup spawn args for the execComp process, using definition supplied
// by Coordinator.
//
// the definition is in the data delivered to node by coordinator, under
// <nodeid>/config/computations/<compname>
// values used by this function are:
//
// "requirements":
//     "resources":
//         "memoryMb": uint/float
//         "cores": uint/float
//     "context": string (opt)
// "workingDirectory": string
// "messaging":
//      "disableChunking":bool
//      "minChunkingSize":uint64
//      "chunkSize":uint64
// "environment":
//       varName: string
//
// logLevel is specified at both the session and computation level,
// so we pass in the session level setting, which may or may not
// be overridden by the computation.
std::string ComputationConfig::fetchContextName(api::ObjectConstRef definition)
{
    api::ObjectConstRef requirements = getObject(definition,"requirements");
    return get(requirements,"context",std::string());
}

void ComputationConfig::setDefinition(api::ObjectConstRef definition,
				      api::ObjectConstRef context,
                                      int sessionLogLevel)
{
   
    api::ObjectConstRef requirements = getObject(definition,"requirements");
    api::ObjectConstRef resources = getObject(requirements,"resources");
    api::ObjectConstRef messaging = getObject(definition,"messaging");
    api::ObjectConstRef environment = getObject(definition,"environment");

    mSpawnArgs = impl::SpawnArgs();
    mSpawnArgs.program = "execComp";

    mSpawnArgs.enforceMemory = mDefaults.enforceMemory;
    mSpawnArgs.enforceCores = mDefaults.enforceCores;

    // the next two can be specified as 
    // non-negative ints or as floats,
    // so we use a special predicate isNonNeg with get()
    mSpawnArgs.assignedMb = getP(resources,"memoryMB", mDefaults.defMemoryMb, isNonNeg);
    mSpawnArgs.assignedCores = getP(resources,"cores", mDefaults.defCores, isNonNeg);

    mSpawnArgs.workingDirectory = get(definition,std::string("workingDirectory"),std::string());
    mSpawnArgs.cleanupProcessGroup = mDefaults.cleanupProcessGroup;

    // execComp arguments...
    auto& args = mSpawnArgs.args;

    // memory and core limits are passed to execComp as arguments, as well
    // as being part of SpawnArgs
    args.push_back("--memoryMB");
    args.push_back(std::to_string(mSpawnArgs.assignedMb));
    args.push_back("--cores");
    args.push_back(std::to_string(mSpawnArgs.assignedCores));
    if (!mDefaults.colorLogging) {
        args.push_back("--use_color");
        args.push_back("0");
    }
    args.push_back("--use_affinity");
    args.push_back("0");

    // message chunking parameters are passed as arguments
    bool disableChunking = getP(messaging,"disableChunking",
                               mDefaults.defDisableChunking, isNum);
    if (disableChunking) {
        args.push_back("--disableChunking");
        args.push_back("1");
    } else {
        size_t minChunkingSize = get(resources,"minimumChunkingSize",
                                     mDefaults.defMinChunkingSize);
        unsigned chunkSize =  get(resources,"chunkSize",
                                  mDefaults.defChunkSize);
        args.push_back("--minimumChunkingSize");
        args.push_back(std::to_string(minChunkingSize));
        args.push_back("--chunkSize");
        args.push_back(std::to_string(chunkSize));
    }

    // the execComp config file contains additional configuration information
    args.push_back(mExecConfigFilePath);

    // process environment contains a few specific settings, plus any env vars
    // specified in context and definition
    impl::Environment& env = mSpawnArgs.environment;
    env.setFrom(environment);
    if (!context.isNull()) {
	api::ObjectConstRef ctxEnv = getObject(context,"environment");
	env.setFrom(ctxEnv);
    }
    env.set("ARRAS_ATHENA_ENV", mDefaults.athenaEnv);
    env.set("ARRAS_ATHENA_HOST", mDefaults.athenaHost);
    env.set("ARRAS_ATHENA_PORT", std::to_string(mDefaults.athenaPort));
    env.set("ARRAS_BREAKPAD_PATH", mDefaults.breakpadPath);
    int logLevel = get(resources, "logLevel", sessionLogLevel);

    // mExecConfig holds the contents of the execComp config file, which we
    // will write out to mConfigFilePath
    mExecConfig = api::Object();
    mExecConfig["sessionId"] = mSessionId.toString();
    mExecConfig["compId"] = mId.toString();
    mExecConfig["execId"] = mId.toString();
    mExecConfig["nodeId"] = mNodeId.toString();
    mExecConfig["ipc"] = mDefaults.ipcName;
    mExecConfig["logLevel"] = logLevel;
    mExecConfig["config"][mName] = definition;
    mExecConfig["config"][mName]["computationId"] = mId.toString();

}
    
// wrap the current configuration for the packaging system (i.e. rez)
// this generally modifies mSpawnArgs to run
//     rez-env <rezargs> -c "<originalProgram> <originalArgs>"
//
// You have to provide a ProcessManager because, in some cases,
// this function may need to run an external program to resolve
// rez packages
//
// throws SessionError if a problem occurs
void ComputationConfig::applyPackaging(impl::ProcessManager& procMan,
                                       api::ObjectConstRef definition,
                                       api::ObjectConstRef context)
{
    api::ObjectConstRef requirements = getObject(definition,"requirements");
    api::ObjectConstRef ctx = context.isNull() ? requirements : context; 

    std::string packagingSystem = get(ctx,"packaging_system",std::string());

    // "requirements" defaults to packaging system "rez1", 
    // context defaults to none
    if (context.isNull() && packagingSystem.empty())
	packagingSystem = mDefaults.defPackagingSystem;

    if (packagingSystem.empty() || packagingSystem == "none") {
	applyNoPackaging(ctx);
    }
    else if (packagingSystem == "current-environment") {
	applyCurrentEnvironment(ctx);
    }
    else if (packagingSystem == "bash") {
	applyShellPackaging(impl::ShellType::Bash,ctx);
    } else if (packagingSystem == "rez1") {
        applyRezPackaging(1,procMan,ctx);
    } else if (packagingSystem == "rez2") {
        applyRezPackaging(2,procMan,ctx);
    } else {
        ARRAS_WARN(log::Id("warnUnknownPackaging") <<
                   log::Session(mSessionId.toString()) <<
                   "In config for " << mName << ": unknown packaging system '" << 
                   packagingSystem << "'");
        throw SessionError("Unknown packaging system '" + packagingSystem + "'");
    }
}

void ComputationConfig::applyNoPackaging(api::ObjectConstRef ctx)
{
    // if no packaging is specified, we will run execComp directly
    // without a shell wrapper. To do this, we need to locate the executable
    // within the PATH in the computation environment
    std::string program = mSpawnArgs.program;
    std::string pseudoCompiler = get(ctx,"pseudo-compiler",std::string());
    if (!pseudoCompiler.empty()) {
	program += "-" + pseudoCompiler;
    }
    bool ok = mSpawnArgs.findProgramInPath(program);
    if (!ok) {
	ARRAS_ERROR(log::Id("ExecFail") <<
		    log::Session(mSessionId.toString()) <<
		    " : cannot find executable " << program << 
		    " on PATH for " << mName); 
	throw SessionError("Execution error");
    }
}

void ComputationConfig::applyCurrentEnvironment(api::ObjectConstRef ctx)
{
    mSpawnArgs.environment.setFromCurrent();
    std::string pseudoCompiler = get(ctx,"pseudo-compiler",std::string());
    if (!pseudoCompiler.empty()) {
	mSpawnArgs.program += "-" + pseudoCompiler;
    }
}

void ComputationConfig::applyShellPackaging(impl::ShellType type,api::ObjectConstRef ctx)
{
    std::string shellScript = get(ctx,"script",std::string());
    if (shellScript.empty()) {
	ARRAS_ERROR(log::Id("ShellWrapFail") <<
		    log::Session(mSessionId.toString()) <<
		    " : Must specify shell script for " << mName); 
	throw SessionError("Shell wrap error");
    }
    std::string pseudoCompiler = get(ctx,"pseudo-compiler",std::string());
    try {
	impl::ShellContext sc(type,
			      pseudoCompiler,
			      mSessionId);
        std::string err;
        bool ok = sc.setScript(shellScript,err);
        if (ok) {
            ok = sc.wrap(mSpawnArgs,mSpawnArgs);
            if (!ok) {
                ARRAS_ERROR(log::Id("ShellWrapFail") <<
                            log::Session(mSessionId.toString()) <<
			    " : Failed to wrap " << mName); 
                throw SessionError("Shell wrap error");
            } 
        } else {
            ARRAS_ERROR(log::Id("ShellSetupFail") <<
                        log::Session(mSessionId.toString()) <<
                        " : Failed to setup shell environment for " << mName 
                        << " : " << err); 
            throw SessionError("Shell wrap error" + err);
        }
    } catch (std::exception& e) {
        ARRAS_ERROR(log::Id("ShellSetupFail") <<
                    log::Session(mSessionId.toString()) <<
                    " : Failed to setup shell environment for " << mName 
                    << " : " << e.what()); 
        throw SessionError(e.what());
    }
}

void ComputationConfig::applyRezPackaging(unsigned rezMajor,
					  impl::ProcessManager& procMan,
					  api::ObjectConstRef ctx)
{
    std::string pseudoCompiler = get(ctx,"pseudo-compiler",std::string());

    // normally rez_packages_prepend defines a prefix that is added to
    // the default packages path. However, packagePathOverride can be
    // set (in ComputationDefaults) to provide the total package path, ignoring
    // both the path set in the definition and the default path.
    std::string rezPathPrefix;
    bool overridingPackagePath = !mDefaults.packagePathOverride.empty();
    if (overridingPackagePath)
	rezPathPrefix = mDefaults.packagePathOverride;
    else
	rezPathPrefix = get(ctx,"rez_packages_prepend",std::string());
    std::string rezPackages = get(ctx,"rez_packages",std::string());
    std::string rezContext = get(ctx,"rez_context",std::string());
    std::string rezContextFile = get(ctx,"rez_context_file",std::string());

    try {
	impl::RezContext rc(mName, rezMajor, rezPathPrefix, 
			    overridingPackagePath,
			    pseudoCompiler,
			    mId, mSessionId);
 
        bool ok = false;
        std::string err;
        if (!rezContext.empty()) ok = rc.setContext(rezContext,err);
        else if (!rezContextFile.empty()) ok = rc.setContextFile(rezContextFile,err);
        else if (!rezPackages.empty()) ok = rc.setPackages(procMan,rezPackages,err);
        else err = "Must specify one of 'rez_context','rez_context_file' or 'rez_packages'";
        if (ok) {
            ok = rc.wrap(mSpawnArgs,mSpawnArgs);
            if (!ok) {
                ARRAS_ERROR(log::Id("RezWrapFail") <<
                            log::Session(mSessionId.toString()) <<
                            "[ rez" << rezMajor << " ] Failed to rez wrap " << mName); 
                throw SessionError("Packaging failure");
            } 
        } else {
            ARRAS_ERROR(log::Id("RezSetupFail") <<
                        log::Session(mSessionId.toString()) <<
                        "[ rez" << rezMajor << " ] Failed to setup rez environment for " << mName 
                        << " : " << err); 
            throw SessionError("Rez error" + err);
        }
    } catch (std::exception& e) {
        ARRAS_ERROR(log::Id("RezSetupFail") <<
                    log::Session(mSessionId.toString()) <<
                    "[ rez" << rezMajor << " ] Failed to setup rez environment for " << mName 
                    << " : " << e.what()); 
        throw SessionError(e.what());
    }
}
 
// call after setupSpawn to add in routing information
void ComputationConfig::addRouting(api::ObjectConstRef routingData)
{
    // user id comes from routing data
    api::ObjectConstRef userInfo = getObject(getObject(getObject(routingData,
                                                                 mSessionId.toString()),
                                                       "clientData"),
                                             "userInfo");
    std::string userId = get(userInfo,"name",std::string(""));
    if (!userId.empty()) {
	mSpawnArgs.environment.set("USER",userId);
    }
    mExecConfig["routing"] = routingData;
}

// call after setupSpawn and addRouting to write the config file
bool ComputationConfig::writeExecConfigFile()
{
    std::ofstream ofs(mExecConfigFilePath);
    if (!ofs.fail()) {   
        try { 
            std::string s = api::objectToString(mExecConfig);
            ofs << s;
            return true;
        } catch (...) {
            // fall through to error handling
        }
    }
    ARRAS_ERROR(log::Id("configFileSaveFail") <<
                log::Session(mSessionId.toString()) <<
                "Failed to save config file: " <<
                mExecConfigFilePath);
    return false;
}
   
template<> int getter<int>(api::ObjectConstRef o)
{
    return o.asInt();
}
     
template<> unsigned getter<unsigned>(api::ObjectConstRef o) 
{
    return o.asUInt();
}

template<> bool getter<bool>(api::ObjectConstRef o)
{
    return o.asBool();
}

template<> size_t getter<size_t>(api::ObjectConstRef o)
{
    return o.asUInt64();
}

template<> std::string getter<std::string>(api::ObjectConstRef o)
{
    return o.asString();
}

template<> bool is<int>(api::ObjectConstRef o)
{
    return o.isIntegral();
}

template<> bool is<size_t>(api::ObjectConstRef o)
{
    return o.isIntegral();
}

template<> bool is<bool>(api::ObjectConstRef o)
{
    return o.isIntegral();
}

template<> bool is<std::string>(api::ObjectConstRef o)
{
    return o.isString();
}

}
}
