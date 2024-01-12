// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "ArrasNode.h"
#include "NodeService.h"
#include "ConfigurationClient.h"
#include "ConsulClient.h"
#include "NodeError.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>
#include <arras4_athena/AthenaLogger.h>

#include <session/ComputationDefaults.h>
#include <session/ArrasSessions.h>
#include <session/OperationError.h>

#include <http/http_types.h>

#include <boost/filesystem.hpp>
#include <boost/program_options/environment_iterator.hpp>
#include <boost/algorithm/string.hpp>
#include <fstream>

#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <malloc.h>

#if defined(JSONCPP_VERSION_MAJOR)
#define memberName name
#endif

namespace {

    // NodeInfo client protocol bit flags
    constexpr unsigned CLIENT_PROTOCOL_BASIC = 0x1;
    // constexpr unsigned CLIENT_PROTOCOL_WEBSOCKET = 0x2; not used by Arras 4
    
    int constexpr IPC_PERMS = 0700;
    
    const char* SESSIONS_HREF_PATH = "/sessions";

    unsigned constexpr GET_COORD_RETRIES = 10000;
    unsigned constexpr GET_COORD_RETRY_SLEEP = 1; // in seconds

    const std::chrono::microseconds DRAIN_EVENTS_TIMEOUT(1000000); // 1 second


    // returns tmp directory path string, defaults to "/tmp/"
    std::string getTmpDir()
    {
        try {
            const std::string& alt_dir =  
                boost::filesystem::temp_directory_path().string() +
                boost::filesystem::path::preferred_separator;
            return alt_dir;
        } catch (const std::exception& e) {
            // error when TMPDIR, TMP, TEMP, TEMPDIR with invalid value
            return "/tmp/";   // use default
        }
    }

    unsigned long memoryFromString(const std::string& s)
    {
        if (s.empty()) return 0;
        unsigned long n = std::stol(s);
        char unit = s.back();
        if (unit == 'k' || unit == 'K') return n << 10;
        if (unit == 'm' || unit == 'M') return n << 20;
        if (unit == 'g' || unit == 'G') return n << 30;
        return n;
    }
    
    // convert hostname to a numeric IP address, if possible
    // returns hostname if not
    std::string hostToIp(const std::string& host)
    {
        addrinfo hints = {0};
        hints.ai_family = AF_INET;
        std::string ipAddress(host);
        addrinfo *infoptr = nullptr;
        int ret = getaddrinfo(host.c_str(), NULL, &hints, &infoptr);
        if (ret == 0) {
            char buffer[256] = "";
            for(addrinfo *p = infoptr; p != nullptr; p = p->ai_next) {
                if (getnameinfo(p->ai_addr, p->ai_addrlen, 
                                buffer, sizeof(buffer), nullptr, 
                                0, NI_NUMERICHOST) == 0) {
                    ipAddress = buffer;
                    break;  
                }
            }
        }
        freeaddrinfo(infoptr);
        return ipAddress;
    }

    // checks that a tags object is valid
    bool validateTags(arras4::api::ObjectConstRef tags, std::string& errorMsg)
    {
        bool ok = true;
        // exclusive_team requires exclusive_production
        if (tags["exclusive_production"].isNull() &&
            !tags["exclusive_team"].isNull()) {
            errorMsg = "Error in tag set : 'exclusive_team' requires 'exclusive_production' to be set. ";
            ok = false;
        }

        // over_subscribe is expected to be bool
        if (tags.isMember("over_subscribe") &&
            !tags["over_subscribe"].isBool()) {
            errorMsg += "Error in tag set : 'over_subscribe' should be type bool. ";
            ok = false;
        }

        // over_subscribe requires exclusive_user to be set
        if (tags["exclusive_user"].isNull()) {
            arras4::api::ObjectConstRef overSubscribe = tags["over_subscribe"];
            if (overSubscribe.isBool() && overSubscribe.asBool()) {
                errorMsg += "Error in tag set : 'over_subscribe' requires 'exclusive_user' to be set. ";
                ok = false;
            }
        }

        if (!ok) 
            ARRAS_ERROR(arras4::log::Id("InvalidTagSet") << errorMsg);

        return ok;
    }
}

namespace arras4 {
    namespace node {

ArrasNode::ArrasNode() : mRun(false)
{
}

// initialize the node
// throws NodeError and ServiceError for initialization failures
void ArrasNode::initialize()
{
    initLogging();

    // get our node id
    if (mOptions.nodeId.empty())
        mNodeId = api::UUID::generate();
    else {
        mNodeId = api::UUID(mOptions.nodeId);
        if (mNodeId.isNull()) {
            throw NodeError("nodeId argument is invalid : " + mOptions.nodeId); 
        }
    }

    ARRAS_INFO("Initializing Node ID " << mNodeId.toString());

    // use options to set memory and cores for node and computations
    calcResources();

    // create a process manager to run the computations
    unsigned compMemoryMb = static_cast<unsigned>(mComputationsMemory >> 20);
    bool useCgroups = mComputationDefaults.enforceMemory || mComputationDefaults.enforceCores;
    mProcessManager = std::unique_ptr<impl::ProcessManager>(new impl::ProcessManager(compMemoryMb,
                                                                                     useCgroups,
                                                                                     mComputationDefaults.enforceMemory,
                                                                                     mComputationDefaults.enforceCores,
                                                                                     mComputationDefaults.loanMemory));

    // expand the number of file descriptors we can use
    if (mOptions.setMaxFDs) setMaxFDs();

    // get list of all inet interfaces, and pick a default
    fetchInetInterfaces();

    // locate Consul and Coordinator services
    findServices();
   
    // set IPC socket address for communicating with router
    // and for passing in to new computations
    mComputationDefaults.ipcName = mOptions.ipcDir + "/arrasnodeipc-" + mNodeId.toString();

    // create a container for sessions : also launches and connects to 
    // the node router via ArrasController. Once this returns the router is running,
    // we are connected to it via IPC and we know the routers Inet (message) port
    mSessions = std::unique_ptr<ArrasSessions>(new ArrasSessions(*mProcessManager,
                                                                 mComputationDefaults,
                                                                 mNodeId));

    // start up HTTP REST service
    mNodeService = std::unique_ptr<NodeService>(new NodeService(mOptions.numHttpServerThreads,
                                                                !mOptions.disableBanlist,
                                                                mCoordinatorUrl,
                                                                *this,
                                                                *mSessions));

    // register the node with Consul and Coordinator (requires NodeService)
    buildNodeInfo();
    registerNode();

    // Get computations to send their events ("ready", "terminated") to
    // Coordinator via the NodeService
    mSessions->getController()->setEventHandler(mNodeService.get());
}

// initialize Athena logging
void ArrasNode::initLogging()
{
    log::AthenaLogger& logger = log::AthenaLogger::createDefault("node",
                                                                  mComputationDefaults.colorLogging, 
                                                                  mComputationDefaults.athenaEnv,
                                                                  mComputationDefaults.athenaHost, 
                                                                  mComputationDefaults.athenaPort);
    logger.setThreshold(static_cast<log::Logger::Level>(mComputationDefaults.logLevel));
    logger.setThreadName("main");
}

// identify URLs for the Consul and Coordinator services
// Normal case : default environment/datacenter is prod/gld
// Consul is looked up in the config service using these values
// Coordinator URL is looked up in Consul.
// However environment, datacenter, Consul URL and Coordinator 
// URL can all be set directly by command line options.
// May throw NodeError and ServiceError
void ArrasNode::findServices()
{
    // consul
    if (!mOptions.noConsul) {
        if (mOptions.consulHost.empty()) {
            if (mOptions.configServiceUrl.empty()) {
                    throw NodeError("DWA_CONFIG_SERVICE not set. Cannot determine consul endpoint");
            }
            ConfigurationClient cc(mOptions.configServiceUrl);
            mConsulUrl = cc.getServiceUrl("consul",
                                          mOptions.environment,
                                          mOptions.dataCenter);
            if (mConsulUrl.empty()) {
                throw NodeError("Failed to get Consul service endpoint from ConfigurationService");
            }
        } else {
            mConsulUrl = "http://" + mOptions.consulHost + ":" + 
                std::to_string(mOptions.consulPort);
        }
        ARRAS_INFO("Node using Consul URL " << mConsulUrl);
    }

    // coordinator
    if (mOptions.coordinatorHost.empty()) {
        if (mOptions.noConsul) {
            throw NodeError("Must specify Coordinator host if Consul is not being used.");
        }
        mCoordinatorUrl = std::string();
        ConsulClient cc(mConsulUrl);
        for (unsigned i = 0; i < GET_COORD_RETRIES; i++) {
            if (i > 0) {
                ARRAS_INFO("Waiting " << GET_COORD_RETRY_SLEEP << " second" <<
                           (GET_COORD_RETRY_SLEEP == 1 ? "" : "s") <<
                           " before trying again to fetch Coordinator endpoint");
                sleep(GET_COORD_RETRY_SLEEP);
            }
            try {
                mCoordinatorUrl = cc.getCoordinatorUrl();
            } catch (std::exception& e) {
                ARRAS_WARN(log::Id("warnGetCoord") <<
                           "Unable to fetch endpoint for coordinator from consul : " <<
                           e.what());
            }
            if (!mCoordinatorUrl.empty())
                break;
        }
        if (mCoordinatorUrl.empty()) {
            throw NodeError("Failed to get Coordinator service endpoint from Consul");
        }
    } else {
        mCoordinatorUrl = "http://" + mOptions.coordinatorHost + ":" +
            std::to_string(mOptions.coordinatorPort);
        if (!mOptions.coordinatorEndpoint.empty() &&
            mOptions.coordinatorEndpoint.front() != '/') {
            mCoordinatorUrl += "/";
        }
        mCoordinatorUrl += mOptions.coordinatorEndpoint;
    }
    ARRAS_INFO("Node using Coordinator URL " << mCoordinatorUrl);
}            

// register the node with Consul and Coordinator
// May throw NodeError and ServiceError
void ArrasNode::registerNode()
{
    ConsulClient consulClient("dummy");

    if (!mOptions.noConsul) {
        // we need to access consul via a numeric address
        // so that we always get the same instance of the service
        // otherwise there may be a race condition
        std::string consulUrl = mConsulUrl;

        // strip leading "http://" from url
        if (consulUrl.substr(0,7) == "http://") consulUrl = consulUrl.substr(7);

        // find the end of hostname
        size_t p = consulUrl.find_first_of(":/");

        // use hostname to get numeric IP address of consul service
        std::string ipAddr = hostToIp(consulUrl.substr(0,p));

        // reconstruct url using numeric address in place of hostname
        if (p != std::string::npos) {
            consulUrl = "http://" + ipAddr + consulUrl.substr(p);
        } else {
            consulUrl = "http://" + ipAddr;
        }

        consulClient = ConsulClient(consulUrl);
  
        // Register as a service in Consul
        mConsulServiceId =  "node@" + mHostName + ":" +
            std::to_string(mNodeService->httpPort());
 
        // may throw ServiceException
        consulClient.registerService(mConsulServiceId, "arras-node", 
                                      mHostIpAddr, mNodeService->httpPort());

        // Register health check with consul
        std::string healthUrl = "http://" + mHostIpAddr + ":" + 
            std::to_string(mNodeService->httpPort()) + "/node/1/health";


        mConsulHealthCheckName = "node-health@" + mHostName + ":" +
            std::to_string(mNodeService->httpPort());

        // may throw ServiceException
        consulClient.registerCheck(mConsulHealthCheckName,
                                   mConsulServiceId,
                                   healthUrl,
                                   30);
    }

    // Write node information to Coordinator and Consul
    bool ok = mNodeService->registerNode(mNodeInfo);
    if (ok && !mOptions.noConsul) ok = consulClient.updateNodeInfo(mNodeInfo);
    if (!ok) throw NodeError("Node registration failed");
        
    mIsRegistered = true;
}

void ArrasNode::deregisterNode()
{
    if (!mIsRegistered) return;

    std::string consulUrl("dummy");
    if (!mOptions.noConsul)
    {
        // see comment for registerNode
        consulUrl = mConsulUrl;
        if (consulUrl.substr(0,7) == "http://") consulUrl = consulUrl.substr(7);
        size_t p = consulUrl.find_first_of(":/");
        std::string ipAddr = hostToIp(consulUrl.substr(0,p));
        if (p != std::string::npos)
            consulUrl = "http://" + ipAddr + consulUrl.substr(p);
        else
            consulUrl = "http://" + ipAddr;
    }
    ConsulClient consulClient(consulUrl);

   try {
       mNodeService->deregisterNode(mNodeId);
       if (!mOptions.noConsul) consulClient.deregisterCheck(mConsulHealthCheckName);
       if (!mOptions.noConsul) consulClient.deregisterService(mConsulServiceId);
   } catch (std::exception& e) {
       ARRAS_ERROR(log::Id("NodeDeregisterFailed") <<
                   "Failure while deregistering node: " <<
                   e.what());
   }
   mIsRegistered = false;
}

// build the nodeInfo data that will be sent to Coordinator
// in order to register this node
void ArrasNode::buildNodeInfo()
{
    mNodeInfo["id"] = mNodeId.toString();
    mNodeInfo["hostname"] = mHostName;
    mNodeInfo["ipAddress"] = mHostIpAddr;
    mNodeInfo["httpPort"] = mNodeService->httpPort();
    mNodeInfo["port"] = mSessions->getController()->routerInetPort();
    mNodeInfo["status"] = "UP";

    // resources
    mNodeInfo["resources"]["cores"] = mComputationsCores;
    mNodeInfo["resources"]["memoryMB"] = static_cast<unsigned>(mComputationsMemory >> 20);
    const auto& procInfo = mProcessors.front();
    mNodeInfo["resources"]["cpuModelNumber"] = procInfo.mModel;
    mNodeInfo["resources"]["cpuModelName"] = procInfo.mModelName;
 
    api::Object cpuFlagsObj(Json::arrayValue);
    for (const auto& flag : procInfo.mFlags) {
        cpuFlagsObj.append(flag);
    }
    mNodeInfo["resources"]["cpuFlags"] = cpuFlagsObj;

    // Inet interfaces
    mNodeInfo["interfaces"] = mInetInterfaces;

    // options
    if (!mOptions.farmFullId.empty()) {
        mNodeInfo["farm_full_id"] = mOptions.farmFullId;
    } else {
        mNodeInfo["farm_full_id"] = api::Object();
    }

    if (mOptions.hostRU > 0.0f) {
        mNodeInfo["host_ru"] = mOptions.hostRU;
    } else {
        mNodeInfo["host_ru"] = api::Object();
    }

    // get user name
    std::string userName = mOptions.userName;
    if (userName.empty()) {
        userName = "unknown";
    }
    // tags may be an empty object, but must not be null (or Coordinator errors)
    mNodeInfo["tags"] = api::emptyObject;
    if (!mOptions.exclusiveUser.empty()) {
        if (mOptions.exclusiveUser == "_unspecified_") {
            mNodeInfo["tags"]["exclusive_user"] = userName;
        } else {
            mNodeInfo["tags"]["exclusive_user"] = mOptions.exclusiveUser;
        }
    }
    if (!mOptions.exclusiveProduction.empty())  {
        mNodeInfo["tags"]["exclusive_production"] = mOptions.exclusiveProduction;
        if (!mOptions.exclusiveTeam.empty()) {
            mNodeInfo["tags"]["exclusive_team"] = mOptions.exclusiveTeam;
        }
    }
    if (mOptions.overSubscribe) {
        mNodeInfo["tags"]["over_subscribe"] = true;
        // over-subscribe requires exclusiveUser
        if (mOptions.exclusiveUser.empty()) {
            mNodeInfo["tags"]["exclusive_user"] = userName;
        }
    }

    std::string sessionUrl = "http://" + mHostIpAddr + ":" + 
        std::to_string(mNodeService->httpPort()) + SESSIONS_HREF_PATH;
    mNodeInfo["hrefs"]["sessions"] = sessionUrl;

    // Supported client protocols : arras 4 supports BASIC TCP connections
    // but not WebSockets
    mNodeInfo["clientProtocols"] = CLIENT_PROTOCOL_BASIC;

    fetchRezVersionInfo(mNodeInfo["version_info"]);

    // OS release, version, and distribution info.
    mNodeInfo["os_version"] = mPlatformInfo.mOSVersion;
    mNodeInfo["os_release"] = mPlatformInfo.mOSRelease;
    mNodeInfo["os_distribution"] = mPlatformInfo.mOSDistribution;
    mNodeInfo["brief_version"] = mPlatformInfo.mBriefVersion;
    mNodeInfo["brief_distribution"] = mPlatformInfo.mBriefDistribution;
}

// fetch a list of all inet/inet6 interfaces as a JSON object,
// the full set will be set to Coordinator as part of this node's
// registration. The first INET interface found is chosen as the
// default and also sent to Coordinator    
void ArrasNode::fetchInetInterfaces()
{
    struct ifaddrs  *ipAddrs;
    if (!getifaddrs(&ipAddrs)) {
        // iterate through all interfaces
        struct ifaddrs  *addr = ipAddrs;
        while (addr) {
            if (addr->ifa_addr) {
                // check for INET/INET6, and not loopback
                sa_family_t family = addr->ifa_addr->sa_family;
                if ( (family == AF_INET || family == AF_INET6) && 
                     !(addr->ifa_flags & IFF_LOOPBACK)) {
                    // get numeric host name
                    char namebuf[NI_MAXHOST];
                    if (!getnameinfo(addr->ifa_addr,
                                     family==AF_INET ? sizeof(struct sockaddr_in)
                                     : sizeof(struct sockaddr_in6),
                                     namebuf, NI_MAXHOST,
                                     nullptr, 0, NI_NUMERICHOST)) 
                    {
                        const char* hostIp = const_cast<const char*>(namebuf);
                        // select the first INET non-loopback
                        // as the default
                        if (mHostIpAddr.empty() && family == AF_INET) {
                            mHostIpAddr = hostIp;
                        }

                        // place info in the mInetInterfaces JSON object
                        api::ObjectRef entry = mInetInterfaces[addr->ifa_name];
                        if (family == AF_INET) {
                            entry["AF_INET"] = hostIp;
                        } else {
                            entry["AF_INET6"] = hostIp;
                        }
                        entry["broadcast"] = addr->ifa_flags & IFF_BROADCAST ? "true" : "false";
                        entry["multicast"] = addr->ifa_flags & IFF_MULTICAST ? "true" : "false";
                    }
                }
            }
            addr = addr->ifa_next;
        }
        freeifaddrs(ipAddrs);
    }

    // also get non-numeric host name
    char buf[HOST_NAME_MAX+1];
    gethostname(buf, HOST_NAME_MAX+1);
    mHostName = const_cast<const char*>(buf);

    ARRAS_INFO("Node Address " << mHostIpAddr << " [" << mHostName << "]");
}

void ArrasNode:: fetchRezVersionInfo(api::ObjectRef info)
{  
    boost::environment_iterator envEof;
    boost::environment_iterator envItr(environ);

    for(; envItr != envEof; ++envItr) {
        const std::string envName(envItr->first);
        if (boost::algorithm::starts_with(envName, "REZ_") &&
            boost::algorithm::ends_with(envName, "_VERSION") &&
            !envItr->second.empty()) {
            info[envName] = envItr->second;
        }
    }
}

// calculate memory available for node process and also for
// computations. These can be set on the command line but default to
// 1GB node memory and all remaining physical memory for computations.
// Node process memory is not enforced, in part because the main user
// of memory will be the router process, not nodeservice.
// May throw NodeError
void
ArrasNode::calcResources()
{
    // mNodeMemory is memory reserved for use by the node process itself
    // this is not currently enforced
    if (mOptions.maxNodeMemory.empty()) {
        mNodeMemory = 1UL << 30; // 1 GB
    } else {
        mNodeMemory = memoryFromString(mOptions.maxNodeMemory);
    }

    unsigned long physicalMemory = HardwareFeatures::getMemoryInBytes();
    if (mNodeMemory >= physicalMemory) {
        throw NodeError("Requested node memory of " + std::to_string(mNodeMemory) +
                        " bytes exceeds host physical memory of " + std::to_string(physicalMemory) + " bytes");
    }

    // physical memory remaining for use by computations
    unsigned long availableMemory = physicalMemory - mNodeMemory;
    if (mOptions.memory.empty()) {
            // if computation memory not specified, allocate remaining physical memory
            mComputationsMemory = availableMemory;
    } else {
        mComputationsMemory = memoryFromString(mOptions.memory);
        if (mComputationsMemory > availableMemory) {
            ARRAS_WARN(log::Id("WarnPhysicalMemory") <<
                       "Requested total memory for node (node process + computations) of " <<
                       (mNodeMemory + mComputationsMemory) << " exceeds host physical memory of " <<
                       physicalMemory);
        }
    }
    ARRAS_INFO("Node memory " << mNodeMemory << " bytes, computation memory " << mComputationsMemory 
               << " bytes, total physical " << physicalMemory << " bytes"); 

    unsigned totalCores = HardwareFeatures::getProcessorInfo(mProcessors);
    if (mOptions.cores) {
        if (mOptions.cores > totalCores) {
            ARRAS_WARN(log::Id("CoresTooHigh") <<
                       "Requested number of cores (" << mOptions.cores << ") " <<
                       "is greater than the number available on this host (" << totalCores);
        } else {
            totalCores = mOptions.cores;
        }
    }
    // reserve 1 core for Node processes
    if (totalCores <= 1) {
        mNodeCores = 0;
        mComputationsCores = 1;
    } else {
        mNodeCores = 1;
        mComputationsCores = totalCores-1;
    }
    ARRAS_INFO(mComputationsCores << " cores available for computations");

    arras4::impl::getPlatformInfo(mPlatformInfo);

    // don't allocate so much memory for malloc arenas
    mallopt(M_ARENA_MAX, 2);
}


// Increase the number of available file descriptors to the maximum
// This is enabled by default, but a command line option can turn it off
void
ArrasNode::setMaxFDs()
{
    rlimit fdLimits;
    int status = getrlimit(RLIMIT_NOFILE, &fdLimits);
    if (status < 0) {
        throw NodeError("Failed to get current file descriptor limits");
    }
    if (fdLimits.rlim_cur < fdLimits.rlim_max) {
        ARRAS_DEBUG("Current fd limit at " << fdLimits.rlim_cur <<
                    ", setting to max of " << fdLimits.rlim_max);
        fdLimits.rlim_cur = fdLimits.rlim_max;
        status = setrlimit(RLIMIT_NOFILE, &fdLimits);
        if (status < 0) {
            throw NodeError("Failed to set current file descriptor limits");
        }
    }
}
  
void ArrasNode::stopRunning()
{
    std::unique_lock<std::mutex> lock(mRunMutex);
    mRun = false;
    mRunCondition.notify_all();
}

void ArrasNode::run()
{
    std::unique_lock<std::mutex> lock(mRunMutex);
    mRun = true;
    while (mRun) {
        mRunCondition.wait(lock);
    }
    
    // cleanup
    ARRAS_DEBUG("Shutting down node");
    mSessions->shutdownAll("node exiting");
    mNodeService->drainEvents(DRAIN_EVENTS_TIMEOUT);
    deregisterNode();

}

ArrasNode::~ArrasNode()
{  
    if (mNodeInfoUpdateThread.joinable())
        mNodeInfoUpdateThread.join();
}

void 
ArrasNode::setStatus(api::ObjectConstRef status)
{
    if (!status["status"].isString())
        throw OperationError("Request body is missing 'status' field",
                             HTTP_BAD_REQUEST);
    std::string statusStr = status["status"].asString();
    if (statusStr == "shutdown") {
        stopRunning();
    } else if (statusStr == "close") {
        mSessions->setClosed(true);
    } else if (statusStr == "unregistered") {
        // indicates Coordinator has unregistered this node :
        // all we need to do is make sure we don't unregister again
        mIsRegistered = false;
    } else {
        throw OperationError("Unknown 'status' value: "+statusStr,
                             HTTP_BAD_REQUEST);
    }
}

void
ArrasNode::updateTags(api::ObjectConstRef tags)
{
    if (!tags.isObject()) 
        throw OperationError("Invalid tag set (JSON object is required)", HTTP_BAD_REQUEST);
                             
    {
        std::lock_guard<std::mutex> lock(mNodeInfoMutex);
        if (mNodeInfoIsUpdating)
            throw OperationError("Cannot modify node tags, because service is busy with another update",
                               HTTP_RESOURCE_CONFLICT);

        // verify tags        
        api::Object current = mNodeInfo["tags"];
        for (api::ObjectConstIterator it = tags.begin();
             it != tags.end(); ++it) {
            current[it.memberName()] = *it;
        }
        std::string msg;
        if (!validateTags(current, msg)) 
            throw OperationError(msg, HTTP_BAD_REQUEST);

        mNodeInfoIsUpdating = true;
    }

    // start async modification thread
    if (mNodeInfoUpdateThread.joinable())
        mNodeInfoUpdateThread.join();
    mNodeInfoUpdateThread = std::thread(&ArrasNode::updateTagsProc,this,tags);
}

void
ArrasNode::deleteTags(api::ObjectConstRef tags)
{
    if (!tags.isArray()) 
        throw OperationError("Invalid tag list (JSON array is required)", HTTP_BAD_REQUEST);
    
    {
        std::lock_guard<std::mutex> lock(mNodeInfoMutex);
        if (mNodeInfoIsUpdating)
            throw OperationError("Cannot modify node tags, because service is busy with another update",
                               HTTP_RESOURCE_CONFLICT);

        // verify tags
        api::Object current = mNodeInfo["tags"];
        for (api::ObjectConstIterator it = tags.begin();
             it != tags.end(); ++it) {
            if ((*it).isString() && current.isMember((*it).asString())) {
                current.removeMember((*it).asString());
            }
        }
        std::string msg;
        if (!validateTags(current, msg)) 
            throw OperationError(msg, HTTP_BAD_REQUEST);

        mNodeInfoIsUpdating = true;
    }

    // start async modification thread
    if (mNodeInfoUpdateThread.joinable())
        mNodeInfoUpdateThread.join();
    mNodeInfoUpdateThread = std::thread(&ArrasNode::deleteTagsProc,this,tags);
}

void
ArrasNode::updateTagsProc(api::Object tags)
{
    api::Object current = mNodeInfo["tags"];
    for (api::ObjectIterator it = tags.begin();
         it != tags.end(); ++it) {
        current[it.memberName()] = *it;
    }

    std::string msg;
    if (validateTags(current, msg)) {
        mNodeInfo["tags"] = current;

        if (!mOptions.noConsul) {
            // updating consul with new node info
            ConsulClient cc(mConsulUrl);
            if (!cc.updateNodeInfo(mNodeInfo)) {
                msg = "Error in updating consul with new tags.";
                ARRAS_ERROR(arras4::log::Id("InvalidTagSet") << msg);
            }
        }
    } else {
        ARRAS_ERROR(arras4::log::Id("InvalidTagSet") << msg);
    }

    std::lock_guard<std::mutex> lock(mNodeInfoMutex); 
    mNodeInfoIsUpdating = false;
}

void
ArrasNode::deleteTagsProc(api::Object tags)
{
    api::Object current = mNodeInfo["tags"];
    for (api::ObjectIterator it = tags.begin();
         it != tags.end(); ++it) {
        if ((*it).isString() && current.isMember((*it).asString())) {
            current.removeMember((*it).asString());
        }
    }

    std::string msg;
    if (validateTags(current, msg)) {
        mNodeInfo["tags"] = current;

        if (!mOptions.noConsul) {
            // updating consul with new node info
            ConsulClient cc(mConsulUrl);
            if (!cc.updateNodeInfo(mNodeInfo)) {
                msg = "Error in updating consul when delete tags.";
                ARRAS_ERROR(arras4::log::Id("InvalidTagSet") << msg);
            }
        }
    } else {
        ARRAS_ERROR(arras4::log::Id("InvalidTagSet") << msg);
    }

    std::lock_guard<std::mutex> lock(mNodeInfoMutex);  
    mNodeInfoIsUpdating = false;
}
    

void 
ArrasNode::checkHealth()
{
    // check node is healthy, and throw an OperationError
    // if it isn't

    checkIpcSocket();
    checkDisk();
}

void ArrasNode::checkIpcSocket()
{
    boost::filesystem::path fsIPCName(mComputationDefaults.ipcName);
    boost::filesystem::file_status fsSatus = boost::filesystem::status(fsIPCName);

    if (boost::filesystem::exists(fsSatus)) {
        if (boost::filesystem::is_other(fsSatus)) {
            boost::filesystem::perms fsPerms = fsSatus.permissions();
            if ((static_cast<int>(fsPerms) & IPC_PERMS) != IPC_PERMS) {
                std::stringstream msg;
                msg << "IPC Socket file " << mComputationDefaults.ipcName << " exists, but permissions are"
                    << std::oct << fsPerms << " : required permissions are " << std::oct << IPC_PERMS;
                throw OperationError(msg.str(),HTTP_INTERNAL_SERVER_ERROR);
            }
        } else {
            throw OperationError("IPC Socket file " + mComputationDefaults.ipcName + 
                                 " exists, but is not a socket",HTTP_INTERNAL_SERVER_ERROR);
        }
    } else {
        throw OperationError("IPC Socket file " + mComputationDefaults.ipcName + 
                                 " does not exist",HTTP_INTERNAL_SERVER_ERROR);
    }
}

void ArrasNode::checkDisk()
{
    boost::filesystem::space_info si = boost::filesystem::space("/");
    float pctUsed = float(si.capacity - si.available) / si.capacity;
    if (pctUsed >= 0.98) {
        throw OperationError("Root partition usage at " + std::to_string(pctUsed*100) + "%",
                             HTTP_INTERNAL_SERVER_ERROR);
    }

    boost::filesystem::path tmpModel(getTmpDir());
    tmpModel += "%%%%-%%%%-%%%%-%%%%";
    boost::filesystem::path tmpFile(boost::filesystem::unique_path(tmpModel));

    std::ofstream tmp;
    tmp.open(tmpFile.native());
    if (tmp.is_open()) {
        tmp << 1;
        tmp.close();

        if (tmp.fail()) {
            throw OperationError("Unable to write to a sample tmp file: " + tmpFile.native(),
                                  HTTP_INTERNAL_SERVER_ERROR);
        }

        if (!boost::filesystem::remove(tmpFile)) {
             throw OperationError("Unable to remove sample tmp file: " + tmpFile.native(),
                                  HTTP_INTERNAL_SERVER_ERROR);
        }

    } else {
        throw OperationError("Unable to open a sample tmp file (" + tmpFile.native() + ") for writing",
                             HTTP_INTERNAL_SERVER_ERROR);
    }
}

}
}
    
    
