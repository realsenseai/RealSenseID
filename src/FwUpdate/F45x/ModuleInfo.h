// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include <string>
#include <vector>
#include <cinttypes>

namespace RealSenseID
{
namespace FwUpdateF45x
{
// each module consists of n blocks, each with offset, size and crc
struct BlockInfo
{
    size_t offset; // block offset from module start
    size_t size;   // block size
    uint32_t crc;  // block crc
};

struct ModuleInfo
{
    std::string filename;          // path to module file
    size_t file_offset = 0;        // module start offset in file
    size_t size = 0;               // actual data size
    size_t aligned_size = 0;       // aligned data size
    std::string name;              // module name
    std::string version;           // module version
    uint32_t crc = 0;              // crc of entire module
    std::vector<BlockInfo> blocks; // block specific data
};

using ModuleVector = std::vector<ModuleInfo>;
} // namespace FwUpdateF45x
} // namespace RealSenseID