// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include "FwUpdaterCommF50x.h"
#include "ModuleInfo.h"
#include <string>
#include <functional>
#include <vector>

namespace RealSenseID
{
namespace FwUpdateF50x
{
class FwUpdateEngineF50x
{
public:
    using ProgressCallback = std::function<void(float)>;
    using ProgressTick = std::function<void()>;
    using Buffer = std::vector<unsigned char>;

    struct Settings
    {
        SerialConfig serial_config;
        static constexpr long DefaultBaudRate = 115200;

        std::string fw_filename;
        long baud_rate = DefaultBaudRate;
        bool force_full = false; // if true update all modules and blocks regardless of crc checks
    };

    FwUpdateEngineF50x() = default;
    ~FwUpdateEngineF50x() = default;
    ModuleVector ModulesFromFile(const std::string& filename);
    void BurnModules(const Settings& settings, const ModuleVector& modules, ProgressCallback on_progress);

private:
    static constexpr const uint32_t BlockSize = 512 * 1024;
    std::unique_ptr<FwUpdaterCommF50x> _comm;

    void BurnSelectModules(const ModuleVector& modules, ProgressTick tick, bool force_full);
    bool FindDirtyModules(const ModuleVector& modules);
    void BurnModule(ProgressTick tick, const ModuleInfo& module, const Buffer& buffer, bool force_full);
    bool ShouldUpdate(const ModuleInfo& module);
    bool ParseDlResponse(const std::string& name, size_t blkNo, size_t sz);
    bool ParseDlBlockResult();
};
} // namespace FwUpdateF50x
} // namespace RealSenseID
