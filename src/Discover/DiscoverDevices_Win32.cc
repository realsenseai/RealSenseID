// License: Apache 2.0. See LICENSE file in root directory.

#include "DiscoverDevicesCommon.h"
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
static constexpr int MAX_PARENT_TRAVERSAL_DEPTH = 5;

// Holds a Media Foundation capture device index + its ContainerId
// This allows us to match UVC streams to COM ports by comparing ContainerIds.
struct CaptureDeviceEntry
{
    int index = -1;      // MF device index
    GUID containerId {}; // Device ContainerId
};

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

// Destroy the device_info_set, log on failure (never throws — this is cleanup)
static void DestroyDeviceInfoList(HDEVINFO device_info_set)
{
    if (!::SetupDiDestroyDeviceInfoList(device_info_set))
    {
        LOG_ERROR(LOG_TAG, "SetupDiDestroyDeviceInfoList() error 0x%08lX", GetLastError());
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

// Holds a discovered COM port and its USB serial number (from the parent USB device's instance ID).
struct SerialPortEntry
{
    std::string portName;
    std::string usbSerialNumber;
    DeviceType deviceType = DeviceType::Unknown;
};

// Walk up the device tree to find the USB device node and extract its iSerialNumber.
//
// The COM port is a child interface of a composite USB device. The serial number is
// on the USB device node, which may be one or more levels up:
//
//   USB\VID_414C&PID_6578\12345                      <-- USB device — serial is here
//     USB\VID_414C&PID_6578&MI_02\A&175A699F&0&0002  <-- COM port interface (devInst)
//
// We walk up via CM_Get_Parent, checking each ancestor's instance ID. We stop when we find
// a node whose ID contains our VID/PID but NOT "&MI_" (i.e. the USB device, not an interface).
// Max depth capped by MAX_PARENT_TRAVERSAL_DEPTH as a safety bound.
//
// Example: ReadUsbSerialNumber(devInst_of_MI_02, "414C", "6578")
//   depth 0: CM_Get_Parent → "USB\VID_414C&PID_6578\12345"
//            has VID_414C&PID_6578, no "&MI_" → extract "12345" → return "12345"
static std::string ReadUsbSerialNumber(DEVINST devInst, const std::string& vid, const std::string& pid)
{
    const std::string vid_pid_pattern = ToUpper("VID_" + vid + "&PID_" + pid);

    DEVINST current = devInst;
    for (int depth = 0; depth < MAX_PARENT_TRAVERSAL_DEPTH; ++depth)
    {
        DEVINST parentInst = 0;
        if (CM_Get_Parent(&parentInst, current, 0) != CR_SUCCESS)
        {
            LOG_WARNING(LOG_TAG, "CM_Get_Parent failed at depth %d", depth);
            break;
        }

        char parent_id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDA(parentInst, parent_id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS)
        {
            LOG_WARNING(LOG_TAG, "CM_Get_Device_IDA failed at depth %d", depth);
            break;
        }

        const std::string id(parent_id);

        // Convert to uppercase for comparison
        const std::string id_upper = ToUpper(id);

        // Check if this is our USB device node: has our VID/PID but is NOT an interface (&MI_)
        if (id_upper.find(vid_pid_pattern) != std::string::npos && id_upper.find("&MI_") == std::string::npos)
        {
            auto last_sep = id.find_last_of('\\');
            if (last_sep != std::string::npos && last_sep + 1 < id.size())
            {
                return id.substr(last_sep + 1);
            }
        }

        // Not found yet — keep walking up
        current = parentInst;
    }

    LOG_WARNING(LOG_TAG, "USB device node not found within %d parent levels", MAX_PARENT_TRAVERSAL_DEPTH);
    return {};
}

// Find all COM ports that belong to RealSenseID devices, along with their device type and serial number.
//
// Windows device tree for a connected RSID device (composite USB):
//
//   USB\VID_414C&PID_6578\12345                      <-- USB device (has iSerialNumber)
//     USB\VID_414C&PID_6578&MI_00\...                <-- Interface 0 (video)
//     USB\VID_414C&PID_6578&MI_02\A&175A699F&0&0002  <-- Interface 2 (COM port)
//       → Friendly name: "USB Serial Device (COM3)"
//
// Algorithm:
//   1. SetupDiGetClassDevs(GUID_DEVCLASS_PORTS) — enumerate all "Ports (COM & LPT)" devnodes.
//   2. For each port, read its device instance ID (e.g. "USB\VID_414C&PID_6578&MI_02\...").
//   3. Extract VID/PID from the instance ID and match against known RSID devices → deviceType.
//   4. Read the friendly name (e.g. "USB Serial Device (COM3)") and extract "COM3".
//   5. Walk up the device tree via ReadUsbSerialNumber to find the USB device node
//      (has our VID/PID but no "&MI_") and extract the serial number from its instance ID.
static std::vector<SerialPortEntry> DiscoverSerial()
{
    std::vector<SerialPortEntry> entries;
    const GUID guid = GUID_DEVCLASS_PORTS;
    HDEVINFO device_info_set = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR(LOG_TAG, "SetupDiGetClassDevs returned invalid handle");
        return entries;
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
            std::string usb_serial = ReadUsbSerialNumber(device_info_data.DevInst, vid, pid);
            entries.push_back({std::move(com_port), std::move(usb_serial), deviceType});
        }
    }
    DestroyDeviceInfoList(device_info_set);
    return entries;
}

// Internal helper: enumerate RSID UVC capture devices (by VID/PID)
// and record their ContainerId so we can match them to COM ports.
static std::vector<CaptureDeviceEntry> DiscoverCaptureInternal()
{
    std::vector<CaptureDeviceEntry> result;
    IMFAttributes* cap_config = nullptr;
    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;

    try
    {
        ThrowIfFailedMSMF("DiscoverCaptureInternal", MFCreateAttributes(&cap_config, 10));
        ThrowIfFailedMSMF("DiscoverCaptureInternal",
                          cap_config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
        ThrowIfFailedMSMF("DiscoverCaptureInternal", MFEnumDeviceSources(cap_config, &ppDevices, &count));
    }
    catch (...)
    {
        if (cap_config)
            cap_config->Release();
        throw;
    }

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

// Look up the Windows ContainerId GUID for a given COM port name (e.g. "COM3").
//
// ContainerId is a GUID that Windows assigns to all interfaces of the same physical USB device.
// Both the COM port and the UVC camera of an RSID device share the same ContainerId, so we use
// it in DiscoverDevices() to pair a serial port with its corresponding MF capture device index.
//
// How it works:
//   1. Enumerate all "Ports (COM & LPT)" devnodes via SetupDiGetClassDevs.
//   2. For each port, read its friendly name (e.g. "USB Serial Device (COM3)").
//   3. Extract "COMx" from the friendly name and compare to the requested port.
//   4. On match, read DEVPKEY_Device_ContainerId from the devnode and return it.
static bool GetComPortContainerId(const char* serial_port, GUID& out)
{
    if (!serial_port)
    {
        LOG_ERROR(LOG_TAG, "GetComPortContainerId: serial_port is null");
        return false;
    }

    const GUID guid = GUID_DEVCLASS_PORTS;
    HDEVINFO device_info_set = SetupDiGetClassDevs(&guid, nullptr, nullptr, DIGCF_PRESENT);
    if (device_info_set == INVALID_HANDLE_VALUE)
    {
        LOG_WARNING(LOG_TAG, "GetComPortContainerId: SetupDiGetClassDevs failed");
        return false;
    }

    SP_DEVINFO_DATA device_info_data = {};
    device_info_data.cbSize = sizeof(device_info_data);

    std::string upper_port = ToUpper(serial_port);
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


// Discover all connected RealSenseID devices and return a DeviceInfo for each one.
//
// Each RSID device is a composite USB device that exposes both a COM port (serial commands)
// and a UVC camera (video frames). This function finds them both and pairs them:
//
//   1. DiscoverSerial() enumerates COM ports, filtering by known RSID VID/PID pairs.
//      Each result includes the port name ("COM3"), device type (F45x/F50x), and USB serial number.
//
//   2. DiscoverCaptureInternal() enumerates Media Foundation video capture devices,
//      filtering by RSID VID/PID. Each result includes the MF device index and its ContainerId.
//
//   3. For each COM port, we look up its ContainerId (via GetComPortContainerId) and match it
//      to a capture device with the same ContainerId. This gives us the MF camera index
//      (cameraNumber) that belongs to the same physical device.
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

    // For each serial port, map to MF camera index and copy serial number into DeviceInfo.
    // Note: DiscoverSerial() only returns ports with a known VID/PID, so deviceType is never Unknown here.
    for (const auto& entry : ports)
    {
        DeviceInfo info = {};
        strncpy(info.serialPort, entry.portName.c_str(), sizeof(info.serialPort) - 1);
        info.serialPort[sizeof(info.serialPort) - 1] = '\0';
        info.deviceType = entry.deviceType;
        info.cameraNumber = -1;

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

        // Use the USB serial number from the device instance ID (set by firmware in the USB iSerialNumber descriptor).
        if (!entry.usbSerialNumber.empty())
        {
            strncpy(info.serialNumber, entry.usbSerialNumber.c_str(), sizeof(info.serialNumber) - 1);
            info.serialNumber[sizeof(info.serialNumber) - 1] = '\0';
        }
        else
        {
            LOG_WARNING(LOG_TAG, "No USB serial number for port %s", entry.portName.c_str());
        }

        devices.push_back(info);
    }

    return devices;
}

// Detect device type (F45x / F50x / Unknown) for the given serial port.
// Delegates to DiscoverSerial() and finds the matching port entry.
DeviceType DiscoverDeviceType(const char* serial_port)
{
    if (serial_port == nullptr)
    {
        LOG_ERROR(LOG_TAG, "DiscoverDeviceType: serial_port is null");
        return DeviceType::Unknown;
    }

    const std::string target = ExtractStringUsingRegex(ToUpper(serial_port), COM_PORT_REGEX);
    for (const auto& entry : DiscoverSerial())
    {
        const std::string port = ToUpper(entry.portName);
        if (port == target)
        {
            LOG_INFO(LOG_TAG, "Detected device %s", Description(entry.deviceType));
            return entry.deviceType;
        }
    }

    LOG_WARNING(LOG_TAG, "Cannot detect device type");
    return DeviceType::Unknown;
}

} // namespace RealSenseID
