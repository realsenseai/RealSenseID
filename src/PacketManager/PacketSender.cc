// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "PacketSender.h"
#include "SerialConnection.h"
#include "Timer.h"
#include "Logger.h"
#include "Crc16.h"
#include <string.h>
#include <cstdint>
#include <stdexcept>
#include <cassert>
#include <inttypes.h>

const char* LOG_TAG = "PacketSender";

namespace RealSenseID
{
namespace PacketManager
{


PacketSender::PacketSender(SerialConnection* serial_iface) : _serial {serial_iface}
{
    if (serial_iface == nullptr)
    {
        throw std::runtime_error("PacketSender::PacketSender() - serial_iface* must not be nullptr");
    }
}

// construct with receive timeout (millis)
PacketSender::PacketSender(SerialConnection* serial_iface, timeout_t receive_timeout) :
    _recv_packet_timeout(receive_timeout), _serial {serial_iface}
{
    if (serial_iface == nullptr)
    {
        throw std::runtime_error("PacketSender::PacketSender() - serial_iface* must not be nullptr");
    }
}

SerialStatus PacketSender::Send(SerialPacket& packet)
{
#ifdef RSID_DEBUG_PACKETS
    LOG_DEBUG(LOG_TAG, "Sending packet '%c'", packet.header.id);
#endif
    // send the headers + payload
    auto* packet_ptr = reinterpret_cast<const char*>(&packet);
    auto packet_size = sizeof(packet.header) + packet.header.payload_size;
    auto status = _serial->SendBytes(packet_ptr, packet_size);
    if (status != SerialStatus::Ok)
    {
        return status;
    }

    // send the hmac
    status = _serial->SendBytes(packet.hmac, sizeof(packet.hmac));
    if (status != SerialStatus::Ok)
    {
        return status;
    }

    // send crc
    auto crc = CalcCrc(packet);
    status = _serial->SendBytes(reinterpret_cast<const char*>(&crc), sizeof(crc));
    return status;
}

SerialStatus PacketSender::SendBinary(SerialPacket& packet)
{
    // send __FACE_API__ command
    auto face_api_len = ::strlen(Commands::face_api);
    auto status = _serial->SendBytes(Commands::face_api, face_api_len);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed sending face entry command");
        return status;
    }

    return Send(packet);
}

// keep trying getting the packet until timeout
SerialStatus PacketSender::Recv(SerialPacket& target)
{
#ifdef RSID_DEBUG_PACKETS
    LOG_DEBUG(LOG_TAG, "Waiting packet..");
#endif

    Timer timer {_recv_packet_timeout};
    // reset the target packet with zeros
    ::memset(reinterpret_cast<char*>(&target), 0, sizeof(target));

    // wait for sync bytes up to timeout
    auto status = WaitSyncBytes(target, &timer);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv sync bytes before timeout");
        return status;
    }

    // validate protocol version
    status = _serial->RecvBytes(reinterpret_cast<char*>(&target.header.protocol_ver), 1);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv protocol version byte");
        return status;
    }
    if (target.header.protocol_ver != ProtocolVer)
    {
        LOG_ERROR(LOG_TAG, "Protocol version doesn't match. Expected: %u, Received: %u", ProtocolVer, target.header.protocol_ver);
        return SerialStatus::VersionMismatch;
    }

    // recv rest of packet header (without the sync bytes and protocol version which we already read)
    auto bytes_to_read = sizeof(target.header) - 3;
    auto* target_ptr = reinterpret_cast<char*>(&target) + 3;
    status = _serial->RecvBytes(target_ptr, bytes_to_read);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv rest of packet header (%zu bytes)", bytes_to_read);
        return status;
    }

    const auto msg_id = target.header.id;
    if (target.header.payload_size > sizeof(SerialPacket::payload))
    {
        LOG_ERROR(LOG_TAG, "Packet size is bigger than payload max size. MsgId: '%c' (%d)", static_cast<char>(msg_id),
                  static_cast<int>(msg_id));
        return SerialStatus::RecvFailed;
    }

    // recv packet payload
    target_ptr = reinterpret_cast<char*>(&target.payload);
    status = _serial->RecvBytes(target_ptr, target.header.payload_size);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv packet payload (%" PRIu16 " bytes). MsgId: '%c' (%d)", target.header.payload_size,
                  static_cast<char>(msg_id), static_cast<int>(msg_id));
        return status;
    }

    // recv packet hmac
    status = _serial->RecvBytes(target.hmac, sizeof(target.hmac));
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv packet hmac (%zu bytes). MsgId: '%c' (%d)", sizeof(target.hmac), static_cast<char>(msg_id),
                  static_cast<int>(msg_id));
        return status;
    }

    // recv packet crc
    status = _serial->RecvBytes(reinterpret_cast<char*>(&target.crc), sizeof(target.crc));
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv packet crc (%zu bytes). MsgId: '%c' (%d)", sizeof(target.crc), static_cast<char>(msg_id),
                  static_cast<int>(msg_id));
        return status;
    }

    // validate crc
    auto expected_crc = CalcCrc(target);
    if (expected_crc != target.crc)
    {
        // header bytes may themselves be corrupt; logged id is best-effort
        LOG_ERROR(LOG_TAG, "Got invalid crc. Expected: %u. Actual: %u. MsgId (maybe corrupt): '%c' (%d)", expected_crc, target.crc,
                  static_cast<char>(msg_id), static_cast<int>(msg_id));
        return SerialStatus::CrcError;
    }

#ifdef RSID_DEBUG_PACKETS
    LOG_DEBUG(LOG_TAG, "Received packet '%c' after %zu millis", target.header.id, timer.Elapsed().count());
#endif // RSID_DEBUG_PACKETS

    return SerialStatus::Ok;
}

// wait for sync bytes and place them into target
SerialStatus PacketSender::WaitSyncBytes(SerialPacket& target, const Timer* timeout)
{
    while (!timeout->ReachedTimeout())
    {
        auto status = _serial->RecvBytes(reinterpret_cast<char*>(&target.header.sync1), 1);
        if (status == SerialStatus::Ok && target.header.sync1 == SyncByte::Sync1)
        {
            // wait for sync2
            status = _serial->RecvBytes(reinterpret_cast<char*>(&target.header.sync2), 1);
            if (status == SerialStatus::Ok && target.header.sync2 == SyncByte::Sync2)
            {
                return SerialStatus::Ok;
            }
        }
    }
    return SerialStatus::RecvTimeout;
}

uint16_t PacketSender::CalcCrc(const SerialPacket& packet)
{
    auto* packet_ptr = reinterpret_cast<const char*>(&packet);
    auto size = sizeof(packet.header) + packet.header.payload_size + sizeof(packet.hmac);
    if (size > sizeof(packet) - sizeof(packet.crc))
    {
        throw std::runtime_error("CalcCrc: Packet size is bigger than packet struct size");
    }
    auto crc = Crc16(packet_ptr, size);
    static_assert(sizeof(packet.crc) == sizeof(crc), "packet.crc and crc size mismatch");
    return crc;
}

} // namespace PacketManager
} // namespace RealSenseID
