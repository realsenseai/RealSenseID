// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include "ModuleInfo.h"
#include <cstdint>
#include <string>

namespace RealSenseID
{
namespace FwUpdateF50x
{

// Parses a packaged binary firmware file and returns a list of modules with their metadata
ModuleVector ParseUfifToModules(const std::string& path, const uint32_t block_size);

struct UfifMetadata
{
    uint8_t otpEncryptVersion;
    uint8_t dbVersion;
    uint8_t configVersion;
    uint8_t deviceType;
    uint8_t secureBootEnabled;
};

// Parses all metadata fields from the UFIF header in a single file open.
UfifMetadata ParseUfifMetadata(const std::string& path);


} // namespace FwUpdateF50x
} // namespace RealSenseID