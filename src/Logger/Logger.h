// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include <functional>
#include <memory>
#include "spdlog/fwd.h"


namespace spdlog
{
class logger;
} // namespace spdlog

namespace RealSenseID
{
// Logger singleton with user callback support
// It can output to any combination of:
// * User provided callback function.
// * Standard output if RSID_DEBUG_CONSOLE is defined.
// * "rsid_debug.log" file if RSID_DEBUG_FILE is defined.
class Logger
{
public:
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

    Logger(Logger const&) = delete;
    void operator=(Logger const&) = delete;

    static Logger& Instance()
    {
        static Logger instance;
        return instance;
    }

    // Set callback for given level. replace current callback if already exists
    void SetCallback(LogCallback callback, LogLevel level, bool do_formatting);

    void Trace(const char* tag, const char* format, ...);
    void Debug(const char* tag, const char* format, ...);
    void Info(const char* tag, const char* format, ...);
    void Warning(const char* tag, const char* format, ...);
    void Error(const char* tag, const char* format, ...);
    void Critical(const char* tag, const char* format, ...);
    // Debug bytes in hex format
    void DebugBytes(const char* tag, const char* msg, const char* buffer, size_t size);

private:
    spdlog::logger* _logger = nullptr;

    Logger();
    ~Logger();

    bool is_callback_set = false;
};
} // namespace RealSenseID

#define LOG_TRACE(...)         RealSenseID::Logger::Instance().Trace(__VA_ARGS__)
#define LOG_DEBUG(...)         RealSenseID::Logger::Instance().Debug(__VA_ARGS__)
#define LOG_INFO(...)          RealSenseID::Logger::Instance().Info(__VA_ARGS__)
#define LOG_WARNING(...)       RealSenseID::Logger::Instance().Warning(__VA_ARGS__)
#define LOG_ERROR(...)         RealSenseID::Logger::Instance().Error(__VA_ARGS__)
#define LOG_CRITICAL(...)      RealSenseID::Logger::Instance().Critical(__VA_ARGS__)
#define LOG_EXCEPTION(tag, ex) RealSenseID::Logger::Instance().Error(tag, "%s", ex.what())


#ifdef RSID_DEBUG_SERIAL
#define DEBUG_SERIAL(tag, msg, buf, size) RealSenseID::Logger::Instance().DebugBytes(tag, msg, buf, size)
#else
#define DEBUG_SERIAL(...) (void)0
#endif // RSID_DEBUG_SERIAL