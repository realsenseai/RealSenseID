// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "SerialPacket.h"
#include "CommonTypes.h"
#include <chrono>

// packet sender for sending/receiving complete serial packets over the serial interface
namespace RealSenseID
{
namespace PacketManager
{
class SerialConnection;
class Timer;
class PacketSender
{
public:
    static constexpr std::chrono::milliseconds DefaultRecvTimeout = std::chrono::milliseconds(5000);

    explicit PacketSender(SerialConnection* serializer);

    // construct with receive timeout (millis)
    PacketSender(SerialConnection* serializer, timeout_t receive_timeout);

    // send packet and return Status::ok on success
    SerialStatus Send(SerialPacket& packet);

    // switch to binary mode
    // send the packet
    // return Status::ok if both sends were successfully
    SerialStatus SendBinary(SerialPacket& packet);

    // receive complete and valid packet (with valid crc)
    // return:
    // Status::Ok on success,
    // Status::RecvTimeout on timeout
    // Status::RecvFailed on other failures
    SerialStatus Recv(SerialPacket& target);

    // Wait for sync bytes
    // return:
    // Status::Ok on success,
    // Status::RecvTimeout on timeout
    // Status::RecvFailed on other failures
    SerialStatus WaitSyncBytes(SerialPacket& target, const Timer* timeout);

private:
    static uint16_t CalcCrc(const SerialPacket& packet);

    timeout_t _recv_packet_timeout = DefaultRecvTimeout;
    SerialConnection* _serial;
};
} // namespace PacketManager
} // namespace RealSenseID
