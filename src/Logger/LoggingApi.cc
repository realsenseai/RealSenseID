// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/Logging.h"
#include "Logger/Logger.h"


namespace RealSenseID
{
void SetLogCallback(LogCallback user_callback, LogLevel level, bool do_formatting)
{
    // callback wrapper
    // cast the inner received Logger::LogLevel to the public RealSenseID::LogLevel and call user callback.
    Logger::LogCallback clbk_wrapper = [user_callback](Logger::LogLevel logger_level, const char* msg) {
        user_callback(static_cast<RealSenseID::LogLevel>(logger_level), msg);
    };

    // cast public RealSenseID::LogLevel to the inner Logger::LogLevel and register the callback wrapper.
    auto logger_level = static_cast<Logger::LogLevel>(level);
    Logger::Instance().SetCallback(clbk_wrapper, logger_level, do_formatting);
}

} // namespace RealSenseID
