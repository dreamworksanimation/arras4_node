// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "BanList.h"

#include <arras4_log/Logger.h>
#include <arras4_log/LogEventStream.h>

namespace arras4 {
namespace node {


BanList::BanList(unsigned int countToBan, unsigned int secToUnBan)
  : mCountToBan(countToBan)
  , mSecToUnBan(secToUnBan)
{
}


bool
BanList::isBanned(const std::string& sourceAddr)
{
    std::lock_guard<std::mutex> lock(mMutex);

    // source address is not in map, not ban
    const auto which = mBanListMap.find(sourceAddr);
    if (which == mBanListMap.end()) {
        return false;
    }
    
    // Check souce address's entry if it's expired.
    if (hasExpired(which->second.timeStamp)) {
        // unban source address by removing it from map, it has expired.
        // Stop tracking this source address for now.
        mBanListMap.erase(which);
        ARRAS_DEBUG("BanList::isBan expired: " << sourceAddr);
        return false;
    }

    // Log first time a request from an IP is being banned.
    // Note: "first time" means the start of this ban period(mSecToUnBan)
    if (which->second.count == mCountToBan) {
        ARRAS_DEBUG("BanList::isBan banning: " << sourceAddr);
        which->second.count++; // inc count so we won't log this again
        which->second.timeStamp = std::chrono::system_clock::now();
    }

    // Returns true if count exceeds ... 
    return which->second.count >= mCountToBan;
}


void
BanList::track(const std::string& sourceAddr)
{
    std::lock_guard<std::mutex> lock(mMutex);

    // Insert if map doesn't already contain a BanListEntry with sourceAddr
    const auto result = mBanListMap.emplace(sourceAddr, 1);
    if (!result.second) {
        // sourceAddr is already in map, just inc count and update time
        result.first->second.count++;
        result.first->second.timeStamp = std::chrono::system_clock::now();
    } else {
        // inserted, log first time a request from an IP is being tracked
        ARRAS_DEBUG("BanList::track tracking: " << sourceAddr);
    }
}


void
BanList::cleanup()
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (auto it = mBanListMap.begin(); it != mBanListMap.end();) {
        auto copy = it;
        ++it;
        if (hasExpired(copy->second.timeStamp)) {
	    ARRAS_DEBUG("BanList::cleanup expired: " << copy->first);
	    mBanListMap.erase(copy);
	}
    }
}


bool
BanList::hasExpired(
    const std::chrono::time_point<std::chrono::system_clock>& timeStamp)
{
    return (std::chrono::system_clock::now() - timeStamp) > mSecToUnBan;
}


void
BanList::getTracked(std::vector<std::string>& out)
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (auto & it : mBanListMap) {
        out.emplace_back(it.first);  // returns everything
    }
}


void
BanList::getBanned(std::vector<std::string>& out)
{
    std::lock_guard<std::mutex> lock(mMutex);

    for (auto & it : mBanListMap) {
        if (it.second.count >= mCountToBan) {
	    out.emplace_back(it.first);
        }
    }
}


void
BanList::getSummary(api::ObjectRef obj)
{
    cleanup();
    std::lock_guard<std::mutex> lock(mMutex);

    obj["banned"] = Json::Value(Json::arrayValue);
    obj["tracked"] = Json::Value(Json::arrayValue);
    for (auto & it : mBanListMap) {
        if (it.second.count >= mCountToBan) {
	    obj["banned"].append(it.first);
        } else {
	    obj["tracked"].append(it.first); 
	}
    }
}


} // end of namespace node 
} // end of namespace arras4

