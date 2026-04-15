// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
/**
 * Serial config
 */
struct RSID_API SerialConfig
{
#ifndef __ANDROID__
    const char* port = nullptr;
#else
    int fileDescriptor = -1;
    int readEndpoint = -1;
    int writeEndpoint = -1;
#endif
};
} // namespace RealSenseID
