// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef __ARRAS_HARDWARE_FEATURES__
#define __ARRAS_HARDWARE_FEATURES__

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {
const std::string CPUINFO = "/proc/cpuinfo";
}

struct HardwareFeatures
{
    struct ProcessorInfo {
        unsigned short mId;
        unsigned short mCore;
        unsigned short mChip;
        unsigned short mModel;
        std::string mModelName;
        std::vector<std::string> mFlags;
    };

    /// Returns how many hardware cores the system has
    static unsigned int getCoreCount();

    /// Returns how many devices with the given device ID are currently present on the system.
    /// Pass the device ID as a 4-hexadecimal digit string (e.g. "255c", etc.)
    static unsigned int getDeviceCount(const std::string& deviceID);

    /// Returns how many MIC (Intel's Xeon Phi) co-processors are currently present on the system
    static unsigned int getMICCount();

    /// Returns size of installed physical RAM
    static unsigned long long getMemoryInBytes();

    static unsigned int getProcessorInfo(std::vector<ProcessorInfo>& aProcessorInfo,
                                         const std::string& aProcInfoFile=CPUINFO);

    // Returns the parsed results of /proc/cpuinfo as a vector of key/value pair maps.
    typedef std::map<std::string, std::string> ProcessorFeatures;
    typedef std::vector<std::unique_ptr<ProcessorFeatures>> ProcessorList;
    static ProcessorList parseProcInfo(const std::string& aProcInfoFile=CPUINFO);
};

#endif // __ARRAS_HARDWARE_FEATURES__

