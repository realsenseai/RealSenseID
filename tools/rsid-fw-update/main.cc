// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#include "RealSenseID/FwUpdater.h"
#include "RealSenseID/DeviceController.h"
#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/Version.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <exception>
#include <cstring>
#include <utility>
#include <vector>
#include <thread>
#include <chrono>
#include <cctype>
#include <stdlib.h>

static const std::string OPFW = "OPFW";

struct DeviceMetadata
{
    std::string serial_number = "Unknown";
    std::string fw_version = "Unknown";
};

struct FullDeviceInfo
{
    std::unique_ptr<DeviceMetadata> metadata;
    std::unique_ptr<RealSenseID::DeviceInfo> device_info;
};

/* User interaction */
static int UserDeviceSelection(const std::vector<FullDeviceInfo>& devices)
{
    std::cout << "Detected devices:\n";
    for (size_t i = 0; i < devices.size(); ++i)
    {
        const auto& device = devices.at(i);
        std::cout << " " << i + 1 << ") S/N: " << device.metadata->serial_number << ", Device: " << device.device_info->deviceType
                  << ", FW: " << device.metadata->fw_version << ", Port: " << device.device_info->serialPort << "\n";
    }

    int device_index = -1;

    while (device_index < 1 || device_index > static_cast<int>(devices.size()))
    {
        std::cout << "> ";

        std::string line;
        if (!std::getline(std::cin, line))
        {
            std::cout << "Input aborted." << std::endl;
            std::exit(EXIT_FAILURE);
        }

        try
        {
            device_index = std::stoi(line);
        }
        catch (...)
        {
            device_index = -1;
        }
    }

    std::cout << "\n";

    return device_index - 1;
}

// wait for y/n
static bool UserApproval()
{
    std::string line;
    while (true)
    {
        std::cout << "Proceed with update? (y/n)\n > ";
        if (!std::getline(std::cin, line))
            return false;

        if (line.empty())
            continue;
        char key = static_cast<char>(std::tolower(line[0]));
        if (key == 'y')
            return true;
        if (key == 'n')
            return false;
    }
}

// Extracts the version substring of a specific module (e.g., "OPFW:") from a '|' delimited firmware version string.
// Returns "Unknown" if the module is not found or the format is invalid.
static std::string ExtractModuleFromVersion(const std::string& module_name, const std::string& full_version)
{
    std::stringstream version_stream(full_version);
    std::string section;
    while (std::getline(version_stream, section, '|'))
    {
        if (section.find(module_name) != std::string::npos)
        {
            auto pos = section.find(':');
            auto sub = section.substr(pos + 1, std::string::npos);
            return sub;
        }
    }
    return "Unknown";
}

static std::string ParseFirmwareVersion(const std::string& full_version)
{
    return ExtractModuleFromVersion("OPFW:", full_version);
}

/* Command line arguments */
struct CommandLineArgs
{
    bool is_valid = false;                                                  // was parsing successful
    bool force_version = false;                                             // force non-compatible versions
    bool force_full = false;                                                // force update of all modules even if already exist in the fw
    bool is_interactive = false;                                            // ask user for confirmation before starting
    bool auto_approve = false;                                              // automatically approve all (use default params)
    std::string fw_file;                                                    // path to firmware update binary
    std::string serial_port;                                                // serial port
    RealSenseID::DeviceType device_type = RealSenseID::DeviceType::Unknown; // device type is auto-detected by default. user can override
};

static RealSenseID::DeviceType toDeviceType(const char* input)
{
    if (strcmp(input, "F45x") == 0 || strcmp(input, "f45x") == 0)
        return RealSenseID::DeviceType::F45x;
    if (strcmp(input, "F50x") == 0 || strcmp(input, "f50x") == 0)
        return RealSenseID::DeviceType::F50x;
    return RealSenseID::DeviceType::Unknown;
}

static void PrintUsage(const char* program_name)
{
    std::cout << "usage: " << program_name
              << " --file <bin path> [--port <COM#>] [--force-version] [--force-full] [--device-type <<F45x/F50x>>] [--interactive] "
                 "[--auto-approve] [--help]\n";
}

static CommandLineArgs ParseCommandLineArgs(int argc, char* argv[])
{
    CommandLineArgs args;

    if (argc < 2)
    {
        PrintUsage(argv[0]);
        exit(EXIT_SUCCESS);
    }

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--file") == 0)
        {
            if (i + 1 < argc)
                args.fw_file = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0)
        {
            if (i + 1 < argc)
                args.serial_port = argv[++i];
        }
        else if (strcmp(argv[i], "--device-type") == 0)
        {
            if (i + 1 < argc)
            {
                args.device_type = toDeviceType(argv[++i]);
                if (args.device_type == RealSenseID::DeviceType::Unknown)
                {
                    std::cerr << "Unknown device type. Should be F45x/F50x\n";
                    exit(EXIT_FAILURE);
                }
            }
        }
        else if (strcmp(argv[i], "--force-full") == 0)
        {
            args.force_full = true;
        }
        else if (strcmp(argv[i], "--force-version") == 0)
        {
            args.force_version = true;
        }
        else if (strcmp(argv[i], "--interactive") == 0)
        {
            args.is_interactive = true;
        }
        else if (strcmp(argv[i], "--auto-approve") == 0)
        {
            args.auto_approve = true;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            PrintUsage(argv[0]);
            exit(EXIT_SUCCESS);
        }
    }

    // Make sure all required options are available.
    if (args.fw_file.empty())
    {
        return args;
    }

    if (args.is_interactive && args.auto_approve)
    {
        std::cout << "--is-interactive and --auto-approve flags do not co-exist. Choose either or none." << std::endl;
        return args;
    }
    args.is_valid = true;
    return args;
}

static DeviceMetadata QueryDeviceMetadata(const RealSenseID::SerialConfig& serial_config, RealSenseID::DeviceType device_type)
{
    DeviceMetadata metadata;
    if (device_type == RealSenseID::DeviceType::Unknown)
    {
        device_type = RealSenseID::DiscoverDeviceType(serial_config.port);
    }

    RealSenseID::DeviceController device_controller(device_type);

    if (device_controller.Connect(serial_config) != RealSenseID::Status::Ok)
    {
        throw std::runtime_error("Failed to connect to device");
    }

    std::string fw_version;
    auto status = device_controller.QueryFirmwareVersion(fw_version);
    if (status == RealSenseID::Status::Ok)
    {
        metadata.fw_version = ParseFirmwareVersion(fw_version);
    }

    std::string serial_number;
    device_controller.QuerySerialNumber(serial_number);
    if (!serial_number.empty())
    {
        metadata.serial_number = serial_number;
    }

    device_controller.Disconnect();
    return metadata;
}

static bool DetectDevices(std::vector<FullDeviceInfo>& out_devices_info)
{
    std::cout << "Using device auto detection...\n";
    out_devices_info.clear();
    auto detected_devices = RealSenseID::DiscoverDevices();
    for (const auto& detected_device : detected_devices)
    {
        auto metadata = QueryDeviceMetadata(RealSenseID::SerialConfig {detected_device.serialPort}, detected_device.deviceType);
        FullDeviceInfo device {std::make_unique<DeviceMetadata>(metadata), std::make_unique<RealSenseID::DeviceInfo>(detected_device)};
        out_devices_info.push_back(std::move(device));
    }

    if (out_devices_info.empty())
    {
        std::cerr << "No devices found!\n";
        return false;
    }
    return true;
}


struct FwUpdaterCliEventHandler : public RealSenseID::FwUpdater::EventHandler
{
public:
    FwUpdaterCliEventHandler() = default;
    void OnProgress(float progress) override
    {
        static int freq = 0;
        if (freq++ % 10 == 0 || progress >= 1)
        {
            auto percents = static_cast<int>(progress * 100);
            std::cout << "--------------- Completed " << percents << " % ---------------\n";
        }
    }
};

int main(int argc, char* argv[])
{
    try
    {
        // parse cli args
        auto args = ParseCommandLineArgs(argc, argv);
        if (!args.is_valid)
        {
            return EXIT_FAILURE;
        }

        // check if firmware file exists
        if (!std::ifstream(args.fw_file))
        {
            std::cerr << "Firmware file not found: " << args.fw_file << "\n";
            return EXIT_FAILURE;
        }

        // populate device list
        FullDeviceInfo selected_device;
        bool auto_detect = args.serial_port.empty();
        if (auto_detect)
        {
            std::vector<FullDeviceInfo> devices_info;
            if (!DetectDevices(devices_info))
            {
                return EXIT_FAILURE;
            }
            // if more than one device exists - ask user to select
            auto index = devices_info.size() == 1 ? 0 : UserDeviceSelection(devices_info);
            selected_device.device_info = std::move(devices_info.at(index).device_info);
            selected_device.metadata = std::move(devices_info.at(index).metadata);
        }
        else
        {
            std::cout << "Using manual device selection...\n\n";
            auto metadata = QueryDeviceMetadata(RealSenseID::SerialConfig {args.serial_port.c_str()}, args.device_type);
            auto device_info = std::make_unique<RealSenseID::DeviceInfo>();
            if (args.serial_port.length() >= RealSenseID::DeviceInfo::MaxBufferSize)
            {
                std::cerr << "Invalid serial port - too long\n";
                return EXIT_FAILURE;
            }
            ::memcpy(device_info->serialPort, args.serial_port.c_str(), sizeof(device_info->serialPort) - 1);
            device_info->serialPort[sizeof(device_info->serialPort) - 1] = '\0';

            // auto detect device type if user didn't provide one
            if (args.device_type == RealSenseID::DeviceType::Unknown)
            {
                device_info->deviceType = RealSenseID::DiscoverDeviceType(args.serial_port.c_str());
            }
            else
            {
                device_info->deviceType = args.device_type;
            }

            selected_device = {std::make_unique<DeviceMetadata>(metadata), std::move(device_info)};
        }

        if (selected_device.device_info->deviceType == RealSenseID::DeviceType::Unknown)
        {
            std::cerr << "Failed to detect device type for port " << args.serial_port << "\n";
            return EXIT_FAILURE;
        }

        // extract fw version from update file
        RealSenseID::FwUpdater fw_updater(selected_device.device_info->deviceType);

        const char* bin_path = args.fw_file.c_str();

        std::string new_fw_version;
        std::string new_recognition_version;
        std::vector<std::string> moduleNames;
        auto is_valid = fw_updater.ExtractFwInformation(bin_path, new_fw_version, new_recognition_version, moduleNames);

        if (!is_valid)
        {
            std::cerr << "Invalid firmware file !\n";
            return EXIT_FAILURE;
        }

        RealSenseID::FwUpdater::Settings settings;
        settings.serial_config = RealSenseID::SerialConfig({selected_device.device_info->serialPort});
        settings.force_full = args.force_full;

        // Check all compatibility fields in a single device connection.
        RealSenseID::FwUpdater::FwCompatibilityInfo compatInfo;
        fw_updater.CheckCompatibility(settings, bin_path, compatInfo);

        if (!compatInfo.IsOtpSkuCompatible())
        {
            if (compatInfo.deviceOtpSku < 0)
                std::cerr << "Failed to determine OTP encryption SKU. Check device connection and try again.\n";
            else
                std::cerr << "Firmware mismatch.\nReplace firmware binary to SKU" << compatInfo.deviceOtpSku << "\n";
            return EXIT_FAILURE;
        }

        if (!compatInfo.IsSecureBootCompatible())
        {
            if (compatInfo.deviceSecureBoot < 0)
                std::cerr << "Failed to determine secure boot variant. Check device connection and try again.\n";
            else
                std::cerr << "Firmware mismatch.\nReplace firmware binary to " << (compatInfo.deviceSecureBoot ? "SIGNED" : "NON SIGNED")
                          << "\n";
            return EXIT_FAILURE;
        }

        if (!compatInfo.IsDeviceTypeCompatible())
        {
            auto deviceTypeName = [](int t) -> const char* {
                if (t == 0)
                    return "F45x";
                if (t == 1)
                    return "F46x";
                if (t == 2)
                    return "F50x";
                return "Unknown";
            };
            std::cerr << "Firmware device type mismatch: connected device (" << deviceTypeName(compatInfo.connectedDeviceType)
                      << ") does not match the selected firmware (" << deviceTypeName(compatInfo.expectedDeviceType) << ").\n";
            std::cerr << "Please use a firmware file that matches your device type.\n";
            return EXIT_FAILURE;
        }

        if (compatInfo.deviceDbVer >= 0 && compatInfo.expectedDbVer >= 0 && compatInfo.deviceDbVer != compatInfo.expectedDbVer)
        {
            std::cerr << "Warning: DB version mismatch (device: v" << compatInfo.deviceDbVer << ", firmware: v" << compatInfo.expectedDbVer
                      << ").\n";
            std::cerr << "The device database may be erased after the update. Export it first if needed.\n\n";

            if (!args.auto_approve)
            {
                std::string line;
                while (true)
                {
                    std::cout << "Proceed with update anyway? (y/n)\n > ";
                    if (!std::getline(std::cin, line))
                        return EXIT_FAILURE;
                    if (line.empty())
                        continue;
                    char key = static_cast<char>(std::tolower(line[0]));
                    if (key == 'y')
                        break;
                    if (key == 'n')
                        return EXIT_FAILURE;
                }
                std::cout << "\n";
            }
        }

        // check compatibility with host
        const auto current_device_type = selected_device.device_info->deviceType;
        const auto& current_fw_version = selected_device.metadata->fw_version;
        const auto current_compatible = RealSenseID::IsFwCompatibleWithHost(current_device_type, current_fw_version);
        const auto new_compatible = RealSenseID::IsFwCompatibleWithHost(current_device_type, new_fw_version);

        // show summary to user - update path, compatibility checks
        std::cout << "\n";
        std::cout << "Summary:\n";
        std::cout << " * Device type: " << selected_device.device_info->deviceType << "\n";
        std::cout << " * Serial number: " << selected_device.metadata->serial_number << "\n";
        std::cout << " * Serial port: " << selected_device.device_info->serialPort << "\n";
        std::cout << " * " << (current_compatible ? "Compatible" : "Incompatible") << " with current device firmware\n";
        std::cout << " * " << (new_compatible ? "Compatible" : "Incompatible") << " with new device firmware\n";
        std::cout << " * Firmware update path:\n";
        std::cout << "     * OPFW: " << current_fw_version << " -> " << new_fw_version << "\n";
        std::cout << "\n";

        // ask user for approval if interactive
        if (args.is_interactive)
        {
            bool user_agreed = UserApproval();
            if (!user_agreed)
                return EXIT_FAILURE;
            std::cout << "\n";
        }

        // allow bypass of incompatible version if forced
        if (!new_compatible && !args.force_version)
        {
            std::cerr << "Version is incompatible with the current host version!\n";
            std::cerr << "Use --force-version to force the update.\n ";
            return EXIT_FAILURE;
        }

        FwUpdaterCliEventHandler event_handler;
        auto status = fw_updater.UpdateModules(&event_handler, settings, bin_path);
        auto success = status == RealSenseID::Status::Ok;
        std::cout << "\n\n";
        std::cout << "Firmware update" << (success ? " finished successfully " : " failed ") << "\n";

        return success ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Exception occurred: " << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
}
