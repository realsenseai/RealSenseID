// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include "FwUpdaterCommF45x.h"
#include "RealSenseID/SerialConfig.h"
#include "ModuleInfo.h"
#include <string>
#include <functional>
#include <vector>

namespace RealSenseID
{
namespace FwUpdateF45x
{
class FwUpdateEngineF45x
{
public:
    using ProgressCallback = std::function<void(float)>;
    using ProgressTick = std::function<void()>;
    using Buffer = std::vector<unsigned char>;

    struct Settings
    {
        SerialConfig serial_config;
        static const long DefaultBaudRate = 115200;
        long baud_rate = DefaultBaudRate;

        std::string fw_filename;
        bool force_full = false; // if true update all modules and blocks regardless of crc checks
    };

    FwUpdateEngineF45x() = default;
    ~FwUpdateEngineF45x() = default;
    ModuleVector ModulesFromFile(const std::string& filename);
    void BurnModules(const Settings& settings, const ModuleVector& modules, ProgressCallback on_progress);

private:
    static constexpr const uint32_t BlockSize = 512 * 1024;
    std::unique_ptr<FwUpdaterCommF45x> _comm;

    struct ModuleVersionInfo;
    std::vector<ModuleVersionInfo> ModulesFromDevice();
    ModuleVersionInfo ModuleFromDevice(const std::string& module_name);
    void CleanObsoleteModules(const std::vector<ModuleInfo>& file_modules, const std::vector<ModuleVersionInfo>& device_modules);
    void InitNewModules(const std::vector<ModuleInfo>& file_modules, const std::vector<ModuleVersionInfo>& device_modules);
    void BurnSelectModules(const ModuleVector& modules, ProgressTick tick, bool force_full);
    void BurnModule(ProgressTick tick, const ModuleInfo& module, const Buffer& buffer, bool is_first, bool is_last, bool force_full);
    std::vector<bool> GetBlockUpdateList(const ModuleInfo& module, bool force_full);
    bool ParseDlResponse(const std::string& name, size_t blkNo, size_t sz);
    bool ParseDlBlockResult();
};
} // namespace FwUpdateF45x
} // namespace RealSenseID
