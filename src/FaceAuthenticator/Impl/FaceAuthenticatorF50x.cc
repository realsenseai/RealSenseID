// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
#include "FaceAuthenticatorF50x.h"
#include "Logger.h"
#include "StatusHelper.h"

#include <thread>
#include <utility>
#include <cassert>

static const char* LOG_TAG = "FaceAuthenticatorF50x";
static constexpr std::chrono::milliseconds KEEP_ALIVE_INTERVAL {4'000};
static constexpr std::chrono::milliseconds SESSION_TIMEOUT {5'000}; // single session timeout
static constexpr unsigned int MAX_PERSONS = 35;

namespace RealSenseID
{
namespace Impl
{

FaceAuthenticatorF50x::FaceAuthenticatorF50x() : FaceAuthenticatorCommon(nullptr /* signature callback, not supported */)
{
}


Status FaceAuthenticatorF50x::DumpAndMount()
{
    auto status = _session.Start(_serial.get());
    if (status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Session start failed with status %d", static_cast<int>(status));
        return ToStatus(status);
    }
    PacketManager::FaPacket packet {PacketManager::MsgId::SaveDebug, nullptr, 0};
    status = _session.SendPacket(packet);
    if (status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed sending packet (status %d)", static_cast<int>(status));
    }
    LOG_DEBUG(LOG_TAG, "Saving debug data. Please wait..");
    status = _session.RecvFaPacket(packet, std::chrono::seconds(90));

    // send usbmm command after a short delay
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return MountDebug();
}

Status FaceAuthenticatorF50x::MountDebug()
{
    using PacketManager::Commands::usbmm;
    auto status = _serial->SendBytes(usbmm, ::strlen(usbmm));
    return ToStatus(status);
}

// return list of persons from given packet
// packet layout:
// [1 byte]   - number of persons N
// [4 bytes]  - timestamp
// [N x PersonRect] - persons data
static std::pair<std::vector<PersonRect>, unsigned int> ParsePersons(const PacketManager::SerialPacket& packet)
{
    assert(packet.header.id == PacketManager::MsgId::PersonDetected);

    auto* data = packet.payload.message.data_msg.data;
    static_assert(1 + sizeof(uint32_t) + MAX_PERSONS * sizeof(PersonRect) < sizeof(packet.payload.message.data_msg.data),
                  "Not enough space payload for MAX_PERSONS and headers");

    const auto n_persons = static_cast<unsigned int>(static_cast<unsigned char>(data[0]));
    if (n_persons > MAX_PERSONS)
    {
        throw std::runtime_error("Got unexpected persons count in response: " + std::to_string(n_persons));
    }

    data++;
    unsigned int ts = 0;
    ::memcpy(&ts, data, sizeof(uint32_t));
    data += sizeof(uint32_t);


    std::vector<PersonRect> persons;
    persons.reserve(n_persons);
    for (unsigned int i = 0; i < n_persons; i++)
    {
        PersonRect person;
        ::memcpy(&person, data, sizeof(person));
        data += sizeof(person);
        persons.push_back(person);
    }
    return std::make_pair(persons, ts);
}

// return list of poses from given packet
// packet layout:
// [1 byte]   - number of persons N
// [4 bytes]  - timestamp
// [N x PersonPose] - poses data
static std::pair<std::vector<PersonPose>, unsigned int> ParsePoses(const PacketManager::SerialPacket& packet)
{
    assert(packet.header.id == PacketManager::MsgId::PoseDetected);

    auto* data = packet.payload.message.data_msg.data;
    static_assert(1 + sizeof(uint32_t) + MAX_PERSONS * sizeof(PersonPose) < sizeof(packet.payload.message.data_msg.data),
                  "Not enough space payload for MAX_PERSONS and headers");

    const auto n_persons = static_cast<unsigned int>(static_cast<unsigned char>(data[0]));
    if (n_persons > MAX_PERSONS)
    {
        throw std::runtime_error("Got unexpected persons count in response: " + std::to_string(n_persons));
    }

    data++;
    unsigned int ts = 0;
    ::memcpy(&ts, data, sizeof(uint32_t));
    data += sizeof(uint32_t);


    std::vector<PersonPose> poses;
    poses.reserve(n_persons);
    for (unsigned int i = 0; i < n_persons; i++)
    {
        PersonPose pose;
        ::memcpy(&pose, data, sizeof(pose));
        data += sizeof(pose);
        poses.push_back(pose);
    }
    return std::make_pair(poses, ts);
}

// Detection flow (shared by DetectPersons, DetectPoses, DetectBodyParts, DecodeBarcodes):
//
// Each method handles both single-shot and loop modes using the 'loop' parameter.
//
// Session lifecycle:
//   - AutoCanceler ensures a cancel command is sent to the device on any exit path
//     (timeout, error, exception, or callback-requested stop).
//   - On Reply (device ended the session normally), we call DoNotCancel() since the
//     session is already complete and no cancel is needed.
//
// Timer behavior:
//   - Loop mode: timer is used for periodic keep-alive messages to prevent device timeout.
//   - Single mode: timer is a session timeout - if reached, we return an error.
//
// Callback return value (loop mode only):
//   - If the callback returns false, the loop stops gracefully (return Ok).
//     AutoCanceler sends the cancel command to the device.
//   - In single mode, the callback return value is ignored.
//
// Message types from device:
//   - Detection result (e.g. PersonDetected): parsed and forwarded to callback.
//   - Result: intermediate status from device (e.g. no face found). Forwarded to callback on failure.
//   - Reply: final session status from device. Session is over, return the status.
//
Status FaceAuthenticatorF50x::DetectPersons(const RealSenseID::FaceAuthenticator::PersonCallback& callback, bool loop)
{
    using PacketManager::MsgId;
    try
    {
        _cancel_loop = false;
        StartSession();
        AutoCanceler canceler(&_session);
        PacketManager::FaPacket fa_packet {loop ? MsgId::DetectPersonsLoop : MsgId::DetectPersons};
        Send(fa_packet);

        PacketManager::Timer timer {loop ? KEEP_ALIVE_INTERVAL : SESSION_TIMEOUT};

        while (!_cancel_loop)
        {
            if (timer.ReachedTimeout())
            {
                if (loop)
                {
                    LOG_DEBUG(LOG_TAG, "Sending keep-alive");
                    SendProgress(true);
                    timer.Reset();
                }
                else
                {
                    LOG_ERROR(LOG_TAG, "session timeout");
                    return Status::Error;
                }
            }

            auto received_id = Recv(fa_packet);
            switch (received_id)
            {
            case MsgId::PersonDetected: {
                auto [persons, ts] = ParsePersons(fa_packet);
                if (!callback(std::move(persons), ts, AuthenticateStatus::PersonFound) && loop)
                {
                    LOG_DEBUG(LOG_TAG, "Person detection loop canceled by callback");
                    return Status::Ok;
                }
                break;
            }
            case MsgId::Result: {
                const auto fa_status = fa_packet.GetStatusCode();
                const auto auth_status = static_cast<AuthenticateStatus>(fa_status);
                // LOG_DEBUG(LOG_TAG, "Got Result %d (%s)", static_cast<int>(auth_status), Description(auth_status));
                bool success = (auth_status == AuthenticateStatus::PersonFound || auth_status == AuthenticateStatus::Success);
                if (!success)
                {
                    if (!callback({}, 0, auth_status) && loop)
                    {
                        LOG_DEBUG(LOG_TAG, "Detection loop canceled by callback");
                        return Status::Ok;
                    }
                }
                break;
            }
            case MsgId::Reply: {
                auto fa_status = fa_packet.GetStatusCode();
                auto final_status = static_cast<Status>(fa_status);
                LOG_DEBUG(LOG_TAG, "Got Reply, final status: %d", static_cast<int>(final_status));
                canceler.DoNotCancel();
                return final_status;
            }
            default:
                LOG_WARNING(LOG_TAG, "Received unexpected MsgId: %d", static_cast<int>(received_id));
            }
        }

        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
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

Status FaceAuthenticatorF50x::DetectPoses(const RealSenseID::FaceAuthenticator::PoseCallback& callback, bool loop)
{
    using PacketManager::MsgId;
    try
    {
        _cancel_loop = false;
        StartSession();
        AutoCanceler canceler(&_session);
        PacketManager::FaPacket fa_packet {loop ? MsgId::DetectPosesLoop : MsgId::DetectPoses};
        Send(fa_packet);

        PacketManager::Timer timer {loop ? KEEP_ALIVE_INTERVAL : SESSION_TIMEOUT};

        while (!_cancel_loop)
        {
            if (timer.ReachedTimeout())
            {
                if (loop)
                {
                    LOG_DEBUG(LOG_TAG, "Sending keep-alive");
                    SendProgress(true);
                    timer.Reset();
                }
                else
                {
                    LOG_ERROR(LOG_TAG, "session timeout");
                    return Status::Error;
                }
            }

            auto received_id = Recv(fa_packet);
            switch (received_id)
            {
            case MsgId::PoseDetected: {
                auto [poses, ts] = ParsePoses(fa_packet);
                if (!callback(std::move(poses), ts, AuthenticateStatus::PersonFound) && loop)
                {
                    LOG_DEBUG(LOG_TAG, "Pose detection loop canceled by callback");
                    return Status::Ok;
                }
                break;
            }
            case MsgId::Result: {
                const auto fa_status = fa_packet.GetStatusCode();
                const auto auth_status = static_cast<AuthenticateStatus>(fa_status);
                LOG_DEBUG(LOG_TAG, "Got Result %d (%s)", static_cast<int>(auth_status), Description(auth_status));
                bool success = (auth_status == AuthenticateStatus::PersonFound || auth_status == AuthenticateStatus::Success);
                if (!success)
                {
                    if (!callback({}, 0, auth_status) && loop)
                    {
                        LOG_DEBUG(LOG_TAG, "Detection loop canceled by callback");
                        return Status::Ok;
                    }
                }
                break;
            }
            // Reply indicates the end of the session, so we return the final status and do not cancel
            case MsgId::Reply: {
                auto fa_status = fa_packet.GetStatusCode();
                auto final_status = static_cast<Status>(fa_status);
                LOG_DEBUG(LOG_TAG, "Got Reply, final status: %d", static_cast<int>(final_status));
                canceler.DoNotCancel();
                return final_status;
            }
            default:
                LOG_WARNING(LOG_TAG, "Received unexpected MsgId: %d", static_cast<int>(received_id));
            }
        }

        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
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

Status FaceAuthenticatorF50x::DetectBodyParts(const RealSenseID::FaceAuthenticator::BodyPartCallback& callback, bool loop)
{
    using PacketManager::MsgId;
    try
    {
        _cancel_loop = false;
        StartSession();
        AutoCanceler canceler(&_session);
        PacketManager::FaPacket fa_packet {loop ? MsgId::DetectBodyPartsLoop : MsgId::DetectBodyParts};
        Send(fa_packet);

        PacketManager::Timer timer {loop ? KEEP_ALIVE_INTERVAL : SESSION_TIMEOUT};

        while (!_cancel_loop)
        {
            if (timer.ReachedTimeout())
            {
                if (loop)
                {
                    LOG_DEBUG(LOG_TAG, "Sending keep-alive");
                    SendProgress(true);
                    timer.Reset();
                }
                else
                {
                    LOG_ERROR(LOG_TAG, "session timeout");
                    return Status::Error;
                }
            }

            auto received_id = Recv(fa_packet);
            switch (received_id)
            {
            case MsgId::PersonDetected: {
                auto [body_parts, ts] = ParsePersons(fa_packet);
                if (!callback(std::move(body_parts), ts, AuthenticateStatus::PersonFound) && loop)
                {
                    LOG_DEBUG(LOG_TAG, "Body part detection loop canceled by callback");
                    return Status::Ok;
                }
                break;
            }
            case MsgId::Result: {
                const auto fa_status = fa_packet.GetStatusCode();
                const auto auth_status = static_cast<AuthenticateStatus>(fa_status);
                // LOG_DEBUG(LOG_TAG, "Got Result %d (%s)", static_cast<int>(auth_status), Description(auth_status));
                bool success = (auth_status == AuthenticateStatus::PersonFound || auth_status == AuthenticateStatus::Success);
                if (!success)
                {
                    if (!callback({}, 0, auth_status) && loop)
                    {
                        LOG_DEBUG(LOG_TAG, "Detection loop canceled by callback");
                        return Status::Ok;
                    }
                }
                break;
            }
            // Reply indicates the end of the session, so we return the final status and do not cancel
            case MsgId::Reply: {
                auto fa_status = fa_packet.GetStatusCode();
                auto final_status = static_cast<Status>(fa_status);
                LOG_DEBUG(LOG_TAG, "Got Reply, final status: %d", static_cast<int>(final_status));
                canceler.DoNotCancel();
                return final_status;
            }
            default:
                LOG_WARNING(LOG_TAG, "Received unexpected MsgId: %d", static_cast<int>(received_id));
            }
        }

        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
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

// return list of barcodes from given packet
// packet layout:
// [1 byte]   - number of barcodes N
// [4 bytes]  - timestamp
// For each barcode:
//   [2 bytes] - string length L
//   [L bytes] - barcode string data
static std::pair<std::vector<std::string>, unsigned int> ParseBarcodes(const PacketManager::SerialPacket& packet)
{
    assert(packet.header.id == PacketManager::MsgId::BarcodeDecoded);

    auto* data = packet.payload.message.data_msg.data;
    const auto n_barcodes = static_cast<unsigned int>(static_cast<unsigned char>(data[0]));

    constexpr unsigned int MAX_BARCODES = 3;
    if (n_barcodes > MAX_BARCODES)
    {
        throw std::runtime_error("Got unexpected barcodes count in response: " + std::to_string(n_barcodes));
    }

    data++;
    unsigned int ts = 0;
    ::memcpy(&ts, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    std::vector<std::string> barcodes;
    barcodes.reserve(n_barcodes);

    for (unsigned int i = 0; i < n_barcodes; i++)
    {
        uint16_t length = 0;
        ::memcpy(&length, data, sizeof(length));
        data += sizeof(length);

        if (length == 0)
        {
            barcodes.emplace_back();
            continue;
        }

        barcodes.emplace_back(reinterpret_cast<const char*>(data), length);
        data += length;
    }

    return std::make_pair(barcodes, ts);
}

Status FaceAuthenticatorF50x::DecodeBarcodes(const RealSenseID::FaceAuthenticator::BarcodeCallback& callback, bool loop)
{
    LOG_DEBUG(LOG_TAG, "Starting DecodeBarcodes");
    using PacketManager::MsgId;
    try
    {
        _cancel_loop = false;
        StartSession();
        AutoCanceler canceler(&_session);
        PacketManager::FaPacket fa_packet {loop ? MsgId::DecodeBarcodesLoop : MsgId::DecodeBarcodes};
        Send(fa_packet);

        PacketManager::Timer timer {loop ? KEEP_ALIVE_INTERVAL : SESSION_TIMEOUT};

        while (!_cancel_loop)
        {
            if (timer.ReachedTimeout())
            {
                if (loop)
                {
                    LOG_DEBUG(LOG_TAG, "Sending keep-alive");
                    SendProgress(true);
                    timer.Reset();
                }
                else
                {
                    LOG_ERROR(LOG_TAG, "session timeout");
                    return Status::Error;
                }
            }

            auto received_id = Recv(fa_packet);
            switch (received_id)
            {
            case MsgId::BarcodeDecoded: {
                auto [barcodes, ts] = ParseBarcodes(fa_packet);
                if (!callback(std::move(barcodes), ts, AuthenticateStatus::BarcodeFound) && loop)
                {
                    LOG_DEBUG(LOG_TAG, "Barcode decoding loop canceled by callback");
                    return Status::Ok;
                }
                break;
            }
            case MsgId::Result: {
                const auto fa_status = fa_packet.GetStatusCode();
                const auto auth_status = static_cast<AuthenticateStatus>(fa_status);
                LOG_DEBUG(LOG_TAG, "Got Result %d (%s)", static_cast<int>(auth_status), Description(auth_status));
                bool success = (auth_status == AuthenticateStatus::BarcodeFound || auth_status == AuthenticateStatus::Success);
                if (!success)
                {
                    if (!callback({}, 0, auth_status) && loop)
                    {
                        LOG_DEBUG(LOG_TAG, "Detection loop canceled by callback");
                        return Status::Ok;
                    }
                }
                break;
            }
            // Reply indicates the end of the session, so we return the final status and do not cancel
            case MsgId::Reply: {
                auto fa_status = fa_packet.GetStatusCode();
                auto final_status = static_cast<Status>(fa_status);
                LOG_DEBUG(LOG_TAG, "Got Reply, final status: %d", static_cast<int>(final_status));
                canceler.DoNotCancel();
                return final_status;
            }
            default:
                LOG_WARNING(LOG_TAG, "Received unexpected MsgId: %d", static_cast<int>(received_id));
            }
        }

        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
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

} // namespace Impl
} // namespace RealSenseID
