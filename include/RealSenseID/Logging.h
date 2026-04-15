// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"
#include <functional>

/**
 *  RealSenseID offers the SetLogCallback() function to get logging events.
 *  In addition, for debugging purposes, one can use the following cmake options:
 *    cmake -DRSID_DEBUG_CONSOLE=ON - to activate colored debug output to stdout.
 *    cmake -DRSID_DEBUG_FILE=ON - to activate debug output to "rsid_debug.log" file.
 */
namespace RealSenseID
{
/**
 * Log severity from lowest(Debug) to highest (critical)
 */
enum class LogLevel
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Critical,
    Off
};

using LogCallback = std::function<void(LogLevel level, const char* msg)>;

/**
 * Use the given callback to get log messages from the library (default off).
 * @param callback[in] function to be called for each log entry.
 * @param min_level[in] minimum log level which would trigger this callback.
 * @param do_formatting[in] set to true to get full formatted message, false to get the bare message.
 */
RSID_API void SetLogCallback(LogCallback callback, LogLevel min_level, bool do_formatting);

} // namespace RealSenseID
