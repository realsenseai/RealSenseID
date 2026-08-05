// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "DeviceControllerImpl.h"
#include "StatusHelper.h"
#include "PacketManager/SerialPacket.h"
#include "PacketManager/PacketSender.h"
#include "PacketManager/Randomizer.h"
#include "Logger.h"
#include "RealSenseID/DiscoverDevices.h"

#include <sstream>
#include <regex>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <stdexcept>

#ifdef _WIN32
#include "PacketManager/WindowsSerial.h"
#elif defined(__ANDROID__)
#include "PacketManager/AndroidSerial.h"
#elif defined(__linux__) || defined(__APPLE__)
#include "PacketManager/LinuxSerial.h"
#else
#error "Platform not supported"
#endif //_WIN32

static const char* LOG_TAG = "DeviceControllerImpl";

namespace RealSenseID
{

DeviceControllerImpl::DeviceControllerImpl(DeviceType device_type) : _deviceType(device_type)
{
    // validate device type
    switch (_deviceType)
    {
    case DeviceType::F45x:
        break;
    case DeviceType::F50x:
        break;
    default:
        throw std::invalid_argument("Unknown device type");
    }
}

Status DeviceControllerImpl::Connect(const SerialConfig& config)
{
    try
    {
        // disconnect if already connected
        _serial.reset();

#ifdef _WIN32
        _serial = std::make_unique<PacketManager::WindowsSerial>(PacketManager::SerialConfig({config.port}));
#elif defined(__ANDROID__)
        PacketManager::SerialConfig serial_config;
        serial_config.fileDescriptor = config.fileDescriptor;
        serial_config.readEndpoint = config.readEndpoint;
        serial_config.writeEndpoint = config.writeEndpoint;
        _serial = std::make_unique<PacketManager::AndroidSerial>(serial_config);
#elif defined(__linux__) || defined(__APPLE__)
        _serial = std::make_unique<PacketManager::LinuxSerial>(PacketManager::SerialConfig({config.port}));
#else
        LOG_ERROR(LOG_TAG, "Serial connection method not supported for OS");
        return Status::Error;
#endif //_WIN32
        return Status::Ok;
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception during serial connect");
        return Status::Error;
    }
}

void DeviceControllerImpl::Disconnect()
{
    _serial.reset();
}

Status DeviceControllerImpl::Reboot()
{
    if (!_serial)
    {
        LOG_ERROR(LOG_TAG, "Not connected to a serial port");
        return Status::SerialError;
    }
    auto status = _serial->SendBytes(PacketManager::Commands::reset, strlen(PacketManager::Commands::reset));
    return ToStatus(status);
}

Status DeviceControllerImpl::QueryFirmwareVersion(std::string& version)
{
    // clear output version string to avoid returning garbage
    version.clear();
    try
    {
        std::string version_in_progress;
        {
            auto status = _serial->SendBytes(PacketManager::Commands::version_info, ::strlen(PacketManager::Commands::version_info));
            if (status != PacketManager::SerialStatus::Ok)
            {
                LOG_ERROR(LOG_TAG, "Failed sending version command");
                return ToStatus(status);
            }
        }

        // receive data until no more is available
        constexpr size_t max_buffer_size = 1024;
        char buffer[max_buffer_size] = {0};
        for (size_t i = 0; i < max_buffer_size - 1; ++i)
        {
            auto status = _serial->RecvBytes(&buffer[i], 1);

            // timeout is legal for the final byte, because we do not know the expected data size
            if (status == PacketManager::SerialStatus::RecvTimeout)
                break;

            // other error are still not accepted
            if (status != PacketManager::SerialStatus::Ok)
            {
                LOG_ERROR(LOG_TAG, "Failed reading version data");
                return ToStatus(status);
            }
        }

        // parse lines of module versions .e.g.
        // ASDISP : 18.9.24.0
        // NNLED : 15.9.24.0
        static const std::regex module_regex {R"((\w+) : ([\d\.]+))"};
        std::stringstream ss(buffer);
        std::string line;
        while (std::getline(ss, line, '\n'))
        {
            std::smatch match;
            auto match_ok = std::regex_search(line, match, module_regex);

            if (match_ok)
            {
                auto version_number = match[2].str();
                if (version_number == "0.0.0.0") // ignore, unused module
                {
                    continue;
                }
                if (!version_in_progress.empty())
                    version_in_progress += '|';

                version_in_progress += match[1].str();
                version_in_progress += ':';
                version_in_progress += version_number;
            }
        }

        if (version_in_progress.empty())
        {
            LOG_ERROR(LOG_TAG, "Firmware version received from device is empty");
            return Status::Error;
        }

        version = version_in_progress;

        return Status::Ok;
    }
    catch (std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return Status::Error;
    }
}

Status DeviceControllerImpl::QuerySerialNumber(std::string& serial)
{
    // clear serial number string to avoid returning garbage
    serial.clear();

    try
    {
        auto send_status = _serial->SendBytes(PacketManager::Commands::device_info, ::strlen(PacketManager::Commands::device_info));
        if (send_status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed sending serial number command");
            return ToStatus(send_status);
        }

        // receive data until no more is available
        constexpr size_t max_buffer_size = 512;
        char buffer[max_buffer_size] = {0};
        for (size_t i = 0; i < max_buffer_size - 1; ++i)
        {
            auto status = _serial->RecvBytes(&buffer[i], 1);

            // timeout is legal for the final byte, because we do not know the expected data size
            if (status == PacketManager::SerialStatus::RecvTimeout)
                break;

            // other error are still not accepted
            if (status != PacketManager::SerialStatus::Ok)
            {
                LOG_ERROR(LOG_TAG, "Failed reading serial number data");
                return ToStatus(status);
            }
        }

        std::stringstream ss(buffer);
        std::string line;
        while (std::getline(ss, line, '\n'))
        {
            static const std::regex serial_number_regex {R"(SN : \[(.*)\])"};
            std::smatch match;

            auto match_ok = std::regex_search(line, match, serial_number_regex);

            if (match_ok)
            {
                serial = match[1].str();
                break;
            }
        }

        if (serial.empty())
        {
            LOG_WARNING(LOG_TAG, "Serial number received from device is empty");
            return Status::Error;
        }

        return Status::Ok;
    }
    catch (std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return Status::Error;
    }
}

Status DeviceControllerImpl::QueryOtpVersion(uint8_t& otpVer)
{
    try
    {
        auto send_status = _serial->SendBytes(PacketManager::Commands::otp_ver, ::strlen(PacketManager::Commands::otp_ver));
        if (send_status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed sending otp version command");
            return ToStatus(send_status);
        }

        // receive data until no more is available
        constexpr size_t max_buffer_size = 128;
        char buffer[max_buffer_size] = {0};
        for (size_t i = 0; i < max_buffer_size - 1; ++i)
        {
            auto status = _serial->RecvBytes(&buffer[i], 1);

            // timeout is legal for the final byte, because we do not know the expected data size
            if (status == PacketManager::SerialStatus::RecvTimeout)
                break;

            // other error are still not accepted
            if (status != PacketManager::SerialStatus::Ok)
            {
                LOG_ERROR(LOG_TAG, "Failed reading serial number data");
                return ToStatus(status);
            }
        }

        std::stringstream ss(buffer);
        std::string line;
        std::string otpVerStr;
        while (std::getline(ss, line, '\n'))
        {
            static const std::regex otp_version_regex {R"(otp version is (.*))"};
            std::smatch match;

            auto match_ok = std::regex_search(line, match, otp_version_regex);

            if (match_ok)
            {
                otpVerStr = match[1].str();
                break;
            }
        }

        if (otpVerStr.empty())
        {
            LOG_ERROR(LOG_TAG, "Otp version received from device is empty");
            return Status::Error;
        }

        otpVer = otpVerStr[0];
        return Status::Ok;
    }
    catch (std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return Status::Error;
    }
}

Status DeviceControllerImpl::Ping()
{
    using namespace PacketManager;
    if (!_serial)
    {
        LOG_ERROR(LOG_TAG, "Not connected to a serial port");
        return Status::Error;
    }

    // create a ping data packet with random data
    // create a ping data packet with random data
    char random_data[512];
    static_assert(sizeof(random_data) <= sizeof(DataMessage::data), "Random data size exceeds max allowed");
    Randomizer::Instance().GenerateRandom((unsigned char*)random_data, sizeof(random_data));
    DataPacket ping_packet {MsgId::Ping, random_data, sizeof(random_data)};

    PacketSender sender {_serial.get()};
    auto status = sender.SendBinary(ping_packet);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed sending ping packet (status %d)", (int)status);
        return ToStatus(status);
    }

    SerialPacket response;
    status = sender.Recv(response);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed receiving data packet (status %d)", (int)status);
        return ToStatus(status);
    }

    if (response.header.id != MsgId::Ping)
    {
        LOG_ERROR(LOG_TAG, "Got unexpected msg id in ping reply: %c", static_cast<char>(response.header.id));
        return Status::Error;
    }

    // check we got same response
    auto is_same = ::memcmp(random_data, response.payload.message.data_msg.data, sizeof(random_data)) == 0;
    if (!is_same)
    {
        LOG_ERROR(LOG_TAG, "got ping reply with different data");
        return Status::Error;
    }
    return Status::Ok;
}

Status DeviceControllerImpl::FetchLog(std::string& result)
{
    try
    {
        result.clear();
        auto send_status = _serial->SendBytes(PacketManager::Commands::getlogs, ::strlen(PacketManager::Commands::getlogs));
        if (send_status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed sending getLogs command");
            return ToStatus(send_status);
        }

        constexpr size_t max_result_size = 128 * 1024;
        constexpr size_t reserve_size = 1024;
        char buffer[1] = {0};
        const std::string start_token = "START_OF_LOG";
        const std::string end_token = "END_OF_LOG";

        result.reserve(reserve_size);
        bool done = false;
        // receive the data one byte at a time until "END_OF_LOG" or no more data is available (timeout is reached)
        while (!done)
        {
            auto status = _serial->RecvBytes(buffer, 1);
            switch (status)
            {
            case PacketManager::SerialStatus::Ok: {
                auto chr = buffer[0];
                bool chr_ok = std::isprint(static_cast<unsigned char>(chr)) || std::isspace(static_cast<unsigned char>(chr));
                result.push_back(chr_ok ? chr : '?');
                done = result.size() >= max_result_size;
                break;
            }
            case PacketManager::SerialStatus::RecvTimeout: {
                done = true;
                break;
            }
            default:
                LOG_ERROR(LOG_TAG, "Failed reading serial");
                return ToStatus(status);
            }
        }

        // remove the end token suffix if exists
        auto pos = result.rfind(end_token);
        if (pos != std::string::npos)
        {
            result.erase(pos);
        }

        // expect the START_OF_LOG token
        pos = result.find(start_token);
        if (pos != std::string::npos)
        {
            result.erase(0, pos + start_token.size());
        }
        else
        {
            result.clear();
            LOG_ERROR(LOG_TAG, "Didn't receive the START_OF_LOG token");
            return Status::Error;
        }

        // make sure it ends with '\n'
        if (!result.empty() && result.back() != '\n')
        {
            result.push_back('\n');
        }

        LOG_DEBUG(LOG_TAG, "Got %zu log bytes", result.size());
        return Status::Ok;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return Status::Error;
    }
}

Status DeviceControllerImpl::GetTemperature(float& soc, float& board)
{
    if (_deviceType != DeviceType::F50x)
    {
        LOG_ERROR(LOG_TAG, "GetTemperature is not supported for this device type");
        return Status::NotSupported;
    }

    auto send_status = _serial->SendBytes(PacketManager::Commands::gtemp, ::strlen(PacketManager::Commands::gtemp));
    if (send_status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed sending gtemp command");
        return ToStatus(send_status);
    }

    // receive data until no more is available
    constexpr size_t max_buffer_size = 512;
    char buffer[max_buffer_size] = {0};
    for (size_t i = 0; i < max_buffer_size - 1; ++i)
    {
        auto status = _serial->RecvBytes(&buffer[i], 1);
        // timeout is legal for the final byte, because we do not know the expected data size
        if (status == PacketManager::SerialStatus::RecvTimeout)
            break;

        // other error are still not accepted
        if (status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed reading serial data");
            return ToStatus(status);
        }
    }

    // extract red blue numbers e.g [123 511]
    try
    {
        std::string input(buffer);
        std::smatch matches;
        /*
          Search for gtemp response. e.g:
            SoC temperature   : 61.0
            Board temperature : 52.9 (793)
        */
        static std::regex temp_pattern(R"(SoC temperature\s*:\s*([\d\.]+)[\s\S]*Board temperature\s*:\s*([\d\.]+))");

        soc = board = 0;
        if (std::regex_search(input, matches, temp_pattern))
        {
            soc = std::stof(matches[1].str());
            board = std::stof(matches[2].str());
            return Status::Ok;
        }
        else
        {
            return Status::Error;
        }
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception in GetTemperature");
        return Status::Error;
    }
}

Status DeviceControllerImpl::GetColorGains(int& red, int& blue)
{
    if (_deviceType != DeviceType::F45x)
    {
        LOG_ERROR(LOG_TAG, "GetColorGains is not supported for this device type");
        return Status::NotSupported;
    }
    const char* const cmd = PacketManager::Commands::get_color_gains;
    auto send_status = _serial->SendBytes(cmd, ::strlen(cmd));
    if (send_status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed sending cm command");
        return ToStatus(send_status);
    }

    // receive data until no more is available
    constexpr size_t max_buffer_size = 128;
    char buffer[max_buffer_size] = {0};
    for (size_t i = 0; i < max_buffer_size - 1; ++i)
    {
        auto status = _serial->RecvBytes(&buffer[i], 1);
        // timeout is legal for the final byte, because we do not know the expected data size
        if (status == PacketManager::SerialStatus::RecvTimeout)
            break;

        // other error are still not accepted
        if (status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed reading serial number data");
            return ToStatus(status);
        }
    }

    // extract red blue numbers e.g [123 511]
    try
    {
        std::string input(buffer);
        std::smatch matches;
        static const std::regex pattern {R"(\[(\d+)\s(\d+)\])"};
        if (std::regex_search(input, matches, pattern))
        {
            red = std::stoi(matches[1].str());
            blue = std::stoi(matches[2].str());
            return Status::Ok;
        }
        else
        {
            return Status::Error;
        }
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception in GetColorGains");
        return Status::Error;
    }
}

Status DeviceControllerImpl::SetColorGains(int red, int blue)
{
    if (_deviceType != DeviceType::F45x)
    {
        LOG_ERROR(LOG_TAG, "SetColorGains is not supported for this device type");
        return Status::NotSupported;
    }

    constexpr int max_value = 511;
    constexpr int min_value = 0;
    if (red < min_value || red > max_value || blue < min_value || blue > max_value)
    {
        LOG_ERROR(LOG_TAG, "Invalid color gain values");
        return Status::Error;
    }

    char buf[64];
    const char* const cmd = PacketManager::Commands::set_color_gains;
    snprintf(buf, sizeof(buf), cmd, red, blue);
    auto send_status = _serial->SendBytes(buf, ::strlen(buf));
    if (send_status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed sending cm command");
    }
    return ToStatus(send_status);
}

Status DeviceControllerImpl::QueryBspVer(std::string& bspver)
{
    bspver.clear();
    try
    {
        auto status = _serial->SendBytes(PacketManager::Commands::version_info, ::strlen(PacketManager::Commands::version_info));
        if (status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed sending bspver command");
            return ToStatus(status);
        }

        // receive data until no more is available
        constexpr size_t max_buffer_size = 1024;
        char buffer[max_buffer_size] = {0};
        for (size_t i = 0; i < max_buffer_size - 1; ++i)
        {
            auto status = _serial->RecvBytes(&buffer[i], 1);

            // timeout is legal for the final byte, because we do not know the expected data size
            if (status == PacketManager::SerialStatus::RecvTimeout)
                break;

            // other error are still not accepted
            if (status != PacketManager::SerialStatus::Ok)
            {
                LOG_ERROR(LOG_TAG, "Failed reading version data");
                return ToStatus(status);
            }
        }
        bspver.assign(buffer);
        return Status::Ok;
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception in QueryBspVer");
        return Status::Error;
    }
}


} // namespace RealSenseID
