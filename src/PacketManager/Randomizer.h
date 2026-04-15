// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include <random>

namespace RealSenseID
{
namespace PacketManager
{
class Randomizer
{
public:
    void GenerateRandom(unsigned char* outBuffer, size_t length);
    static Randomizer& Instance();

private:
    Randomizer() = default;
    std::random_device _random_device;
};
} // namespace PacketManager
} // namespace RealSenseID