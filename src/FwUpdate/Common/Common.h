// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace RealSenseID
{
namespace FwUpdateCommon
{

uint32_t CalculateCRC(uint32_t crc, const void* buffer, uint32_t buffer_size);
std::vector<unsigned char> LoadFileToBuffer(const std::string& path, size_t aligned_size, size_t size, size_t offset);

} // namespace FwUpdateCommon
} // namespace RealSenseID
