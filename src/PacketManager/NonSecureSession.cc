// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "NonSecureSession.h"
#include "PacketSender.h"
#include "StatusHelper.h"
#include "Logger.h"
#include <stdexcept>
#include <cstring>
#include <cassert>

static const char* LOG_TAG = "NonSecureSession";
static constexpr int MAX_SEQ_NUMBER_DELTA = 20;
constexpr std::chrono::milliseconds start_session_timeout {4'000};

namespace RealSenseID
{
namespace PacketManager
{
NonSecureSession::~NonSecureSession()
{
    if (_serial)
    {
        try
        {
            LOG_DEBUG(LOG_TAG, "Close session");
            auto ignored = HandleCancelFlag(); // cancel if requested but not handled yet
            (void)ignored;
        }
        catch (...)
        {
        }
    }
}

SerialStatus NonSecureSession::Start(SerialConnection* serial_conn)
{
    LOG_DEBUG(LOG_TAG, "Start session");

    _is_open = false;
    _cancel_required = false;

    if (serial_conn == nullptr)
    {
        throw std::runtime_error("NonSecureSession: serial connection is null");
    }
    _serial = serial_conn;
    _serial->Clear(); // clear any leftovers from previous sessions

    _last_sent_seq_number = 0;
    _last_recv_seq_number = 0;
    DataPacket packet {MsgId::StartSession};
    PacketSender sender {_serial};
    auto status = sender.SendBinary(packet);
    if (status != SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to send start session packet");
        return status;
    }

    PacketManager::Timer session_timer {start_session_timeout};
    while (true)
    {
        if (session_timer.ReachedTimeout())
        {
            LOG_ERROR(LOG_TAG, "session timeout");
            return SerialStatus::RecvTimeout;
        }

        auto recv_status = sender.Recv(packet);
        if (recv_status != SerialStatus::Ok) //  || packet.header.id != MsgId::StartSession
        {
            LOG_ERROR(LOG_TAG, "Failed to recv device start session response");
            return recv_status;
        }

        auto msg_id = packet.header.id;


        if (msg_id == MsgId::StartSession)
        {
            LOG_DEBUG(LOG_TAG, "Session Started");
            _is_open = true;
            return SerialStatus::Ok;
        }

        else if (msg_id == MsgId::Reply)
        {
            // MsgId::Reply here means session was not opened successfully
            auto fa_packet = reinterpret_cast<FaPacket*>(&packet);
            auto status_code = static_cast<int>(fa_packet->GetStatusCode());
            auto fa_status = static_cast<Status>(status_code);
            const char* description = Description(fa_status);
            LOG_ERROR(LOG_TAG, "Failed: %s", description);
            return ToSerialStatus(fa_status);
        }
        else
        {
            LOG_WARNING(LOG_TAG, "Received unexpected msg id '%c' (%d)", msg_id, static_cast<int>(msg_id));
            return SerialStatus::RecvUnexpectedPacket;
        }
    }
}

bool NonSecureSession::IsOpen() const
{
    return _is_open;
}

SerialStatus NonSecureSession::SendPacket(SerialPacket& packet)
{
    return SendPacketImpl(packet);
}

SerialStatus NonSecureSession::RecvPacket(SerialPacket& packet)
{
    return RecvPacketImpl(packet, PacketSender::DefaultRecvTimeout);
}

SerialStatus NonSecureSession::RecvFaPacket(FaPacket& packet, timeout_t timeout)
{
    return RecvPacketImpl(packet, timeout);
}

SerialStatus NonSecureSession::RecvFaPacket(FaPacket& packet)
{
    return RecvFaPacket(packet, PacketSender::DefaultRecvTimeout);
}

SerialStatus NonSecureSession::RecvDataPacket(DataPacket& packet)
{
    return RecvPacketImpl(packet, PacketSender::DefaultRecvTimeout);
}

SerialStatus NonSecureSession::SendPacketImpl(SerialPacket& packet)
{
    // increment and set sequence number in the packet
    packet.payload.sequence_number = ++_last_sent_seq_number;
    assert(_serial != nullptr);
    PacketSender sender {_serial};
    return sender.SendBinary(packet);
}

// new sequence number should advance by max of MAX_SEQ_NUMBER_DELTA from last number
static bool ValidateSeqNumber(uint32_t last_recv_number, uint32_t seq_number)
{
    return (last_recv_number < seq_number && seq_number <= last_recv_number + MAX_SEQ_NUMBER_DELTA);
}

SerialStatus NonSecureSession::RecvPacketImpl(SerialPacket& packet, timeout_t recv_timeout)
{
    assert(_serial != nullptr);
    PacketSender sender {_serial, recv_timeout};

    // Handle cancel flag
    auto status = HandleCancelFlag();
    if (status != SerialStatus::Ok)
    {
        return status;
    }

    status = sender.Recv(packet);
    if (status != SerialStatus::Ok)
    {
        return status;
    }

    // validate sequence number
    auto current_seq = packet.payload.sequence_number;
    if (!ValidateSeqNumber(_last_recv_seq_number, current_seq))
    {
        LOG_ERROR(LOG_TAG, "Invalid sequence number. Last: %u, Current: %u", _last_recv_seq_number, current_seq);
        return SerialStatus::SecurityError;
    }
    _last_recv_seq_number = current_seq;
    return SerialStatus::Ok;
}

void NonSecureSession::Cancel()
{
    LOG_DEBUG(LOG_TAG, "Cancel requested.");
    _cancel_required = true;
}


SerialStatus NonSecureSession::SendCancel()
{
    if (!_serial)
    {
        LOG_ERROR(LOG_TAG, "Cannot send cancel, no serial connection");
        return SerialStatus::SendFailed;
    }
    LOG_DEBUG(LOG_TAG, "Sending cancel..");
    return _serial->SendBytes(Commands::face_cancel, ::strlen(Commands::face_cancel));
}

void NonSecureSession::OnConnectionClosed()
{
    _serial = nullptr;
}

SerialStatus NonSecureSession::HandleCancelFlag()
{
    if (!_cancel_required)
    {
        return SerialStatus::Ok;
    }
    _cancel_required = false;
    if (!_serial)
    {
        LOG_WARNING(LOG_TAG, "Cannot send cancel, no serial connection");
        return SerialStatus::SendFailed;
    }

    LOG_DEBUG(LOG_TAG, "Sending cancel..");
    return _serial->SendBytes(Commands::face_cancel, ::strlen(Commands::face_cancel));
}
} // namespace PacketManager
} // namespace RealSenseID
