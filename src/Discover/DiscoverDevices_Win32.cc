// License: Apache 2.0. See LICENSE file in root directory.

#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/DeviceController.h"
#include "Logger.h"

// clang-format off
#include <windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <evr.h>
#include <mfapi.h>
#include <cfgmgr32.h>
#include <initguid.h>   
#include <devpkey.h>
#include <devpropdef.h>
// clang-format on
#include <string.h>
#include <string>
#include <sstream>
#include <regex>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cassert>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mf")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "Strmiids")
#pragma comment(lib, "Mfreadwrite")
#pragma comment(lib, "Cfgmgr32")

static const char* LOG_TAG = "DiscoverDevices";

namespace RealSenseID
{
// Extract VID / PID / COMx using regex from PnP IDs / names
static const std::regex VID_REGEX(".*VID_([0-9A-Fa-f]{4}).*", std::regex_constants::icase);
static const std::regex PID_REGEX(".*PID_([0-9A-Fa-f]{4}).*", std::regex_constants::icase);
static const std::regex COM_PORT_REGEX {".*(COM[0-9]+).*"};

static constexpr size_t BUFFER_SIZE = 4096;

// Maps VID/PID pair to DeviceType
struct DeviceDescriptor
{
    const std::string vid;
    const std::string pid;
    const DeviceType deviceType = DeviceType::Unknown;
};

// Known RealSenseID VID/PID pairs
static const std::vector<DeviceDescriptor> ExpectedVidPidPairs {/*{"04d8", "00dd", DeviceType::F45x},// debug channel - CMD*/
                                                                {"2aad", "6373", DeviceType::F45x},
                                                                /* {"414c", "6666", DeviceType::F50x}, // debug channel - CMD*/
                                                                {"414c", "6578", DeviceType::F50x}};

// Holds a Media Foundation capture device index + its ContainerId
// This allows us to match UVC streams to COM ports by comparing ContainerIds.
struct CaptureDeviceEntry
{
    int index = -1;      // MF device index
    GUID containerId {}; // Device ContainerId
};

// Check if given VID/PID matches one of the known RealSenseID descriptors
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

    assert(deviceType == DeviceType::Unknown);
    return false;
}

// Extract the first capture group from a regex if it matches the entire string.
// Used for VID/PID/COMx extraction.
static std::string ExtractStringUsingRegex(const std::string& input, const std::regex& regex)
{
    std::smatch matches;
    if (std::regex_match(input, matches, regex) && matches.size() == 2)
        return matches[1];
    return {};
}

// Simple HRESULT → exception helper for MSMF calls
static void ThrowIfFailedMSMF(const char* what, HRESULT hr)
{
    if (FAILED(hr))
    {
        std::stringstream err_stream;
        err_stream << what << "MSMF failed with HResult error: " << hr;
        throw std::runtime_error(err_stream.str());
    }
}

// Destroy the device_info_set or throw runtime_error if failed
static void DestroyDeviceInfoList(HDEVINFO device_info_set)
{
    if (!::SetupDiDestroyDeviceInfoList(device_info_set))
    {
        LOG_ERROR(LOG_TAG, "SetupDiDestroyDeviceInfoList() error 0x%08lX", GetLastError());
        throw std::runtime_error("SetupDiDestroyDeviceInfoList() failed");
    }
}

// Read DEVPKEY_Device_ContainerId from a devnode (DEVINST).
// This is used for COM ports (Ports class devnodes).
static bool GetDevNodeContainerId(DEVINST devInst, GUID& out)
{
    DEVPROPTYPE propType = 0;
    ULONG size = 0;

    // First call: query buffer size and type
    CONFIGRET cr = CM_Get_DevNode_PropertyW(devInst, const_cast<DEVPROPKEY*>(&DEVPKEY_Device_ContainerId), &propType, nullptr, &size, 0);

    if (cr != CR_BUFFER_SMALL || propType != DEVPROP_TYPE_GUID)
        return false;

    // Second call: fetch the GUID value
    std::vector<BYTE> buf(size);
    cr = CM_Get_DevNode_PropertyW(devInst, const_cast<DEVPROPKEY*>(&DEVPKEY_Device_ContainerId), &propType, buf.data(), &size, 0);

    if (cr != CR_SUCCESS || size < sizeof(GUID))
        return false;

    out = *reinterpret_cast<const GUID*>(buf.data());
    return true;
}

// Read DEVPKEY_Device_ContainerId from a device interface path (symbolic link).
// This is used for UVC MF device interfaces (symbolic links).
static bool GetInterfaceContainerId(const wchar_t* interfacePath, GUID& out)
{
    if (!interfacePath || !*interfacePath)
        return false;

    DEVPROPTYPE propType = 0;
    ULONG size = 0;

    // First call: query buffer size and type
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(interfacePath, const_cast<DEVPROPKEY*>(&DEVPKEY_Device_ContainerId), &propType,
                                                     nullptr, &size, 0);

    if (cr != CR_BUFFER_SMALL || propType != DEVPROP_TYPE_GUID)
        return false;

    // Second call: fetch the GUID value
    std::vector<BYTE> buf(size);
    cr = CM_Get_Device_Interface_PropertyW(interfacePath, const_cast<DEVPROPKEY*>(&DEVPKEY_Device_ContainerId), &propType, buf.data(),
                                           &size, 0);

    if (cr != CR_SUCCESS || size < sizeof(GUID))
        return false;

    out = *reinterpret_cast<const GUID*>(buf.data());
    return true;
}

// Return serial ports connected to RealSenseID devices (by VID/PID).
// We enumerate "Ports (COM & LPT)" devnodes and pick only those with known VID/PID.
static std::vector<std::string> DiscoverSerial()
{
    std::vector<std::string> port_names;
    const GUID guid = GUID_DEVCLASS_PORTS;
    HDEVINFO device_info_set = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR(LOG_TAG, "SetupDiGetClassDevs returned invalid handle");
        return port_names;
    }

    SP_DEVINFO_DATA device_info_data = {};
    device_info_data.cbSize = sizeof(device_info_data);

    for (int i = 0; SetupDiEnumDeviceInfo(device_info_set, i, &device_info_data); ++i)
    {
        char device_id_buffer[BUFFER_SIZE] = {};
        DWORD device_id_size = 0;
        auto ok = SetupDiGetDeviceInstanceIdA(device_info_set, &device_info_data, device_id_buffer, BUFFER_SIZE - 1, &device_id_size);
        if (!ok)
        {
            LOG_ERROR(LOG_TAG, "SetupDiGetDeviceInstanceId() error 0x%08lX", GetLastError());
            continue;
        }

        std::string device_id(device_id_buffer);
        std::string vid = ExtractStringUsingRegex(device_id, VID_REGEX);
        std::string pid = ExtractStringUsingRegex(device_id, PID_REGEX);

        if (vid.empty() || pid.empty())
            continue;

        DeviceType deviceType;
        if (!MatchToExpectedVidPidPairs(vid, pid, deviceType))
            continue;

        // Friendly name usually contains "COMx"
        BYTE name_buffer[BUFFER_SIZE] = {};
        DWORD name_size = 0;
        ok = ::SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_FRIENDLYNAME, nullptr, name_buffer,
                                                 BUFFER_SIZE - 1, &name_size);
        if (!ok)
        {
            LOG_ERROR(LOG_TAG, "SetupDiGetDeviceRegistryProperty() error 0x%08lX", GetLastError());
            continue;
        }
        std::string friendly(reinterpret_cast<const char*>(name_buffer));
        std::string com_port = ExtractStringUsingRegex(friendly, COM_PORT_REGEX);
        if (!com_port.empty())
        {
            port_names.push_back(std::move(com_port));
        }
    }
    DestroyDeviceInfoList(device_info_set);
    return port_names;
}

// Internal helper: enumerate RSID UVC capture devices (by VID/PID)
// and record their ContainerId so we can match them to COM ports.
static std::vector<CaptureDeviceEntry> DiscoverCaptureInternal()
{
    std::vector<CaptureDeviceEntry> result;
    IMFAttributes* cap_config = nullptr;
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;

    ThrowIfFailedMSMF("DiscoverCaptureInternal", MFCreateAttributes(&cap_config, 10));
    ThrowIfFailedMSMF("DiscoverCaptureInternal",
                      cap_config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    ThrowIfFailedMSMF("DiscoverCaptureInternal", MFEnumDeviceSources(cap_config, &ppDevices, &count));

    if (count < 1)
    {
        LOG_ERROR(LOG_TAG, "No video devices detected");
        if (cap_config)
            cap_config->Release();
        return result;
    }

    for (UINT32 i = 0; i < count; ++i)
    {
        WCHAR* symLink = nullptr;
        UINT32 cchName = 0;
        HRESULT hr = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symLink, &cchName);
        if (SUCCEEDED(hr) && symLink)
        {
            // Symbolic link string contains the USB PnP ID (with VID/PID)
            char buffer[BUFFER_SIZE];
            snprintf(buffer, sizeof(buffer), "%ls", symLink);
            std::string device_id(buffer);

            std::string vid = ExtractStringUsingRegex(device_id, VID_REGEX);
            std::string pid = ExtractStringUsingRegex(device_id, PID_REGEX);

            DeviceType deviceType;
            if (!vid.empty() && !pid.empty() && MatchToExpectedVidPidPairs(vid, pid, deviceType))
            {
                // For RSID devices, fetch ContainerId for later COM+UVC matching
                GUID containerId {};
                if (GetInterfaceContainerId(symLink, containerId))
                {
                    CaptureDeviceEntry entry;
                    entry.index = static_cast<int>(i);
                    entry.containerId = containerId;
                    result.push_back(entry);
                    LOG_DEBUG(LOG_TAG, "Detected capture device. index=%d", entry.index);
                }
            }

            CoTaskMemFree(symLink);
        }

        ppDevices[i]->Release();
    }

    CoTaskMemFree(ppDevices);
    if (cap_config)
        cap_config->Release();
    return result;
}

// Public API: return MF indices of RSID capture devices (F45x/F50x).
// This now delegates to DiscoverCaptureInternal() and strips ContainerId.
std::vector<int> DiscoverCapture()
{
    std::vector<int> capture_numbers;
    auto entries = DiscoverCaptureInternal();
    capture_numbers.reserve(entries.size());
    for (const auto& e : entries)
        capture_numbers.push_back(e.index);
    return capture_numbers;
}

// Get ContainerId for a given COM port (e.g. "COM3").
// We enumerate Ports devnodes, match by COMx in friendly name, then read devnode ContainerId.
static bool GetComPortContainerId(const char* serial_port, GUID& out)
{
    const GUID guid = GUID_DEVCLASS_PORTS;
    HDEVINFO device_info_set = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        LOG_WARNING(LOG_TAG, "GetComPortContainerId: SetupDiGetClassDevs failed");
        return false;
    }

    SP_DEVINFO_DATA device_info_data = {};
    device_info_data.cbSize = sizeof(device_info_data);

    std::string upper_port(serial_port);
    std::transform(upper_port.begin(), upper_port.end(), upper_port.begin(), ::toupper);
    upper_port = ExtractStringUsingRegex(upper_port, COM_PORT_REGEX);

    bool found = false;

    for (int i = 0; SetupDiEnumDeviceInfo(device_info_set, i, &device_info_data); ++i)
    {
        BYTE name_buffer[BUFFER_SIZE] = {};
        DWORD name_size = 0;
        auto ok = SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_FRIENDLYNAME, nullptr, name_buffer,
                                                    BUFFER_SIZE - 1, &name_size);
        if (!ok)
        {
            LOG_ERROR(LOG_TAG, "GetComPortContainerId: SetupDiGetDeviceRegistryPropertyA() error 0x%08lX", GetLastError());
            continue;
        }

        std::string friendly(reinterpret_cast<const char*>(name_buffer));
        std::string friendly_port = ExtractStringUsingRegex(friendly, COM_PORT_REGEX);

        // When the friendly name's COMx matches the input port, we grab its ContainerId
        if (upper_port == friendly_port)
        {
            if (GetDevNodeContainerId(device_info_data.DevInst, out))
                found = true;
            break;
        }
    }

    DestroyDeviceInfoList(device_info_set);
    return found;
}


// Discover ports and associate each port to device type, S/N and cameraNumber
// cameraNumber: MF capture index that belongs to the same physical device (ContainerId match)
std::vector<DeviceInfo> DiscoverDevices()
{
    std::vector<DeviceInfo> devices;

    auto ports = DiscoverSerial();

    // Pre-discover RSID capture devices with their ContainerId
    std::vector<CaptureDeviceEntry> captureEntries;
    try
    {
        captureEntries = DiscoverCaptureInternal();
    }
    catch (const std::exception& e)
    {
        LOG_WARNING(LOG_TAG, "DiscoverCaptureInternal failed: %s", e.what());
        // continue even if failed here; we will return unknown camera numbers (cameraNumber stays -1)
    }

    // For each serial port, detect DeviceType, map to MF camera index, and query serial number
    for (const auto& port : ports)
    {
        DeviceInfo info;
        errno_t rc = ::strcpy_s(info.serialPort, sizeof(info.serialPort), port.c_str());
        if (rc != 0)
        {
            LOG_ERROR(LOG_TAG, "strcpy_s failed for port %s", port.c_str());
            continue;
        }
        info.deviceType = DiscoverDeviceType(info.serialPort);
        info.cameraNumber = -1;

        if (info.deviceType != DeviceType::Unknown)
        {
            // Try to find camera number by matching COM port ContainerId to capture ContainerId
            GUID comContainer {};
            if (GetComPortContainerId(info.serialPort, comContainer))
            {
                for (const auto& cap : captureEntries)
                {
                    if (IsEqualGUID(comContainer, cap.containerId))
                    {
                        info.cameraNumber = cap.index;
                        break;
                    }
                }
            }

            // Detect serial number via DeviceController
            DeviceController deviceController(info.deviceType);
            auto connect_status = deviceController.Connect({port.c_str()});
            if (connect_status != RealSenseID::Status::Ok)
            {
                LOG_WARNING(LOG_TAG, "Failed to connecting to port %s", port.c_str());
                continue;
            }

            std::string serial_num;
            auto status = deviceController.QuerySerialNumber(serial_num);
            if (status != RealSenseID::Status::Ok)
            {
                LOG_WARNING(LOG_TAG, "Failed to retrieve s/n on port %s", port.c_str());
            }
            else
            {
                errno_t rc = ::strcpy_s(info.serialNumber, sizeof(info.serialNumber), serial_num.c_str());
                if (rc != 0)
                {
                    LOG_ERROR(LOG_TAG, "strcpy_s failed for serial number %s", serial_num.c_str());
                    continue;
                }
            }

            devices.push_back(info);
        }
        else
        {
            LOG_WARNING(LOG_TAG, "Failed to auto detect device type for port: %s", info.serialPort);
        }
    }

    return devices;
}

// Use the port's vid/pid to decide which RealSenseID device is it (F45x, F50x, etc.)
// This re-walks the Ports devnodes and finds the one whose friendly name COMx matches serial_port.
DeviceType DiscoverDeviceType(const char* serial_port)
{
    const GUID guid = GUID_DEVCLASS_PORTS;
    HDEVINFO device_info_set = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        LOG_WARNING(LOG_TAG, "Cannot detect device type");
        return DeviceType::Unknown;
    }

    SP_DEVINFO_DATA device_info_data = {};
    device_info_data.cbSize = sizeof(device_info_data);

    std::string upper_port(serial_port);
    std::transform(upper_port.begin(), upper_port.end(), upper_port.begin(), ::toupper);
    upper_port = ExtractStringUsingRegex(upper_port, COM_PORT_REGEX);

    for (int i = 0; SetupDiEnumDeviceInfo(device_info_set, i, &device_info_data); ++i)
    {
        char device_id_buffer[BUFFER_SIZE] = {};
        DWORD device_id_size = 0;
        auto ok = SetupDiGetDeviceInstanceIdA(device_info_set, &device_info_data, device_id_buffer, BUFFER_SIZE - 1, &device_id_size);
        if (!ok)
        {
            LOG_ERROR(LOG_TAG, "SetupDiGetDeviceInstanceIdA() error 0x%08lX", GetLastError());
            continue;
        }
        std::string device_id(reinterpret_cast<const char*>(device_id_buffer));
        std::string vid = ExtractStringUsingRegex(device_id, VID_REGEX);
        std::string pid = ExtractStringUsingRegex(device_id, PID_REGEX);

        DeviceType deviceType;
        if (!MatchToExpectedVidPidPairs(vid, pid, deviceType))
            continue;

        BYTE name_buffer[BUFFER_SIZE] = {};
        DWORD name_size = 0;
        ok = SetupDiGetDeviceRegistryPropertyA(device_info_set, &device_info_data, SPDRP_FRIENDLYNAME, nullptr, name_buffer,
                                               BUFFER_SIZE - 1, &name_size);
        if (!ok)
        {
            LOG_ERROR(LOG_TAG, "SetupDiGetDeviceRegistryPropertyA() error 0x%08lX", GetLastError());
            continue;
        }

        std::string friendly(reinterpret_cast<const char*>(name_buffer));
        std::string friendly_port = ExtractStringUsingRegex(friendly, COM_PORT_REGEX);

        if (upper_port == friendly_port)
        {
            DestroyDeviceInfoList(device_info_set);
            LOG_INFO(LOG_TAG, "Detected device %s", Description(deviceType));
            return deviceType;
        }
    }

    DestroyDeviceInfoList(device_info_set);
    LOG_WARNING(LOG_TAG, "Cannot detect device type");
    return DeviceType::Unknown;
}

} // namespace RealSenseID
