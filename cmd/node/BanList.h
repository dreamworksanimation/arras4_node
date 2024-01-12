// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __BANLIST_H__
#define __BANLIST_H__

#include <message_api/Object.h>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace arras4 {
namespace node {

// count : number of "Unsupported GET endpoint" accumulated from an address
// timeStamp: the latest time of the "Unsupported GET endpoint"
struct BanListEntry {
    unsigned int count;
    std::chrono::time_point<std::chrono::system_clock> timeStamp;
    BanListEntry(unsigned int c) : count(c), timeStamp(std::chrono::system_clock::now()) {}
};

// map with souce-addr as key
typedef std::map<std::string, BanListEntry> BanListMap;

class BanList {

public:
    BanList(unsigned int countToBan=5, unsigned int secToUnBan=300);

    // returns true if sourceAddr is ban
    bool isBanned(const std::string& sourceAddr);

    // track the given sourceAddr.
    void track(const std::string& sourceAddr);

    // remove entries which have expired
    void cleanup();

    // outputs list of tracked source addresses, including banned.
    void getTracked(std::vector<std::string>&);

    // outputs list of banned source addresses
    void getBanned(std::vector<std::string>&);

    void getSummary(api::ObjectRef obj);

 private:

    // returns true if given timestamp has expired and can unban
    bool hasExpired(const std::chrono::time_point<std::chrono::system_clock>&);


    unsigned int mCountToBan;
    std::chrono::seconds mSecToUnBan;
    BanListMap mBanListMap;
    std::mutex mMutex;
};

} 
} 

#endif // _BANLIST_H__

