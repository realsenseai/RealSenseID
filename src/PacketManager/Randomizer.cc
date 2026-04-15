// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "Randomizer.h"

#include <limits.h>

static std::uniform_int_distribution<> distrib(0, UCHAR_MAX);

namespace RealSenseID
{
namespace PacketManager
{
Randomizer& Randomizer::Instance()
{
    static Randomizer s_instance;
    return s_instance;
}

void Randomizer::GenerateRandom(unsigned char* outBuffer, size_t length)
{
    std::mt19937 gen(_random_device());
    for (size_t i = 0; i < length; i++)
    {
        outBuffer[i] = (unsigned char)(distrib(gen));
    }
}
} // namespace PacketManager
} // namespace RealSenseID