// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "Logger.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/base_sink.h"
#include "spdlog/fmt/bin_to_hex.h"
#include "spdlog/sinks/basic_file_sink.h"
#include <memory>
#include <cstdarg> // for va_start
#include <cassert>


#ifdef __ANDROID__
#include "spdlog/sinks/android_sink.h"
#else
#include "spdlog/sinks/stdout_color_sinks.h"
#endif // __ANDROID__

#define LOG_BUFFER_SIZE 512


namespace RealSenseID
{
class UserCallbackSink : public spdlog::sinks::base_sink<std::mutex>
{
    Logger::LogCallback _clbk;

public:
    UserCallbackSink(Logger::LogCallback clbk, Logger::LogLevel min_level, bool do_formatting) : _clbk {clbk}
    {
        auto spdlog_level = static_cast<spdlog::level::level_enum>(min_level);
        set_level(spdlog_level);
        const char* pattern = do_formatting ? "%+" : "%v";
        this->base_sink<std::mutex>::set_pattern_(pattern);
    }

    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        formatted.push_back('\0'); // convert to c_str
        auto clbk_level = static_cast<Logger::LogLevel>(msg.level);
        _clbk(clbk_level, (const char*)formatted.data());
    };

    // make sure Logger::LogLevels and spdlog::levels values match
    static_assert(static_cast<int>(Logger::LogLevel::Trace) == static_cast<int>(spdlog::level::trace), "neq");
    static_assert(static_cast<int>(Logger::LogLevel::Debug) == static_cast<int>(spdlog::level::debug), "neq");
    static_assert(static_cast<int>(Logger::LogLevel::Info) == static_cast<int>(spdlog::level::info), "neq");
    static_assert(static_cast<int>(Logger::LogLevel::Warning) == static_cast<int>(spdlog::level::warn), "neq");
    static_assert(static_cast<int>(Logger::LogLevel::Critical) == static_cast<int>(spdlog::level::critical), "neq");
    static_assert(static_cast<int>(Logger::LogLevel::Off) == static_cast<int>(spdlog::level::off), "neq");

    void flush_() override {};
};

Logger::Logger()
{
    _logger = new spdlog::logger("");
    auto& sinks = _logger->sinks();
    sinks.clear();
    _logger->set_level(spdlog::level::off);

#ifdef RSID_DEBUG_CONSOLE
#ifdef __ANDROID__
    sinks.push_back(std::make_shared<spdlog::sinks::android_sink_mt>("RSID", true));
#else
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
#endif // __ANDROID__
    _logger->set_level(spdlog::level::debug);
#endif // RSID_DEBUG_CONSOLE

#ifdef RSID_DEBUG_FILE
    const std::string logfile = "rsid_debug.log";
    try
    {
        auto truncate = true;
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(logfile, truncate));
        _logger->set_level(spdlog::level::debug);
        _logger->flush_on(spdlog::level::debug); // flush each log message immediately to the file.
    }
    catch (const std::exception& ex)
    {
        printf("Failed to create log file \"%s\": %s\n", logfile.c_str(), ex.what());
    }
#endif // RSID_DEBUG_FILE
}

Logger::~Logger()
{
    delete _logger;
}

// set log callback sink. replace exiting one if already exists
void Logger::SetCallback(LogCallback clbk, LogLevel level, bool do_formatting)
{
    auto& sinks = _logger->sinks();
    if (is_callback_set)
    {
        // the last sink is the callback sink - replace with new one
        assert(!sinks.empty());
        if (!sinks.empty())
        {
            sinks.pop_back();
        }
        is_callback_set = false;
    }

    sinks.push_back(std::make_shared<UserCallbackSink>(clbk, level, do_formatting));
    is_callback_set = true;
    auto required_spdlog_level = static_cast<spdlog::level::level_enum>(level);
    if (!_logger->should_log(required_spdlog_level))
    {
        _logger->set_level(required_spdlog_level);
    }
}


// if log level is right, vsprintf the args to buffer and log it
#define LOG_IT_(LEVEL)                                                                                                                     \
    va_list args;                                                                                                                          \
    if (!_logger->should_log(LEVEL))                                                                                                       \
        return;                                                                                                                            \
    va_start(args, format);                                                                                                                \
    char buffer[LOG_BUFFER_SIZE];                                                                                                          \
    auto Ok = vsnprintf(buffer, sizeof(buffer), format, args) >= 0;                                                                        \
    if (!Ok)                                                                                                                               \
        snprintf(buffer, sizeof(buffer), "(bad printf format \"%s\")", format);                                                            \
    _logger->log(LEVEL, "[{}] {}", tag, buffer);                                                                                           \
    va_end(args)


void Logger::Trace(const char* tag, const char* format, ...)
{
    LOG_IT_(spdlog::level::trace);
}

void Logger::Debug(const char* tag, const char* format, ...)
{
    LOG_IT_(spdlog::level::debug);
}

void Logger::Info(const char* tag, const char* format, ...)
{
    LOG_IT_(spdlog::level::info);
}

void Logger::Warning(const char* tag, const char* format, ...)
{
    LOG_IT_(spdlog::level::warn);
}

void Logger::Error(const char* tag, const char* format, ...)
{
    LOG_IT_(spdlog::level::err);
}

void Logger::Critical(const char* tag, const char* format, ...)
{
    LOG_IT_(spdlog::level::critical);
}

void Logger::DebugBytes(const char* tag, const char* msg, const char* buf, size_t size)
{
    _logger->debug("[{}] {} {} bytes {:pa}\n", tag, msg, size, spdlog::to_hex(buf, &buf[size]));
}
} // namespace RealSenseID
