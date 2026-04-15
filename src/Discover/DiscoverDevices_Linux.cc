// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/DeviceController.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <vector>
#include <tuple>
#include <dirent.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>

#ifdef RSID_PREVIEW
#include "DiscoverCapture.h"
#endif

static const char* LOG_TAG = "DiscoverDevices";

namespace RealSenseID
{
struct DeviceDescriptor
{
    const std::string vid;
    const std::string pid;
    const DeviceType deviceType = DeviceType::Unknown;
};

static const std::vector<DeviceDescriptor> ExpectedVidPidPairs {/*{"04d8", "00dd", DeviceType::F45x},// debug channel - CMD*/
                                                                {"2aad", "6373", DeviceType::F45x},
                                                                /* {"414c", "6666", DeviceType::F50x}, // debug channel - CMD*/
                                                                {"414c", "6578", DeviceType::F50x}};

// Match given vid/pid against the known RSID device table and return the device type.
static bool MatchToExpectedVidPidPairs(std::string vid, std::string pid, DeviceType& deviceType)
{
    deviceType = DeviceType::Unknown;
    std::transform(vid.begin(), vid.end(), vid.begin(), ::tolower);
    std::transform(pid.begin(), pid.end(), pid.begin(), ::tolower);

    for (const auto& expected : ExpectedVidPidPairs)
    {
        if (vid == expected.vid && pid == expected.pid)
        {
            deviceType = expected.deviceType;
            return true;
        }
    }
    return false;
}

// Parse VID/PID from a sysfs "uevent" file and return them as hex strings.
// uevent file should contain a line like this: "PRODUCT=414c/6578/10c"
static const std::regex PRODUCT_REGEX(R"(PRODUCT=([0-9A-Fa-f]{4})/([0-9A-Fa-f]{4})/)", std::regex_constants::icase);
static std::tuple<bool, std::string, std::string> ParseVidPid(const std::string& uevent_file_path)
{
    std::ifstream uevent_file(uevent_file_path);
    if (!uevent_file)
    {
        LOG_WARNING(LOG_TAG, "Cannot access %s.", uevent_file_path.c_str());
        return {false, "", ""};
    }
    std::string line;
    while (std::getline(uevent_file, line))
    {
        std::smatch matches;
        if (std::regex_search(line, matches, PRODUCT_REGEX) && matches.size() == 3)
        {
            return {true, matches[1].str(), matches[2].str()};
        }
    }
    return {false, "", ""};
}

// Return the parent directory of a given path (no trailing slash, unless root).
static std::string ParentPath(const std::string& path)
{
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return {};
    if (pos == 0)
        return "/";
    return path.substr(0, pos);
}

// Read an unsigned int from a sysfs file (e.g., busnum/devnum).
static bool ReadUnsignedIntFile(const std::string& path, unsigned int& value)
{
    std::ifstream file(path);
    if (!file)
    {
        LOG_WARNING(LOG_TAG, "ReadUnsignedIntFile: failed to open %s", path.c_str());
        return false;
    }

    file >> value;
    if (!file)
    {
        LOG_WARNING(LOG_TAG, "ReadUnsignedIntFile: failed to read integer from %s", path.c_str());
        return false;
    }

    return true;
}

// Given a sysfs interface path (e.g. .../1-1.2.1:1.0), find the USB device dir and read busnum/devnum.
static bool GetUsbBusAndAddress(const std::string& real_path, unsigned int& bus, unsigned int& addr)
{
    std::string dev_dir = ParentPath(real_path);
    if (dev_dir.empty())
    {
        LOG_WARNING(LOG_TAG, "GetUsbBusAndAddress: empty parent for %s", real_path.c_str());
        return false;
    }

    const std::string bus_path = dev_dir + "/busnum";
    const std::string devnum_path = dev_dir + "/devnum";

    if (!ReadUnsignedIntFile(bus_path, bus))
    {
        LOG_WARNING(LOG_TAG, "GetUsbBusAndAddress: failed reading busnum from %s", bus_path.c_str());
        return false;
    }
    if (!ReadUnsignedIntFile(devnum_path, addr))
    {
        LOG_WARNING(LOG_TAG, "GetUsbBusAndAddress: failed reading devnum from %s", devnum_path.c_str());
        return false;
    }

    return true;
}

#ifdef RSID_PREVIEW

std::vector<int> DiscoverCapture()
{
    std::vector<int> capture_numbers;
    for (const auto& info : DiscoverDevices())
    {
        if (info.cameraNumber >= 0)
            capture_numbers.push_back(info.cameraNumber);
    }
    return capture_numbers;
}

#else

std::vector<int> DiscoverCapture()
{
    LOG_WARNING(LOG_TAG, "DiscoverCapture is disabled when RSID_PREVIEW is disabled.");
    return {};
}

#endif // RSID_PREVIEW

// Detect device type (F45x / F50x / Unknown) for the given serial port using sysfs VID/PID.
DeviceType DiscoverDeviceType(const char* serial_port)
{
    if (strncmp(serial_port, "/dev/", 5) != 0)
    {
        LOG_WARNING(LOG_TAG, "Cannot detect device type. port must start with /dev/");
        return DeviceType::Unknown;
    }

    std::string dev_path = "/sys/class/tty/" + std::string(serial_port).substr(5) + "/device/uevent";

    bool ok = false;
    std::string vid, pid;
    std::tie(ok, vid, pid) = ParseVidPid(dev_path);
    if (!ok)
    {
        LOG_WARNING(LOG_TAG, "Cannot detect device type (ParseVidPid failed)");
        return DeviceType::Unknown;
    }

    DeviceType deviceType;
    if (!MatchToExpectedVidPidPairs(vid, pid, deviceType))
    {
        LOG_WARNING(LOG_TAG, "Cannot detect device type (MatchToExpectedVidPidPairs)");
        return DeviceType::Unknown;
    }

    LOG_INFO(LOG_TAG, "Detected device type %s", Description(deviceType));
    return deviceType;
}

// ----------------------------------------------------------------------------
// Discover all connected RSID devices.
//
// Walks /sys/bus/usb/devices, matches interfaces by VID/PID, finds tty children,
// and queries device type + serial number.
//
// When RSID_PREVIEW is enabled, also matches each device to a capture index
// (V4L2 /dev/videoN index or libuvc device list index) via USB bus/addr.
// ----------------------------------------------------------------------------
std::vector<DeviceInfo> DiscoverDevices()
{
    std::vector<DeviceInfo> devices;
    constexpr const char* base_path = "/sys/bus/usb/devices/";
    LOG_DEBUG(LOG_TAG, "Discovering devices...");
    DIR* dir = opendir(base_path);
    if (!dir)
    {
        LOG_ERROR(LOG_TAG, "Cannot open %s", base_path);
        return devices;
    }

#ifdef RSID_PREVIEW
    const auto capture_entries = DiscoverCaptureEntries();
    if (capture_entries.empty())
    {
        LOG_DEBUG(LOG_TAG, "No capture devices detected");
    }
#endif

    while (dirent* entry = readdir(dir))
    {
        std::string name = entry->d_name;
        if (name == "." || name == ".." || name.find(':') == std::string::npos)
            continue;

        std::string full_path = base_path + name;
        char real_buf[PATH_MAX] = {};
        if (!realpath(full_path.c_str(), real_buf))
        {
            LOG_WARNING(LOG_TAG, "realpath failed for %s", full_path.c_str());
            continue;
        }

        std::string real_path(real_buf);
        std::string uevent_path = real_path + "/uevent";

        DeviceType deviceType;
        std::string vid, pid;
        bool ok;
        std::tie(ok, vid, pid) = ParseVidPid(uevent_path);

        if (!ok || !MatchToExpectedVidPidPairs(vid, pid, deviceType))
            continue;

#ifdef RSID_PREVIEW
        unsigned int busnum = 0;
        unsigned int devaddr = 0;
        const bool have_bus_addr = GetUsbBusAndAddress(real_path, busnum, devaddr);
#endif

        std::string tty_path = real_path + "/tty";
        DIR* tty_dir = opendir(tty_path.c_str());
        if (!tty_dir)
            continue;

        while (dirent* tty_entry = readdir(tty_dir))
        {
            std::string tty_name = tty_entry->d_name;
            if (tty_name == "." || tty_name == "..")
                continue;

            std::string dev_path = "/dev/" + tty_name;
            struct stat s
            {
            };
            if (stat(dev_path.c_str(), &s) != 0 || !S_ISCHR(s.st_mode))
                continue;

            DeviceInfo info = {0};
            strncpy(info.serialPort, dev_path.c_str(), sizeof(info.serialPort) - 1);
            info.serialPort[sizeof(info.serialPort) - 1] = '\0';

            info.deviceType = DiscoverDeviceType(info.serialPort);

#ifdef RSID_PREVIEW
            info.cameraNumber = -1;
            if (have_bus_addr)
            {
                info.cameraNumber = FindCaptureIndex(capture_entries, busnum, devaddr);
                if (info.cameraNumber >= 0)
                    LOG_DEBUG(LOG_TAG, "Matched %s to capture device %d (bus=%u addr=%u)", info.serialPort, info.cameraNumber, busnum,
                              devaddr);
                else
                    LOG_WARNING(LOG_TAG, "No capture device for %s (bus=%u addr=%u)", info.serialPort, busnum, devaddr);
            }
#else
            info.cameraNumber = -1;
#endif

#if !defined(__ANDROID__)
            // query device serial number via DeviceController
            if (info.deviceType != DeviceType::Unknown)
            {
                DeviceController controller {info.deviceType};
                if (controller.Connect({info.serialPort}) == RealSenseID::Status::Ok)
                {
                    std::string serial_num;
                    if (controller.QuerySerialNumber(serial_num) == RealSenseID::Status::Ok)
                    {
                        ::strncpy(info.serialNumber, serial_num.c_str(), sizeof(info.serialNumber) - 1);
                        info.serialNumber[sizeof(info.serialNumber) - 1] = '\0';
                    }
                    else
                    {
                        LOG_WARNING(LOG_TAG, "Failed to retrieve s/n on port %s", info.serialPort);
                    }
                }
                devices.push_back(info);
            }
            else
            {
                LOG_WARNING(LOG_TAG, "Failed to detect device type for port: %s", info.serialPort);
            }
#else // __ANDROID__
            if (info.deviceType != DeviceType::Unknown)
            {
                devices.push_back(info); // On Android we don't connect directly to the device, so we skip serial number query
            }
            else
            {
                LOG_WARNING(LOG_TAG, "Failed to detect device type for port: %s", info.serialPort);
            }
#endif
        }
        closedir(tty_dir);
    }

    closedir(dir);
    return devices;
}

} // namespace RealSenseID
