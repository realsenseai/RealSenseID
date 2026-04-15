// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/Version.h"

#include <regex>
#include <sstream>

namespace RealSenseID
{

const char* Description(DeviceType deviceType)
{
    switch (deviceType)
    {
    case RealSenseID::DeviceType::F45x:
        return "F45x";
    case RealSenseID::DeviceType::F50x:
        return "F50x";
    default:
        return "Unknown";
    }
}

const char* Version()
{
    static std::string version =
        std::string(std::to_string(RSID_VER_MAJOR) + '.' + std::to_string(RSID_VER_MINOR) + '.' + std::to_string(RSID_VER_PATCH));
    return version.c_str();
}

// use static version strings to be able to return const char* directly and to avoid string copy and allocation when using from the c
// wrapper
const char* CompatibleFirmwareVersion(DeviceType device)
{
    switch (device)
    {
    case DeviceType::F45x: {
        static std::string version = std::string(std::to_string(RSID_FW45x_VER_MAJOR) + '.' + std::to_string(RSID_FW45x_VER_MINOR));
        return version.c_str();
    }

    case DeviceType::F50x: {
        static std::string version = std::string(std::to_string(RSID_FW46x_VER_MAJOR) + '.' + std::to_string(RSID_FW46x_VER_MINOR));
        return version.c_str();
    }
    default:
        return "Unknown";
    }
}

bool IsFwCompatibleWithHost(DeviceType device_type, const std::string& fw_version)
{
    std::string search_in = fw_version;

    // firmware version api returns a single version string for all modules
    // this is a temp measure to first extract the relevant firmware module string
    std::stringstream version_stream(fw_version);
    std::string section;
    while (std::getline(version_stream, section, '|'))
    {
        if (section.find("OPFW:") != std::string::npos)
        {
            auto pos = section.find(':');
            auto sub = section.substr(pos + 1, std::string::npos);
            search_in = sub;
        }
    }

    // try to extract semver from string
    std::smatch matches;
    std::regex SEMVER_REGEX(R"(^([0-9]+)\.([0-9]+)\.([0-9]+)\.([0-9]+)$)");
    bool success = std::regex_match(search_in, matches, SEMVER_REGEX);

    if (!success)
        return false;

    auto version_major = std::stoi(matches[1]);
    auto version_minor = std::stoi(matches[2]);

    if (device_type == DeviceType::F45x)
        return version_major == RSID_FW45x_VER_MAJOR && version_minor >= RSID_FW45x_VER_MINOR;
    else if (device_type == DeviceType::F50x)
        return version_major == RSID_FW46x_VER_MAJOR && version_minor >= RSID_FW46x_VER_MINOR;
    else
        return false;
}
} // namespace RealSenseID