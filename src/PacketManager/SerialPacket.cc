// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "SerialPacket.h"
#include <stdexcept>
#include <string.h>
#include <cassert>

namespace RealSenseID
{
namespace PacketManager
{
SerialPacket::SerialPacket()
{
    // zero all bytes before filling the relevant parts
    ::memset(this, 0, sizeof(SerialPacket));

    // fill sync bytes
    header.sync1 = SyncByte::Sync1;
    header.sync2 = SyncByte::Sync2;

    header.protocol_ver = ProtocolVer;
    header.id = MsgId::None;
    header.payload_size = 0;
}

static int AlignTo32Bytes(int size)
{
    int mod = size % 32;
    if (mod)
        return size + (32 - size % 32);
    return size;
}

//
// FaPacket impl
//
FaPacket::FaPacket(MsgId id, const char* user_id, char status)
{
    header.id = id;
    header.payload_size = static_cast<uint16_t>(AlignTo32Bytes(sizeof(payload.sequence_number) + sizeof(FaMessage)));
    auto& fa_msg = payload.message.fa_msg;
    constexpr size_t buffer_size = sizeof(fa_msg.user_id);
    static_assert(buffer_size == (PacketManager::MaxUserIdSize + 1), "sizeof(fa_msg.user_id) != (MaxUserIdSize + 1)");

    if (user_id != nullptr)
    {
        // store the user_id in a 31 bytes buffer (max 30 ascii chars + zero terminating byte(s))
        ::strncpy(fa_msg.user_id, user_id, buffer_size - 1);
        fa_msg.user_id[buffer_size - 1] = '\0'; // always null terminated
        memset(fa_msg.reserved, '0', sizeof(fa_msg.reserved));
    }

    fa_msg.fa_status = static_cast<char>('0' + status);
}

FaPacket::FaPacket(MsgId id, const char* user_id, char status, const char* reserved, size_t reservedSize) : FaPacket(id, user_id, status)
{
    auto& fa_msg = payload.message.fa_msg;
    if (reserved == nullptr || reservedSize == 0)
    {
        throw std::runtime_error("FaPacket ctor: reserved data is null or empty");
    }
    if (reservedSize > sizeof(fa_msg.reserved))
    {
        throw std::runtime_error("FaPacket ctor: given reserved size exceeds max allowed");
    }

    ::memcpy(fa_msg.reserved, reserved, reservedSize);
}

FaPacket::FaPacket(MsgId id) : FaPacket(id, nullptr, 0)
{
}

// translate to null terminated user id
const char* FaPacket::GetUserId() const
{
    return payload.message.fa_msg.user_id;
}

char FaPacket::GetStatusCode() const
{
    return static_cast<char>(payload.message.fa_msg.fa_status - '0');
}

const char* FaPacket::GetReserved() const
{
    return payload.message.fa_msg.reserved;
}

//
// DataPacket impl
//
DataPacket::DataPacket(MsgId id, char* data, size_t data_size)
{
    header.id = id;
    header.payload_size = static_cast<uint16_t>(AlignTo32Bytes(static_cast<int>(sizeof(payload.sequence_number) + data_size)));
    uint32_t target_size = sizeof(payload.sequence_number) + sizeof(payload.message.data_msg.data);
    if (header.payload_size > target_size)
    {
        throw std::runtime_error("DataPacket ctor: given size exceeds max allowed");
    }
    auto* target_ptr = payload.message.data_msg.data;
    if (data != nullptr)
    {
        ::memcpy(target_ptr, data, data_size);
    }
}

DataPacket::DataPacket(MsgId id) : DataPacket(id, nullptr, 0)
{
}

const DataMessage& DataPacket::Data() const
{
    return payload.message.data_msg;
}

bool IsFaPacket(const SerialPacket& packet)
{
    return packet.header.id >= MsgId::MinFa && packet.header.id <= MsgId::MaxFa;
}

bool IsDataPacket(const SerialPacket& packet)
{
    return !IsFaPacket(packet);
}
} // namespace PacketManager
} // namespace RealSenseID
