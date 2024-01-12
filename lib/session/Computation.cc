// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "Computation.h"
#include "Session.h"
#include "SessionError.h"
#include "ArrasController.h"

#include <execute/ProcessManager.h>
#include <shared_impl/ProcessExitCodes.h>

#include <signal.h>
#include <sys/time.h>

namespace arras4 {
    namespace node {

namespace {

std::string exitStatusString(impl::ExitStatus es, bool expected)
{
    if (es.exitType == impl::ExitType::Exit) {
	return arras4::impl::exitCodeString(es.status,expected);
    } else if (es.exitType == impl::ExitType::Signal) {
	// strsignal doesnt seem threadsafe
	std::string ret = "exited due to signal " + std::to_string(es.status);
	return ret;
    }
    return impl::ExitStatus::internalCodeString(es.status);
}

std::string timeString(long secs,int usec) 
{
    if (secs == 0 && usec == 0) {
        return "";
    }

    tm date;
    time_t ttsec = secs;
    localtime_r(&ttsec, &date);
    char datetime[30];
    datetime[0]=0;
    snprintf(datetime, sizeof(datetime)-1, "%4d-%02d-%02d %02d:%02d:%02d,%03ld",
            date.tm_year + 1900,
            date.tm_mon + 1,
            date.tm_mday,
            date.tm_hour,
            date.tm_min,
            date.tm_sec,
            (long int)(usec * 1e-3));
    return std::string(datetime);
}

}

Computation::Computation(const api::UUID& id,
                         const std::string& name,
                         Session& session)
    : mSession(session)
{
    mProcess = mSession.processManager().addProcess(id,
                                                    name,
                                                    session.id());
    if (!mProcess) {
        ARRAS_ERROR(log::Id("processObjectCreateFail") <<
                    log::Session(session.id().toString()) <<
                    "Failed to create Process object for " << name);
        throw SessionError("Failed to create Process object");
    }

    // initialize mLastActivitySecs to a reasonable value
    timeval now;
    gettimeofday(&now, nullptr);
    mLastActivitySecs = now.tv_sec;
}
        
Computation::~Computation()
{
    mSession.processManager().removeProcess(mProcess->id()); 
}

const api::UUID& Computation::id() const { return mProcess->id(); }
const api::UUID& Computation::sessionId() const { return mSession.id(); };
const std::string& Computation::name() const { return mProcess->name(); }

bool Computation::start(const impl::SpawnArgs& spawnArgs)
{
    impl::StateChange sc = mProcess->spawn(spawnArgs);
    if (!impl::StateChange_success(sc)) {
        ARRAS_ERROR(log::Id("processSpawnFail") <<
                    log::Session(mSession.id().toString()) <<
                    "Failed to spawn process for " << name());
        return false;
    }
    mTerminationExpected = false;
    timeval now;
    gettimeofday(&now, nullptr);
    mLastActivitySecs = now.tv_sec;
    return true;
}

void Computation::shutdown()
{
    mTerminationExpected = true;
    mProcess->terminate(false);
}

void Computation::signal(api::ObjectConstRef signalData)
{
    std::string status;
    if (signalData["status"].isString())
        status = signalData["status"].asString();

    impl::ProcessState state = mProcess->state();
    if (status == "run" && state == impl::ProcessState::Spawned) {
	if (mSentGo) {
	    // already sent "go", so send a routing update
	    mSession.arrasController().sendControl(id(), sessionId(),
						   "update", signalData);
	} else {
	    mSentGo = true;
	    mSession.arrasController().sendControl(id(), sessionId(),
						   "go", signalData);
	    if (mSession.isAutoSuspend()) {
		// autoSuspend suspends computations at "go" using
		// the signal SIGSTOP. This is used for debugging, and
		// they can be resumed using SIGCONT. It would
		// probably be better to do this with a message,
		// once this can be added to computations
		ARRAS_INFO("Auto-suspending computation " << name() <<
			   " by sending SIGSTOP. Use SIGCONT to resume.");
		kill(-mProcess->pid(),SIGSTOP);
		// send SIGSTOP to every member of the process group...
	    }
	}
    }
}

bool Computation::waitUntilShutdown(const std::chrono::steady_clock::time_point& endTime)
{
    return mProcess->waitUntilExit(nullptr,endTime);
}
 
// callback from Process when it terminates. Don't try to access mutex protected members
// of Process (name is ok)
void Computation::onTerminate(const api::UUID& id, 
                              const api::UUID& sessionId, 
                              impl::ExitStatus status)
{
    std::string typeStr = "fail";
    if (status.exitType == impl::ExitType::Exit) typeStr = "exit";
    else if (status.exitType == impl::ExitType::Signal) typeStr = "signal";
    ARRAS_ATHENA_TRACE(0,log::Session(sessionId.toString()) <<
		       "{trace:comp} " << typeStr << " " << id.toString() <<
		       " " << status.status);
    
    api::Object data;
    status.convertHighExitToSignal();
    data["reason"] = name() + " " + exitStatusString(status,mTerminationExpected);
    data["eventType"] = "computationTerminated";
    mSession.arrasController().handleEvent(sessionId,id,data);
}


void Computation::onSpawn(const api::UUID& /*id*/, const api::UUID& /*sessionId*/, pid_t /*pid*/)
{
    // no implementation
}

void Computation::onHeartbeat(impl::ExecutorHeartbeat::ConstPtr heartbeat)
{
    std::lock_guard<std::mutex> lock(mStatsMutex);
    mLastHeartbeat = heartbeat;
    // update max values
    if (heartbeat->mCpuUsage5SecsCurrent > mCpuUsage5SecsMax)
	mCpuUsage5SecsMax = heartbeat->mCpuUsage5SecsCurrent;
    if (heartbeat->mCpuUsage60SecsCurrent > mCpuUsage60SecsMax)
	mCpuUsage60SecsMax = heartbeat->mCpuUsage60SecsCurrent;
    if (heartbeat->mMemoryUsageBytesCurrent > mMemoryUsageBytesMax)
	mMemoryUsageBytesMax = heartbeat->mMemoryUsageBytesCurrent;
    if (heartbeat->mSentMessages5Sec > 0) {
	mLastSentMessagesSecs = heartbeat->mTransmitSecs;
	mLastSentMessagesMicroSecs = heartbeat->mTransmitMicroSecs;
	mLastActivitySecs = heartbeat->mTransmitSecs;
    }
    if (heartbeat->mReceivedMessages5Sec > 0) {
	mLastReceivedMessagesSecs = heartbeat->mTransmitSecs;
	mLastReceivedMessagesMicroSecs = heartbeat->mTransmitMicroSecs;
	mLastActivitySecs = heartbeat->mTransmitSecs;
    }
}

api::Object Computation::getStatus()
{
    api::Object status;
    impl::ProcessState ps;
    pid_t pid;
    impl::ExitStatus es;
    mProcess->state(ps,pid,es);
    if (ps == impl::ProcessState:: NotSpawned) {
	status["state"] = "NotStarted";
    } else if (ps == impl::ProcessState::Spawned) {
	if (mSentGo) {
	    status["state"] = "Running";
	} else {
	    status["state"] = "Starting";
	}
    } else if (ps == impl::ProcessState::Terminating) {
	status["state"] = "Stopping";
    } else {
	es.convertHighExitToSignal();
	status["state"] = "Stopped";
	status["stoppedReason"] = exitStatusString(es, mTerminationExpected);
	if (es.exitType == impl::ExitType::Exit) {
	    status["exitType"] = "Exit";
	    status["exitCode"] = es.status;
	} else if (es.exitType == impl::ExitType::Signal) {
	    status["exitType"] = "Signal";
	    status["signal"] = es.status;
	} else {
	    if (es.status == impl::ExitStatus::UNINTERRUPTABLE) {
		status["state"] = "Uninterruptable";
	    } else {
		status["state"] = "LaunchError";
	    }
	}
    }
    return status;
}    

void Computation::getPerformanceStats(api::ObjectRef obj)
{
    std::lock_guard<std::mutex> lock(mStatsMutex);
  
    obj["memoryUsageBytesMax"] = (Json::UInt64)mMemoryUsageBytesMax;
    obj["memoryUsageBytesCurrent"] = (Json::UInt64)mLastHeartbeat->mMemoryUsageBytesCurrent;

    obj["cpuUsage5Secs"] = mLastHeartbeat->mCpuUsage5SecsCurrent;
    obj["cpuUsage5SecsMax"] = mCpuUsage5SecsMax;
   
    obj["cpuUsage60Secs"] = mLastHeartbeat->mCpuUsage60SecsCurrent;
    obj["cpuUsage60SecsMax"] = mCpuUsage60SecsMax;
    obj["cpuUsageTotalSecs"] = mLastHeartbeat->mCpuUsageTotalSecs;
    obj["hyperthreaded"] = mLastHeartbeat->mHyperthreaded;

    obj["sentMessagesCount5Secs"] = (Json::UInt)mLastHeartbeat->mSentMessages5Sec;
    obj["sentMessagesCount60Secs"] = (Json::UInt)mLastHeartbeat->mSentMessages60Sec;
    obj["sentMessagesCountTotal"] = (Json::UInt)mLastHeartbeat->mSentMessagesTotal;

    obj["receivedMessagesCount5Secs"] = (Json::UInt)mLastHeartbeat->mReceivedMessages5Sec;
    obj["receivedMessagesCount60Secs"] = (Json::UInt)mLastHeartbeat->mReceivedMessages60Sec;
    obj["receivedMessagesCountTotal"] = (Json::UInt)mLastHeartbeat->mReceivedMessagesTotal;

    obj["lastHeartbeatTime"] = timeString(mLastHeartbeat->mTransmitSecs,mLastHeartbeat->mTransmitMicroSecs);
    obj["lastSentMessagesTime"] = timeString(mLastSentMessagesSecs,mLastSentMessagesMicroSecs);
    obj["lastReceivedMessagesTime"] = timeString(mLastReceivedMessagesSecs,mLastReceivedMessagesMicroSecs);
}

}
}
