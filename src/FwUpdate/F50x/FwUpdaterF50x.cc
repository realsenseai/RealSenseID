// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "FwUpdaterF50x.h"
#include "RealSenseID/DeviceController.h"
#include "PacketManager/Timer.h"
#include "FwUpdateEngineF50x.h"
#include "Logger.h"
#include "Utilities.h"

#include <algorithm>
#include <fstream>
#include <exception>
#include <regex>
#include <sstream>
#include <string>

namespace RealSenseID
{

namespace FwUpdateF50x
{

static const char* LOG_TAG = "FwUpdaterF50x";
static constexpr long NORMAL_BAUD_RATE = 115200;
static const char* MODULE_OPFW = "OPFW";
static const char* MODULE_RECOG = "RECOG";

static bool DoesFileExist(const char* path)
{
    if (path == nullptr)
        return false;
    std::ifstream f(path);
    return f.good();
}

bool FwUpdaterF50x::ExtractFwInformation(const char* binPath, std::string& outFwVersion, std::string& outRecognitionVersion,
                                         std::vector<std::string>& moduleNames) const
{
    try
    {
        outFwVersion.clear();
        outRecognitionVersion.clear();
        moduleNames.clear();

        if (binPath == nullptr || !DoesFileExist(binPath))
        {
            return false;
        }

        FwUpdateEngineF50x update_engine;
        auto modules = update_engine.ModulesFromFile(binPath);

        for (const auto& module : modules)
        {
            moduleNames.push_back(module.name);
            if (module.name == MODULE_OPFW)
            {
                outFwVersion = module.version;
            }
            else if (module.name == MODULE_RECOG)
            {
                outRecognitionVersion = module.version;
            }
        }

        if (outFwVersion.empty() || outRecognitionVersion.empty())
        {
            return false;
        }

        return true;
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return false;
    }
}

Status FwUpdaterF50x::UpdateModules(FwUpdater::EventHandler* handler, FwUpdater::Settings settings, const char* binPath) const
{
    LOG_DEBUG(LOG_TAG, "UpdateModules Entry");
    try
    {
        if (binPath == nullptr)
        {
            LOG_ERROR(LOG_TAG, "binPath is null");
            return Status::Error;
        }

        // Check firmware upgrade file exists.
        if (!DoesFileExist(binPath))
        {
            LOG_ERROR(LOG_TAG, "file does not exist: :%s", binPath);
            return Status::Error;
        }

        auto callback_wrapper = [&handler](float progress) {
            if (handler != nullptr)
            {
                handler->OnProgress(progress);
            }
        };

        FwUpdateEngineF50x::Settings internal_settings;
        internal_settings.fw_filename = binPath;
        internal_settings.baud_rate = NORMAL_BAUD_RATE;
        internal_settings.serial_config = settings.serial_config;
        internal_settings.force_full = settings.force_full;

        FwUpdateEngineF50x update_engine;
        auto modules = update_engine.ModulesFromFile(binPath);
        PacketManager::Timer timer;
        update_engine.BurnModules(internal_settings, modules, callback_wrapper);
        auto elapsed_seconds = timer.Elapsed().count() / 1000;
        LOG_INFO(LOG_TAG, "Firmware update success (duration %lldm:%llds)", elapsed_seconds / 60, elapsed_seconds % 60);
        return Status::Ok;
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
}

// Connects to the device, queries bspver, and returns the content between the two delimiter lines.
// Throws on connection/query failure.
static std::string QueryBspVerContent(const FwUpdater::Settings& settings)
{
    RealSenseID::DeviceController device_controller(DeviceType::F50x);
    Status s = device_controller.Connect(settings.serial_config);
    if (s != Status::Ok)
    {
        throw std::runtime_error("Failed to connect to device");
    }

    std::string bspver;
    s = device_controller.QueryBspVer(bspver);
    if (s != Status::Ok)
    {
        throw std::runtime_error("Failed to query bspver");
    }

    static const std::string delimiter = "+-----------------------------------------------+";
    auto first_pos = bspver.find(delimiter);
    if (first_pos == std::string::npos)
    {
        throw std::runtime_error("Failed to parse bspver: cannot find first delimiter");
    }
    auto second_pos = bspver.find(delimiter, first_pos + delimiter.size());
    if (second_pos == std::string::npos)
    {
        throw std::runtime_error("Failed to parse bspver: cannot find second delimiter");
    }

    return bspver.substr(first_pos, second_pos + delimiter.size() - first_pos);
}

bool FwUpdaterF50x::CheckCompatibility(const FwUpdater::Settings& settings, const char* binPath, FwUpdater::FwCompatibilityInfo& info) const
{
    if (binPath == nullptr)
        return false;

    try
    {
        // Parse all binary metadata in one file open.
        auto meta = FwUpdateF50x::ParseUfifMetadata(binPath);
        info.expectedSecureBoot = static_cast<int>(meta.secureBootEnabled);
        info.expectedDbVer = static_cast<int>(meta.dbVersion);
        info.expectedDeviceType = static_cast<int>(meta.deviceType);

        // Query device via a single bspver call and parse all device-side values from it.
        auto bspver_content = QueryBspVerContent(settings);

        // Secure boot: presence of "(secure)" in bspver indicates CSS-signed firmware.
        info.deviceSecureBoot = (bspver_content.find("(secure)") != std::string::npos) ? 1 : 0;

        // DB version: udb_ver = ICATCH_DB_VER in F460/F500 firmware.
        // Soft-fail: device in bootloader may not expose udb_ver; leave deviceDbVer = -1 to skip the check.
        std::regex cfg_ver_rgx(R"(udb_ver:\s*(\d+))");
        std::smatch match;
        if (!std::regex_search(bspver_content, match, cfg_ver_rgx))
            LOG_INFO(LOG_TAG, "Failed to parse udb_ver from bspver, DB version check skipped");
        else
            info.deviceDbVer = std::stoi(match[1].str());

        // Device type: model name in bspver title line.
        // Soft-fail: device in bootloader may not expose model name; leave connectedDeviceType = -1 to skip the check.
        if (bspver_content.find("F460") != std::string::npos)
            info.connectedDeviceType = 1;
        else if (bspver_content.find("F500") != std::string::npos)
            info.connectedDeviceType = 2;
        else
            LOG_INFO(LOG_TAG, "Failed to determine device type from bspver, device type check skipped");

        LOG_INFO(LOG_TAG, "SecureBoot: binary=%d device=%d, DB: binary=%d device=%d, Type: binary=%d device=%d", info.expectedSecureBoot,
                 info.deviceSecureBoot, info.expectedDbVer, info.deviceDbVer, info.expectedDeviceType, info.connectedDeviceType);

        return info.IsAllCompatible();
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return false;
    }
}

struct FirmwareVersion
{
public:
    int fwMajor, fwMinor;
    FirmwareVersion(const int& fwMajor, const int& fwMinor) : fwMajor(fwMajor), fwMinor(fwMinor)
    {
    }
    bool operator<(const FirmwareVersion& other) const
    {
        // We want to switch to OPFW_FIRST only once the major version number passes the one from the critical version.
        // Therefore we ignore the minor version number in the comparison.
        // return fwMajor < other.fwMajor || (fwMajor == other.fwMajor && fwMinor < other.fwMinor);
        return fwMajor < other.fwMajor;
    }

    bool operator>(const FirmwareVersion& other) const
    {
        return other < *this;
    }

    bool operator<=(const FirmwareVersion& other) const
    {
        return !(*this > other);
    }

    bool operator>=(const FirmwareVersion& other) const
    {
        return !(*this < other);
    }

    std::string ToString() const
    {
        std::stringstream ss;
        ss << fwMajor << "." << fwMinor << ".#.#";
        return ss.str();
    }
};

} // namespace FwUpdateF50x
} // namespace RealSenseID
