// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ArrasNode.h"
#include "ServiceError.h"
#include "PreemptionMonitor.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <message_api/UUID.h>

#include <boost/program_options.hpp>

#include <iostream>
#include <execinfo.h> // backtrace
#include <signal.h>
#include <cstring> //strsignal
#include <sys/resource.h>

#if not defined(DONT_USE_CRASH_REPORTER)
#include <arras4_crash/CrashReporter.h>
#endif

namespace {
    constexpr unsigned int MAX_TRACEBACK = 1024;
    std::unique_ptr<arras4::node::ArrasNode> node;
}


using namespace arras4::node;
using namespace arras4::api;

namespace bpo = boost::program_options;

// Used for matching environment variable keys to config options
const std::map<std::string, std::string> ENV_MAP = {
    {"ARRAS_COORDINATOR_HOST", "coordinator-host"},
    {"ARRAS_COORDINATOR_PORT", "coordinator-port"},
    {"ARRAS_CONSUL_HOST", "consul-host"},
    {"ARRAS_CONSUL_PORT", "consul-port"},
    {"ARRAS_NODE_PROFILING", "profiling"},
    {"ARRAS_LOG_LEVEL", "log-level"},
    {"DWA_FULL_ID", "farmFullId"},
    {"DWA_HOST_RU", "hostRU"},
    {"LOGNAME", "userName"},
    {"DWA_CONFIG_SERVICE", "dwa-config-service"}
};

std::string envConfigMapper(const std::string& envVar)
{
    // Empty strings indicate the env var should be ignored
    std::string mappedKey = "";
    const auto& it =  ENV_MAP.find(envVar);
    if (it != ENV_MAP.end()) {
        mappedKey = it->second;
    }

    return mappedKey;
}
void parseCmdLine(int argc, char* argv[],
                  ComputationDefaults& compDefs,
                  NodeOptions& opts)
{

    // These are options that control default settings for computations
    bpo::options_description compDefaults("Computation Settings");
    compDefaults.add_options()

	("use-color", bpo::value<bool>(&compDefs.colorLogging),
	 "Enable ANSI color output in logs (set --use-color=0 to disable)")
	("log-level,l", bpo::value<int>(&compDefs.logLevel),
	 "Log level [0-5] with 5 being the highest")
	("athena-env", bpo::value<std::string>(&compDefs.athenaEnv),
	 "Athena logging env, currently only prod and dev are supported.")
        ("athena-host", bpo::value<std::string>(&compDefs.athenaHost),
	 "Hostname of the Athena logging server (or localhost and let the local syslog daemon forward).")
        ("athena-port", bpo::value<int>(&compDefs.athenaPort),
	 "Athena logging UDP port.")
	("minimumChunkingSize", bpo::value<size_t>(&compDefs.defMinChunkingSize),
	 "Minimum message size (in bytes) to start chunking (0 to disable chunking)")
        ("chunkSize", bpo::value<size_t>(&compDefs.defChunkSize),
	 "Size (in bytes) of each message chunk")
        ("disableChunking", bpo::value<bool>(&compDefs.defDisableChunking)->default_value(false),
	 "Disable message chunking")
	("use-cgroups", bpo::value<bool>(&compDefs.useCgroups),
	 "Create a cgroup for each computation")
        ("enforce-memory", bpo::value<bool>(&compDefs.enforceMemory),
	 "Prevent computations from exceeding their memory allocation")
        ("loan-memory", bpo::value<bool>(&compDefs.loanMemory),
	 "Allow computations to use unallocated memory when available")
        ("enforce-cpu", bpo::value<bool>(&compDefs.enforceCores),
	 "Prevent computations from exceeding their cpu allocation")
	("auto-suspend", bpo::bool_switch(&compDefs.autoSuspend),
	 "Suspend computations at startup, via SIGSTOP")
	("rez-package-path-override", bpo::value<std::string>(&compDefs.packagePathOverride),
	 "Override to REZ_PACKAGES_PATH, replacing both the default path and any specified in the session definition")
        ("no-local-rez","Legacy option (ignored)")
	("client-connection-timeout",bpo::value<unsigned>(&compDefs.clientConnectionTimeoutSecs),
	 "Time (in seconds) allowed for client to connect before session expires")
;
    // These are options that control the service connections
    bpo::options_description connSettings("Connection Settings");
    connSettings.add_options()
        ("coordinator-host", bpo::value<std::string>(&opts.coordinatorHost), "Coordinator host")
        ("coordinator-port", bpo::value<unsigned>(&opts.coordinatorPort),
	 "Coordinator port (ignored unless --coordinator-host is specified)")
        ("coordinator-endpoint", bpo::value<std::string>(&opts.coordinatorEndpoint),
	 "Coordinator endpoint (ignored unless --coordinator-host is specified)")
        ("consul-host", bpo::value<std::string>(&opts.consulHost), "Consul host")
        ("consul-port", bpo::value<unsigned>(&opts.consulPort), "Consul port")
        ("env", bpo::value<std::string>(&opts.environment),"Environment to join")
        ("dc", bpo::value<std::string>(&opts.dataCenter),"Datacenter for environment")
        ("ipc-dir", bpo::value<std::string>(&opts.ipcDir),
	 "Location to create domain socket file for IPC with computations.")
	("dwa-config-service", bpo::value<std::string>(&opts.configServiceUrl))
        ("no-consul", bpo::bool_switch(&opts.noConsul),"Disable use of Consul")
;
    // These are descriptions of the resources available on this node, that are sent to
    // Coordinator
    bpo::options_description resourceInfo("Node Resource Info");
    resourceInfo.add_options()
	("exclusive-user", bpo::value<std::string>(&opts.exclusiveUser)->implicit_value("_unspecified_"),
	 "Reserves this node exclusively for use by the (implicit) current user; "
	 "or via the optional arg to a specified user")
        ("exclusive-production", bpo::value<std::string>(&opts.exclusiveProduction),
         "Reserves this node exclusively for clients which specify a matching production in their session options.")
        ("exclusive-team", bpo::value<std::string>(&opts.exclusiveTeam),
	 "Reserves this node exclusively for clients which specify a matching team in their session options.")
	("over-subscribe", bpo::bool_switch(&opts.overSubscribe),
	 "Over subscribe cores and memory, will force 'exclusive-user' flag")
	("cores", bpo::value<unsigned>(&opts.cores),
	 "Number of cores to use, if unset all cores will be used")
        ("memory", bpo::value<std::string>(&opts.memory),
	 "Memory to make available on this node, the number should end with a size specifier (k,m,g)")
        ("hostRU", bpo::value<float>(&opts.hostRU))
	("farmFullId", bpo::value<std::string>(&opts.farmFullId))
    ;

    bpo::options_description generalOpts("General Options");
    generalOpts.add_options()
        ("help", "Display command line options")
        ("set-max-fds", bpo::value<bool>(&opts.setMaxFDs),
	 "Attempt to increase the maximum file descriptors (true by default use --set-max-fds=0 to disable)")
	("max-node-memory", bpo::value<std::string>(&opts.maxNodeMemory),
	 "Max memory to use, the number should end with a size specifier (k,m,g)")
        ("num-http-server-threads,n", bpo::value<unsigned>(&opts.numHttpServerThreads))
        ("spot-monitor","Monitor AWS spot instance interruption notifications")
	("azure-monitor","Monitor Azure instance for interruption notifications")
	("profiling", bpo::bool_switch(&opts.profiling), "Enable profiling")
        ("userName", bpo::value<std::string>(&opts.userName))
	("nodeId", bpo::value<std::string>(&opts.nodeId))
	("no-banlist", bpo::bool_switch(&opts.disableBanlist), "Disable 'banning' of IP addresses that send too many bad requests")
   ;

    bpo::options_description allOpts("Node Service options");
    allOpts.add(compDefaults).add(connSettings)
	.add(resourceInfo).add(generalOpts);

    bpo::variables_map cmdOpts;
    bpo::store(bpo::parse_command_line(argc, argv, allOpts), cmdOpts);
    bpo::store(bpo::parse_environment(allOpts, envConfigMapper), cmdOpts);
    bpo::notify(cmdOpts);

    if (cmdOpts.count("help")){
        std::cout << allOpts << std::endl;
        exit(0);
    }
    if (cmdOpts.count("spot-monitor")) {
	opts.preemptionMonitorType = PreemptionMonitorType::AWS;
    }
    if (cmdOpts.count("azure-monitor")) {
	if (opts.preemptionMonitorType != PreemptionMonitorType::None) {
	    ARRAS_WARN(arras4::log::Id("NodeBadArgs") <<
		       "Ignoring 'azure-monitor' option, since 'spot-monitor' was also specified");
	} else {
	    opts.preemptionMonitorType = PreemptionMonitorType::Azure;
	}
    }
#if not defined (DONT_USE_CRASH_REPORTER)
    compDefs.breakpadPath = arras4::crash::CrashReporter::getProgramParentPath();
#endif
}



int mainloop(int argc, char* argv[])
{
    arras4::log::Logger::instance().setThreshold(arras4::log::Logger::LOG_DEBUG);
    node.reset(new ArrasNode());

    try {
        parseCmdLine(argc, argv,
		     node->computationDefaults(),
	             node->nodeOptions());
    } catch (std::exception& e) {
	ARRAS_ERROR(arras4::log::Id("NodeBadArgs") <<
		    "Error in Node command options: " << e.what());
        return -1;
    } catch(...) {
        ARRAS_ERROR(arras4::log::Id("NodeBadArgs") <<
		    "Error in Node command options (unknown exception)");
        return -1;
    }

    try {
	node->initialize();
    } catch (std::exception& e) {
	ARRAS_ERROR(arras4::log::Id("NodeInitError") <<
		    "Error during initialization of Node : " << e.what());
        return -1;
    } catch(...) {
	ARRAS_ERROR(arras4::log::Id("NodeInitError") <<
		    "Error during initialization of Node (unknown exception)");
        return -1;
    }

    std::unique_ptr<PreemptionMonitor> monitor(
	PreemptionMonitor::start(node->nodeOptions().preemptionMonitorType,
				 node.get()));

    node->run();

    monitor.reset(nullptr);
    node.reset(nullptr);

    return 0;
}

// TERM/INTR signal handler
void
termhandler(int /*aSignal*/, siginfo_t* /*aSigInfo*/, void* /*aCtx*/)
{
    if (node)
	node->stopRunning();
}

void
crashhandler(int aSignal, siginfo_t* /*aSigInfo*/, void* /*aCtx*/)
{
    void* addresses[MAX_TRACEBACK];
    std::string signame(::strsignal(aSignal));

    std::stringstream msg;
    msg << "Received signal " << signame << ". Stack trace:" << std::endl;
    int stackdepth= backtrace(addresses, MAX_TRACEBACK);
    char** trace = backtrace_symbols(addresses, stackdepth);
    if (trace != nullptr) {
        for (int i=0; i < stackdepth; i++) {
            msg << "    " << i << ": " << trace[i] << std::endl;
        }
    }
    ARRAS_ERROR(::arras4::log::Id("signalCaught_"+signame) <<
                msg.str());
    signal(aSignal, SIG_DFL);
    raise(aSignal);
}

int main(int argc, char* argv[])
{
#if not defined(DONT_USE_CRASH_REPORTER)
    // CrashReporter to generate and store stacktraces, in event of crash
    // assuming program parent directory is '<install_path>/bin'
    // Should handle these following signals:
    //   SIGSEGV(11), SIGABRT(6), SIGFPE(8), SIGILL(4), SIGBUS(7), SIGTRAP(5)
    INSTANCE_CRASH_REPORTER(argv[0]);
#endif

    // limit core dump file size to 0, use breakpad stacktrack files
    struct rlimit core_limit;
    core_limit.rlim_cur = 0;
    core_limit.rlim_max = 0;
    setrlimit(RLIMIT_CORE, &core_limit);

    struct sigaction action, oldAction;
    sigemptyset (&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = termhandler;
    sigaction(SIGINT, &action, &oldAction);
    sigaction(SIGTERM, &action, &oldAction);

    int ret;
    try {
        ret = mainloop(argc, argv);
    }  catch (std::exception& e) {
        ARRAS_FATAL("node terminating: " << e.what());
        ret = -1;
    } catch (...) {
        ARRAS_FATAL("node terminating: Caught unknown exception");
        ret = -3;
    }

    return ret;
}

