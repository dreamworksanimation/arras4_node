// Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "HardwareFeatures.h"

#include <fstream>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include <unistd.h>

#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/xpressive/xpressive.hpp>

namespace {

// Static Regular expression to parse /proc/cpuinfo lines
// equivalent of "^(\w[\w|\s]*\w)\s*:\s(.*)$"
namespace bx = boost::xpressive;
const bx::sregex CPUINFO_LINE_RE = bx::bos
                                 >> (bx::s1 = bx::_w >> *(bx::_w|bx::space) >> bx::_w)
                                 >> *bx::space >> ":" >> bx::space
                                 >> (bx::s2 = *bx::_)
                                 >> bx::eos;

bool 
getCommandOutput(const std::string& command, std::string& output)
{
    FILE *fp;
    char line[1035];

    // run the command on a separate process for reading
    fp = popen(command.c_str(), "r");
    if (fp == NULL) {
        return false;
    }

    output.clear();
    // read the output one line at a time
    while (fgets(line, static_cast<int>(sizeof(line)) - 1, fp) != NULL) {
        output += line;
        output += "\n";
    }

    pclose(fp);

    return true;
}

std::pair<std::string, std::string>
splitProcInfoLine(const std::string& line)
{
    std::string key, value;
    bx::smatch match;

    if (bx::regex_match(line, match, CPUINFO_LINE_RE)) {
        key = match[1];
        value = match[2];
    }

    return std::make_pair(key, value);
}

} // namespace 

unsigned int
HardwareFeatures::getCoreCount()
{
    return std::thread::hardware_concurrency();
}

// For each "processor" on the system there is an entry in /proc/cpu_info
// that looks like this:
// --- start of copy
// processor       : 0
// vendor_id       : GenuineIntel
// cpu family      : 6
// model           : 62
// model name      : Intel(R) Xeon(R) CPU E5-2690 v2 @ 3.00GHz
// stepping        : 4
// cpu MHz         : 3001.000
// cache size      : 25600 KB
// physical id     : 0
// siblings        : 10
// core id         : 0
// cpu cores       : 10
// apicid          : 0
// initial apicid  : 0
// fpu		   : yes
// fpu_exception   : yes
// cpuid level	   : 13
// wp		   : yes
// flags	   : fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca
// cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx
// pdpe1gb rdtscp lm constant_tsc arch_perfmon pebs bts rep_good xtopology
// nonstop_tsc aperfmperf pni pclmulqdq dt es64 monitor ds_cpl vmx smx est tm2
// ssse3 cx16 xtpr pdcm pcid dca sse4_1 sse4_2 x2apic popcnt tsc_deadline_timer
// aes xsave avx f16 c rdrand lahf_lm ida arat epb xsaveopt pln pts dts
// tpr_shadow vnmi flexpriority ept vpid fsgsbase smep erms
// bogomips        : 5985.80
// clflush size    : 64
// cache_alignment : 64
// address sizes   : 46 bits physical, 48 bits virtual
// power management:
// --- end of copy
//
// Hyperthreads are recognized by having processors that use the same core id
// and physical id (chip). This functions currently just returns processor,
// chip, and core
//

HardwareFeatures::ProcessorList
HardwareFeatures::parseProcInfo(const std::string& aProcInfoFile)
{
    HardwareFeatures::ProcessorList processorList;
    auto pMap = std::unique_ptr<HardwareFeatures::ProcessorFeatures>(nullptr);

    std::ifstream input(aProcInfoFile);
    while (input.good()) {
        std::string line;
        std::getline(input, line);

        // The file ends with a blank line, when we get to that
        // we will add the last map to the processorList
        // In the next iteration getline() will again return an empty line
        // but this time pMap will be null
        if (!line.empty()) {
            if (pMap == nullptr) {
                pMap.reset(new HardwareFeatures::ProcessorFeatures);
            }

            pMap->insert(splitProcInfoLine(line));
        } else if (pMap != nullptr) {
            processorList.push_back(std::move(pMap));
        }
    }

    input.close();

    return processorList;
}

unsigned int
HardwareFeatures::getProcessorInfo(std::vector<ProcessorInfo>& aProcInfo,
                                   const std::string& aProcInfoFile)
{
    aProcInfo.clear();

    for (const auto& pMap : parseProcInfo(aProcInfoFile)) {
        ProcessorInfo info;
        HardwareFeatures::ProcessorFeatures::const_iterator it;

        it = pMap->find("processor");
        if (it != pMap->end()) {
            info.mId = boost::lexical_cast<unsigned short>(it->second);
        }

        it = pMap->find("physical id");
        if (it != pMap->end()) {
            info.mChip = boost::lexical_cast<unsigned short>(it->second);
        }

        it = pMap->find("core id");
        if (it != pMap->end()) {
            info.mCore = boost::lexical_cast<unsigned short>(it->second);
        }

        it = pMap->find("model");
        if (it != pMap->end()) {
            info.mModel = boost::lexical_cast<unsigned short>(it->second);
        }

        it = pMap->find("model name");
        if (it != pMap->end()) {
            info.mModelName = it->second;
        }

        it = pMap->find("flags");
        if (it != pMap->end()) {
            boost::split(info.mFlags, it->second, boost::is_any_of(" "));
        }

        aProcInfo.push_back(info);
    }

    return static_cast<unsigned int>(aProcInfo.size());
}

unsigned int
HardwareFeatures::getDeviceCount(const std::string& deviceID)
{
    // run a shell command to extract how many devices are currently available using lspci
    // NOTE: We list all devices with the passed device ID and then we count the lines with "wc -l"
    std::string command;
    std::string output;
    command += "/sbin/lspci -d \"*\":";
    command += deviceID;
    command += "|wc -l";
    if (!getCommandOutput(command, output)) {
        // we should probably throw or something else
        return 0;
    }
    // we should have a number
    return atoi(output.c_str());
}

unsigned int
HardwareFeatures::getMICCount()
{
    // NOTE: 0x225c is the devide ID of Xeo Phi co-processors
    return HardwareFeatures::getDeviceCount("225c");
}

unsigned long long
HardwareFeatures::getMemoryInBytes()
{
    return (unsigned long long) sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
}

