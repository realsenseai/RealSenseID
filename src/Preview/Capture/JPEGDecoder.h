// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.

#pragma once

namespace RealSenseID
{
namespace Capture
{

struct buffer
{
    unsigned char* data = nullptr;
    unsigned int size = 0;
    unsigned int offset = 0;
};

} // namespace Capture
} // namespace RealSenseID