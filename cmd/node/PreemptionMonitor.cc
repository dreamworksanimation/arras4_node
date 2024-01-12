// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "PreemptionMonitor.h"
#include "ServiceClient.h"
#include "ServiceError.h"
#include "ArrasNode.h"

#include <message_api/Object.h>
#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

namespace {

    // Based on documentation at
    // https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/spot-interruptions.html
    const std::string AWS_PREEMPTION_MONITOR_HOST= "http://169.254.169.254/";
    const std::string AWS_PREEMPTION_MONITOR_PATH = "latest/meta-data/spot/instance-action";
    
    const std::string AZURE_PREEMPTION_MONITOR_HOST= "http://169.254.169.254/";
    const std::string AZURE_PREEMPTION_MONITOR_PATH = "metadata/scheduledevents?api-version=2019-08-01";
}


namespace arras4 {
    namespace node {


void PreemptionMonitor::run()
{
    mRun = true;
    mThread = std::thread(&PreemptionMonitor::threadProc,this);
}

PreemptionMonitor::~PreemptionMonitor()
{
    mRun = false;
    mStopCondition.notify_all();
    if (mThread.joinable()) {
	mThread.join();
    }
}

class AWSSpotMonitor : public PreemptionMonitor
{
public:
    AWSSpotMonitor(ArrasNode* node) : PreemptionMonitor(node) {}
protected:
    void threadProc() override;
};


void AWSSpotMonitor::threadProc()
{
    log::Logger::instance().setThreadName("AWSPreemption");
    ARRAS_INFO("Running AWS Spot Monitor");

    ServiceClient monitor(AWS_PREEMPTION_MONITOR_HOST);
    while (mRun) {
	// 15 second delay between queries, which will be interrupted by
	// notification of mStopCondition
	{ 
	    std::unique_lock<std::mutex> lock(mStopMutex);
	    mStopCondition.wait_for(lock,std::chrono::seconds(15));
	    if (!mRun) break;
	}
	    
	try {
	    api::Object value = monitor.doGet(AWS_PREEMPTION_MONITOR_PATH);
	    if (!value["action"].isString() ||
		!value["time"].isString()) {
		ARRAS_WARN(arras4::log::Id("spotMonitorError") <<
			   "AWS spot monitor returned invalid data: " <<
			   api::objectToString(value));
	    } else {
		std::string action = value["action"].asString();
		std::string time = value["time"].asString();
		if (action == "stop") {
		    ARRAS_INFO("AWS spot instance is stopping at " << time << ". Shutting node down.");
		    if (mNode) mNode->stopRunning();
		} else if (action == "terminate") {
		    ARRAS_INFO("AWS spot instance is terminating at " << time << ". Shutting node down.");
		    if (mNode) mNode->stopRunning();
		}
	    } 
	} catch (ServiceError&) {
	    // some errors are normal...
	} catch (std::exception& e) {
	    ARRAS_WARN(arras4::log::Id("spotMonitorError") <<
		       "Exception making spot monitor query : " << e.what());
	} catch (...) {
	    ARRAS_WARN(arras4::log::Id("spotMonitorError") <<
		       "Unexpected exception making spot monitor query");
	}
    }
    ARRAS_INFO("Stopped AWS Spot Monitor");
    
}
    
class AzurePreemptionMonitor : public PreemptionMonitor
{
public:
    AzurePreemptionMonitor(ArrasNode* node) : PreemptionMonitor(node) {}
protected:
    void threadProc() override;
};

void AzurePreemptionMonitor::threadProc()
{
    log::Logger::instance().setThreadName("AzurePreemption");
    ARRAS_INFO("Running Azure Preemption Monitor");

    ServiceClient monitor(AZURE_PREEMPTION_MONITOR_HOST);
    while (mRun) {

        // 15 second delay between queries, which will be interrupted by
	// notification of mStopCondition
	{ 
	    std::unique_lock<std::mutex> lock(mStopMutex);
	    mStopCondition.wait_for(lock,std::chrono::seconds(15));
	    if (!mRun) break;
	}

	try {
	    std::map<std::string,std::string> headers;
	    headers["Metadata"] =  "true";
	    api::Object root = monitor.doGet(AZURE_PREEMPTION_MONITOR_PATH,headers);
	    api::Object events = root["Events"];
	    for (api::ObjectIterator eIt = events.begin();
		 eIt != events.end(); ++eIt) {
		api::ObjectRef event = *eIt;
		if (event["EventType"].isString()) {
		    std::string eventtype = event["EventType"].asString();
		    if ((eventtype == "Reboot") || (eventtype == "Redeploy") || (eventtype == "Preempt")) {
			std::string time("[Unknown]");
			if (event["NotBefore"].isString()) {
			    time = event["NotBefore"].asString();
			}
			ARRAS_INFO("Azure instance will " << eventtype << " at " << time << ". Shutting node down.");
			if (mNode) mNode->stopRunning();
		    }
		}
	    }
	} catch (ServiceError&) {
	    // some errors are normal...
	} catch (std::exception& e) {
	    ARRAS_WARN(arras4::log::Id("AzureMonitorError") <<
		       "Exception making azure monitor query : " << e.what());
	} catch (...) {
	    ARRAS_WARN(arras4::log::Id("azureMonitorError") <<
		       "Unexpected exception making azure preemptionmonitor query");
	}
    }
    ARRAS_INFO("Stopped Azure Preemption Monitor");
}
	    
/*static*/ PreemptionMonitor* 
PreemptionMonitor::start(PreemptionMonitorType type,
                         ArrasNode* node)
{
    PreemptionMonitor* ret = nullptr;
    if (type == PreemptionMonitorType::AWS) {
	ret = new AWSSpotMonitor(node);
    } else if (type == PreemptionMonitorType::Azure) {
	ret = new AzurePreemptionMonitor(node);
    }
    if (ret) ret->run();
    return ret;
}

}
}
