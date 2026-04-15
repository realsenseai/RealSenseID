// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include <cstdint>
#include <cstddef>

namespace RealSenseID
{
namespace PacketManager
{
uint16_t Crc16(uint16_t initial_crc, const char* buffer, std::size_t bufferSize);
uint16_t Crc16(const char* buffer, std::size_t bufferSize);
} // namespace PacketManager
} // namespace RealSenseID
