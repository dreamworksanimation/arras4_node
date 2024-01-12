// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

// this needs to be included with main program to force it to be first so it interposes
#include "pthread_create_interposer.inc"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <arras4_athena/AthenaLogger.h>


#include <boost/program_options.hpp>
#include <message_api/UUID.h>
#include <network/InetSocketPeer.h>
#include <network/IPCSocketPeer.h>
#include <node/router/NodeRouterManage.h>
#include <node/router/NodeRouter.h>
#include <signal.h>
#include <unistd.h>
#include <sys/resource.h>

#if not defined(DONT_USE_CRASH_REPORTER)
#include <arras4_crash/CrashReporter.h>
#endif

namespace bpo = boost::program_options;

namespace {

arras4::node::NodeRouter* router = nullptr;

void
handle(int /*aSignal*/, siginfo_t* /*aSigInfo*/, void* /*aCtx*/)
{
    // std::cerr << "Caught signal " << aSignal << std::endl;
    if (router != nullptr) {
        arras4::node::requestRouterShutdown(router);
    }
}

} // end anonymous namespace

void
parseCmdLine(int argc, char* argv[],
             bpo::options_description& flags, 
             bpo::variables_map& cmdOpts)
{
    flags.add_options()
        ("help", "Display command line options")
        ("log-level,l", bpo::value<unsigned short>()->default_value(arras4::log::Logger::LOG_INFO), 
                                                      "Log level [0-5] with 5 being the highest")
        ("nodeid", bpo::value<std::string>()->required(), "The node id for the node")
        ("inetPort", bpo::value<unsigned short>()->default_value(0), "The socket to listen on for tcp connections")
        ("ipcName", bpo::value<std::string>()->required(), "The socket to listen on for ipc connections")
	("athena-env",bpo::value<std::string>()->default_value("prod"), 
	 "Athena logging env, currently only prod and dev are supported.")
	("athena-host", bpo::value<std::string>()->default_value("localhost"), 
	 "Hostname of the Athena logging server (or localhost and let the local syslog daemon forward).")
	("athena-port", bpo::value<int>()->default_value(514), "Athena logging UDP port.")
        ;

    bpo::store(bpo::command_line_parser(argc, argv).
               options(flags).run(), cmdOpts);
    bpo::notify(cmdOpts);
}

void initLogging(const bpo::variables_map& cmdOpts)
{
    arras4::log::AthenaLogger& logger = arras4::log::AthenaLogger::createDefault(
	"node",
	true, 
	cmdOpts["athena-env"].as<std::string>(),
	cmdOpts["athena-host"].as<std::string>(), 
	static_cast<unsigned short>(cmdOpts["athena-port"].as<int>()));
    logger.setThreshold(static_cast<arras4::log::Logger::Level>(cmdOpts["log-level"].as<unsigned short>()));
    logger.setThreadName("router-main");
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
    action.sa_sigaction = handle;
    sigaction(SIGINT, &action, &oldAction);
    sigaction(SIGTERM, &action, &oldAction);

    bpo::options_description flags;
    bpo::variables_map cmdOpts;

    try {
        parseCmdLine(argc, argv, flags, cmdOpts);
    } catch(std::exception& e) {
        std::cerr << "error: " << e.what() << std::endl;
        return 1;
    } catch(...) {
        std::cerr << "Exception of unknown type!" << std::endl;;
        return 1;
    }

    if (cmdOpts.count("help")) {
        std::cout << flags << std::endl;
        return 0;
    }

    //initLogging(cmdOpts);

    arras4::api::UUID nodeId = arras4::api::UUID(cmdOpts["nodeid"].as<std::string>()); 
    unsigned short inetPort = cmdOpts["inetPort"].as<unsigned short>();
    const std::string ipcName = cmdOpts["ipcName"].as<std::string>();

    unlink(ipcName.c_str());
    arras4::network::IPCSocketPeer* ipcPeer = new arras4::network::IPCSocketPeer();
    ipcPeer->listen(ipcName);
    unsigned short ipcSocket = static_cast<unsigned short>(ipcPeer->fd());

    arras4::network::InetSocketPeer* inetPeer = new arras4::network::InetSocketPeer();
    inetPeer->listen(inetPort);
    unsigned short inetSocket = static_cast<unsigned short>(inetPeer->fd());
    unsigned short aListenPort = inetPeer->localPort();
    ARRAS_INFO("Router listening on port " << aListenPort);

// turn off warnings for static assignments.
#pragma warning(push)
#pragma warning(disable: 1711)

    // this static assignment is safe because it is done during initialization
    router = arras4::node::createNodeRouter(nodeId, inetSocket, ipcSocket);
    router->setInetPort(aListenPort);

// turn warnings for static assignments back on
#pragma warning(pop)

    waitForServiceDisconnected(router);

    destroyNodeRouter(router);
}

