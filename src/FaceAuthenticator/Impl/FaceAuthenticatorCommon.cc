// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "FaceAuthenticatorCommon.h"
#include "Logger.h"
#include "PacketManager/Timer.h"
#include "PacketManager/PacketSender.h"
#include "PacketManager/SerialPacket.h"
#include "StatusHelper.h"
#include "RealSenseID/MatcherDefines.h"
#include "RealSenseID/Faceprints.h"
#include "Matcher/Matcher.h"
#include "PacketManager/AuthConfigPayload.h"

#include <cstring>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <type_traits>

#ifdef _WIN32
#include "PacketManager/WindowsSerial.h"
#elif defined(__ANDROID__)
#include "PacketManager/AndroidSerial.h"
#elif defined(__linux__) || defined(__APPLE__)
#include "PacketManager/LinuxSerial.h"
#else
#error "Platform not supported"
#endif //_WIN32

static const char* LOG_TAG = "FaceAuthenticator";

using RealSenseID::PacketManager::AuthConfigPayload;
using RealSenseID::PacketManager::MsgId;

namespace RealSenseID
{
namespace Impl
{

static constexpr unsigned int NUM_LANDMARKS = 5;
static constexpr unsigned int MAX_FACES = 5;
static constexpr unsigned int QUERY_CHUNK_SIZE = 50;
static constexpr unsigned int MAX_UPLOAD_IMG_SIZE = 900 * 1024;
static constexpr std::chrono::milliseconds ENROLL_MAX_TIMEOUT {12'000};
static constexpr std::chrono::milliseconds AUTH_MAX_TIMEOUT {10'000};
static constexpr std::chrono::milliseconds KEEP_ALIVE_INTERVAL {4'000};

// Auto canceler implementation.
// Used to ensure session cancellation at the end of detection/auth loops, unless explicitly disabled by
// DoNotCancel call.
FaceAuthenticatorCommon::AutoCanceler::AutoCanceler(Session* session) : _session(session)
{
}

FaceAuthenticatorCommon::AutoCanceler::~AutoCanceler()
{
    try
    {
        if (_session)
        {
            auto ignored = _session->SendCancel();
            (void)ignored;
        }
    }
    catch (...)
    {
    }
}

void FaceAuthenticatorCommon::AutoCanceler::DoNotCancel()
{
    _session = nullptr;
}

static char to_printable(PacketManager::MsgId id)
{
    int i = static_cast<int>(id);
    return std::isprint(i) ? static_cast<char>(i) : '?';
}

static void throw_unexpected_packet(PacketManager::MsgId received)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Received unexpected MsgId %d ('%c')", static_cast<int>(received), to_printable(received));
    throw std::runtime_error(buf);
}

static void throw_unexpected_packet(PacketManager::MsgId received, PacketManager::MsgId expected)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Unexpected MsgId. Expected: %d ('%c'), Received: %d ('%c')", static_cast<int>(expected),
                  to_printable(expected), static_cast<int>(received), to_printable(received));
    throw std::runtime_error(buf);
}


static_assert(AuthConfigPayload::MAX_ROIS == DeviceConfig::MAX_ROIS, "MAX_ROIS mismatch between AuthConfigPayload and DeviceConfig");

static AuthConfigPayload CreateAuthConfigPayload(const DeviceConfig& config)
{
    AuthConfigPayload packet;
    ::memset(&packet, 0, sizeof(packet));
    packet.camera_rotation = static_cast<uint8_t>(config.camera_rotation);
    packet.security_level = static_cast<uint8_t>(config.security_level);
    packet.algo_flow = static_cast<uint8_t>(config.algo_flow);
    packet.gpio_auth_toggling = (config.gpio_auth_toggling == 1) ? 0x0b : 0x00;
    packet.dump_mode = static_cast<uint8_t>(config.dump_mode);
    packet.frontal_face_policy = static_cast<uint8_t>(config.frontal_face_policy);
    packet.person_motion_mode = static_cast<uint8_t>(config.person_motion_mode);
    packet.max_spoofs = config.max_spoofs;
    packet.match_thresh = config.match_thresh;
    packet.face_selection_policy = static_cast<uint8_t>(config.face_selection_policy);
    packet.manual_exposure_time_us = config.manual_exposure_time_us;
    packet.manual_gain = config.manual_gain;
    packet.rect_enable = config.rect_enable;
    packet.landmarks_enable = config.landmarks_enable;
    for (size_t i = 0; i < config.num_rois && i < AuthConfigPayload::MAX_ROIS; i++)
        packet.detection_rois[i] = {config.detection_rois[i].x, config.detection_rois[i].y, config.detection_rois[i].width,
                                    config.detection_rois[i].height};
    packet.distance_limit = static_cast<uint8_t>(config.distance_limit);
    packet.distance_enabled = config.distance_enabled ? 1 : 0;
    packet.num_rois = config.num_rois;
    return packet;
}

static void ParseAuthConfigPayload(const AuthConfigPayload& packet, DeviceConfig& config)
{
    config.camera_rotation = static_cast<DeviceConfig::CameraRotation>(packet.camera_rotation);
    config.security_level = static_cast<DeviceConfig::SecurityLevel>(packet.security_level);
    config.algo_flow = static_cast<DeviceConfig::AlgoFlow>(packet.algo_flow);
    config.gpio_auth_toggling = (packet.gpio_auth_toggling == 0x0b) ? 1 : 0;
    config.dump_mode = static_cast<DeviceConfig::DumpMode>(packet.dump_mode);
    config.frontal_face_policy = static_cast<DeviceConfig::FrontalFacePolicy>(packet.frontal_face_policy);
    config.person_motion_mode = static_cast<DeviceConfig::PersonMotionMode>(packet.person_motion_mode);
    config.max_spoofs = packet.max_spoofs;
    config.match_thresh = packet.match_thresh;
    config.face_selection_policy = static_cast<DeviceConfig::FaceSelectionPolicy>(packet.face_selection_policy);
    config.manual_exposure_time_us = packet.manual_exposure_time_us;
    config.manual_gain = packet.manual_gain;
    config.rect_enable = packet.rect_enable;
    config.landmarks_enable = packet.landmarks_enable;
    for (unsigned char i = 0; i < DeviceConfig::MAX_ROIS; i++)
        config.detection_rois[i] = {packet.detection_rois[i].x, packet.detection_rois[i].y, packet.detection_rois[i].w,
                                    packet.detection_rois[i].h};
    config.distance_limit = static_cast<DeviceConfig::DistanceLimit>(packet.distance_limit);
    config.distance_enabled = (packet.distance_enabled == 1);
    unsigned char nr = packet.num_rois;
    if (nr == 0 || nr > DeviceConfig::MAX_ROIS)
        nr = 1;
    config.num_rois = nr;
}


void DownloadImage::OnStart(unsigned int width, unsigned int height, unsigned timestamp)
{
    auto buffer_size = static_cast<size_t>(width * height * 3);
    buffer.reserve(buffer_size);
    buffer.clear();
    w = width;
    h = height;
    ts = timestamp;
    last_chunk = 0;
    timer.Reset();
}

bool DownloadImage::AddChunk(unsigned short chunk_n, unsigned char* data, size_t size)
{
    if (chunk_n != 0 && chunk_n != last_chunk + 1)
    {
        LOG_ERROR(LOG_TAG, "Got invalid chunk number. Expected %u. Got %u", last_chunk + 1, chunk_n);
        return false;
    }
    if (buffer.size() + size > MAX_UPLOAD_IMG_SIZE)
    {
        LOG_ERROR(LOG_TAG, "Max upload size exceeded");
        return false;
    }

    buffer.insert(buffer.end(), data, data + size);
    last_chunk = chunk_n;
    return true;
}

// save callback functions to use in the secure session later
FaceAuthenticatorCommon::FaceAuthenticatorCommon(SignatureCallback* callback) :
#ifdef RSID_SECURE
    _session {[callback](const unsigned char* buffer, const unsigned int bufferLen, unsigned char* outSig) {
                  return callback->Sign(buffer, bufferLen, outSig);
              },
              [callback](const unsigned char* buffer, const unsigned int bufferLen, const unsigned char* sig, const unsigned int sigLen) {
                  return callback->Verify(buffer, bufferLen, sig, sigLen);
              }}
{
    assert(callback != nullptr);
    if (callback == nullptr)
    {
        throw(std::runtime_error("Got nullptr for SignatureCallback"));
    }
}
#else
    _session {}
{
    assert(callback == nullptr);
    (void)callback; // callback not used
}
#endif // RSID_SECURE

Status FaceAuthenticatorCommon::Connect(const SerialConfig& config)
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

void FaceAuthenticatorCommon::Disconnect()
{
    _session.OnConnectionClosed();
    _serial.reset();
}

#ifdef RSID_SECURE
Status FaceAuthenticatorCommon::Pair(const char* ecdsaHostPubKey, const char* ecdsaHostPubKeySig, char* ecdsaDevicePubKey)
{
    // NOTE: 'Pair' operates outside the standard session flow initially, using a temporary packet sender.
    // It remains unchanged to preserve the secure handshake logic.
    if (!_serial)
    {
        LOG_ERROR(LOG_TAG, "Not connected to a serial port");
        return Status::Error;
    }
    LOG_INFO(LOG_TAG, "Pairing start");

    unsigned char ecdsaSignedHostPubKey[SIGNED_PUBKEY_SIZE];
    ::memset(ecdsaSignedHostPubKey, 0, sizeof(ecdsaSignedHostPubKey));
    ::memcpy(ecdsaSignedHostPubKey, ecdsaHostPubKey, ECC_P256_KEY_SIZE_BYTES);
    ::memcpy(ecdsaSignedHostPubKey + ECC_P256_KEY_SIZE_BYTES, ecdsaHostPubKeySig, ECC_P256_SIG_SIZE_BYTES);

    PacketManager::DataPacket packet {MsgId::HostEcdsaKey, reinterpret_cast<char*>(ecdsaSignedHostPubKey), sizeof(ecdsaSignedHostPubKey)};

    PacketManager::PacketSender sender {_serial.get()};
    auto status = sender.SendBinary(packet);
    if (status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to send ecdsa public key");
        return ToStatus(status);
    }

    status = sender.Recv(packet);
    if (status != PacketManager::SerialStatus::Ok)
    {
        LOG_ERROR(LOG_TAG, "Failed to recv device ecdsa public key");
        return ToStatus(status);
    }
    if (packet.header.id != MsgId::DeviceEcdsaKey)
    {
        LOG_ERROR(LOG_TAG, "Mutual authentication failed");
        return Status::SecurityError;
    }

    ::memcpy(ecdsaDevicePubKey, packet.Data().data, ECC_P256_KEY_SIZE_BYTES);

    DEBUG_SERIAL(LOG_TAG, "Device Pubkey", ecdsaDevicePubKey, ECC_P256_KEY_SIZE_BYTES);
    LOG_INFO(LOG_TAG, "Pairing Ok");
    return Status::Ok;
}

Status FaceAuthenticatorCommon::Unpair()
{
    if (!_serial)
    {
        LOG_ERROR(LOG_TAG, "Not connected to a serial port");
        return Status::Error;
    }
    auto status = _session.Unpair(_serial.get());
    return ToStatus(status);
}
#endif // RSID_SECURE


// return list of faces from given packet
static std::vector<FaceRect> GetDetectedFaces(const PacketManager::SerialPacket& packet, unsigned int& ts)
{
    assert(packet.header.id == MsgId::FaceDetected);

    static_assert(1 + sizeof(uint32_t) + MAX_FACES * sizeof(FaceRect) < sizeof(packet.payload.message.data_msg.data),
                  "Not enough space payload for MAX_FACES and headers");

    auto* data = packet.payload.message.data_msg.data;
    const auto n_faces = static_cast<unsigned int>(static_cast<unsigned char>(data[0]));
    if (n_faces > MAX_FACES)
    {
        throw std::runtime_error("Got unexpected faces count in response: " + std::to_string(n_faces));
    }

    data++;
    static_assert(sizeof(ts) == sizeof(uint32_t), "ts size mismatch");
    ::memcpy(&ts, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    std::vector<FaceRect> rv;
    rv.reserve(n_faces);
    for (unsigned int i = 0; i < n_faces; i++)
    {
        FaceRect face;
        ::memcpy(&face, data, sizeof(face));
        data += sizeof(face);
        rv.push_back(face);
    }
    return rv;
}

// return list of landmarks from given packet
static std::vector<FaceLandmarks> GetDetectedLandmarks(const PacketManager::SerialPacket& packet, unsigned int& ts)
{
    assert(packet.header.id == MsgId::LandmarksDetected);

    static_assert(1 + sizeof(uint32_t) + MAX_FACES * sizeof(FaceLandmarks) < sizeof(packet.payload.message.data_msg.data),
                  "Not enough space payload for MAX_FACES and headers");

    auto* data = packet.payload.message.data_msg.data;
    const auto n_faces = static_cast<unsigned int>(static_cast<unsigned char>(data[0]));
    if (n_faces > MAX_FACES)
    {
        throw std::runtime_error("Got unexpected faces count in response: " + std::to_string(n_faces));
    }

    data++;
    ::memcpy(&ts, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    std::vector<FaceLandmarks> rv;
    rv.reserve(n_faces);
    for (unsigned int i = 0; i < n_faces; i++)
    {
        FaceLandmarks lms;
        ::memcpy(&lms, data, sizeof(lms));
        data += sizeof(lms);
        rv.push_back(lms);
    }
    return rv;
}

// return list of face distances from given packet
// serialization format:
//   First byte: distance count
//   4 bytes: timestamp
//   N doubles (little endian, packed)
static std::vector<double> GetFaceDistances(const PacketManager::SerialPacket& packet, unsigned int& ts)
{
    assert(packet.header.id == PacketManager::MsgId::FaceDistances);

    auto* data = packet.payload.message.data_msg.data;
    const auto n_distances = static_cast<unsigned int>(static_cast<unsigned char>(data[0]));

    data++;
    ::memcpy(&ts, data, sizeof(uint32_t));
    data += sizeof(uint32_t);

    std::vector<double> rv;
    rv.reserve(n_distances);
    for (unsigned int i = 0; i < n_distances; i++)
    {
        double distance;
        ::memcpy(&distance, data, sizeof(distance));
        data += sizeof(distance);
        rv.push_back(distance);
    }
    return rv;
}

// Do enroll session with the device. Call user's enroll callbacks in the process.
Status FaceAuthenticatorCommon::Enroll(EnrollmentCallback& callback, const char* user_id)
{
    try
    {
        if (!ValidateUserId(user_id))
        {
            return Status::Error;
        }

        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::Enroll, user_id, 0};
        Send(fa_packet);

        PacketManager::Timer session_timer {ENROLL_MAX_TIMEOUT};
        while (true)
        {
            if (session_timer.ReachedTimeout())
            {
                LOG_ERROR(LOG_TAG, "session timeout");
                callback.OnResult(EnrollStatus::Failure);
                Cancel();
                return Status::Error;
            }

            auto msg_id = Recv(fa_packet);
            // handle face detected as data packet
            if (msg_id == MsgId::FaceDetected)
            {
                unsigned int ts = 0;
                auto faces = GetDetectedFaces(fa_packet, ts);
                callback.OnFaceDetected(faces, ts);
                continue; // continue to recv next messages
            }

            // handle landmarks detected as data packet
            if (msg_id == MsgId::LandmarksDetected)
            {
                unsigned int ts = 0;
                auto landmarks = GetDetectedLandmarks(fa_packet, ts);
                if (!landmarks.empty())
                    callback.OnLandmarksDetected(landmarks, ts);
                continue; // continue to recv next messages
            }

            // handle face cropped image data packet
            if (msg_id == MsgId::FaceCroppedImage)
            {
                bool inProgress = true;
                bool res = GetFaceCroppedImage(fa_packet, inProgress);
                if (res && !inProgress)
                    callback.OnFaceCroppedImage(_image_downloader.buffer.data(), _image_downloader.w, _image_downloader.h,
                                                _image_downloader.ts);
                continue; // continue to recv next messages
            }

            auto fa_status = fa_packet.GetStatusCode();
            auto enroll_status = static_cast<EnrollStatus>(fa_status);
            const char* log_enroll_status = Description(enroll_status);
            float frameScore = 0.0f;

            switch (msg_id)
            {
                // end of transaction
            case (MsgId::Reply):
                LOG_DEBUG(LOG_TAG, "Got Reply: %s", log_enroll_status);
                return static_cast<Status>(fa_status);

            case (MsgId::Result):
                callback.OnResult(enroll_status);
                break;

            case (MsgId::Progress):
                callback.OnProgress(static_cast<FacePose>(fa_status));
                break;

            case (MsgId::Hint):
                memcpy(&frameScore, fa_packet.GetReserved(), sizeof(float));
                callback.OnHint(enroll_status, frameScore);
                break;

            default:
                throw_unexpected_packet(msg_id);
            }
        }
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        callback.OnResult(ToEnrollStatus(ex.status));
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        callback.OnResult(EnrollStatus::Failure);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        callback.OnResult(EnrollStatus::Failure);
        return Status::Error;
    }
}

// send image to device in chunks
Status FaceAuthenticatorCommon::SendImageToDevice(const unsigned char* buffer, unsigned int width, unsigned int height)
{
    if (buffer == nullptr)
    {
        LOG_ERROR(LOG_TAG, "Invalid buffer");
        return Status::Error;
    }

    if (width > 0xFFFF || height > 0xFFFF)
    {
        LOG_ERROR(LOG_TAG, "Invalid width/height");
        return Status::Error;
    }

    uint32_t image_size = ((uint32_t)width * (uint32_t)height) * 3;
    if (image_size == 0 || image_size > MAX_UPLOAD_IMG_SIZE)
    {
        LOG_ERROR(LOG_TAG, "Invalid image size %u", image_size);
        return Status::Error;
    }

    // split to chunks and send to device
    auto constexpr chunk_size = sizeof(PacketManager::DataMessage::data);
    static_assert(chunk_size > 128, "invalid chunk size");

    uint32_t image_chunk_size = static_cast<uint32_t>(chunk_size) - 6;
    uint32_t n_chunks = image_size / image_chunk_size;
    uint32_t n_chunks_mod = image_size % image_chunk_size;
    uint32_t last_chunk_size = image_chunk_size;

    if (n_chunks_mod > 0)
    {
        last_chunk_size = n_chunks_mod;
        n_chunks++;
    }

    const auto width_16 = static_cast<uint16_t>(width);
    const auto height_16 = static_cast<uint16_t>(height);
    LOG_DEBUG(LOG_TAG, "Sending %d chunks..", n_chunks);
    char chunk[chunk_size];
    size_t total_image_bytes_sent = 0;

    try
    {
        for (uint32_t i = 0; i < n_chunks; i++)
        {
            StartSession();
            auto chunk_number = static_cast<uint16_t>(i);
            ::memcpy(&chunk[0], &chunk_number, sizeof(chunk_number));
            ::memcpy(&chunk[2], &width_16, sizeof(width_16));
            ::memcpy(&chunk[4], &height_16, sizeof(height_16));

            auto* image_chunk_ptr = &buffer[chunk_number * image_chunk_size];
            auto is_last_chunk = (i == n_chunks - 1);
            uint32_t bytes_to_send = is_last_chunk ? last_chunk_size : image_chunk_size;
            ::memcpy((unsigned char*)&chunk[6], image_chunk_ptr, bytes_to_send);

            LOG_DEBUG(LOG_TAG, "Send chunk %u/%u size=%u", chunk_number + 1, n_chunks, bytes_to_send);
            PacketManager::DataPacket data_packet {MsgId::UploadImage, chunk, chunk_size};
            Send(data_packet);

            total_image_bytes_sent += bytes_to_send;
            assert(total_image_bytes_sent <= image_size);

            // wait for reply
            Recv(data_packet, MsgId::UploadImage);
            LOG_DEBUG(LOG_TAG, "Sent chunk %hu OK. %zu/%u bytes", chunk_number + 1, total_image_bytes_sent, image_size);
        }
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
    assert(total_image_bytes_sent == image_size);
    return Status::Ok;
}

EnrollStatus FaceAuthenticatorCommon::EnrollImage(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height)
{
    return EnrollImageImpl(MsgId::EnrollImage, user_id, buffer, width, height);
}

EnrollStatus FaceAuthenticatorCommon::EnrollImageFeatureExtraction(const char* user_id, const unsigned char* buffer, unsigned int width,
                                                                   unsigned int height, ExtractedFaceprints* faceprints)
{
    if (!ValidateUserId(user_id))
    {
        LOG_ERROR(LOG_TAG, "invalid user id");
        return EnrollStatus::Failure;
    }

    if (nullptr == faceprints)
    {
        LOG_ERROR(LOG_TAG, "the faceprints argument is null");
        return EnrollStatus::Failure;
    }

    Status imageSendingStatus = SendImageToDevice(buffer, width, height);
    if (Status::Ok != imageSendingStatus)
    {
        LOG_ERROR(LOG_TAG, "Error sending the image to the device. status %d", static_cast<int>(imageSendingStatus));
        return EnrollStatus::Failure;
    }

    try
    {
        // now that the image was uploaded, send the enroll image request
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::EnrollImageFeatureExtraction, user_id, 0};
        Send(fa_packet);
        Recv(fa_packet);
        if (static_cast<EnrollStatus>(fa_packet.GetStatusCode()) != EnrollStatus::Success)
        {
            return static_cast<EnrollStatus>(fa_packet.GetStatusCode());
        }

        // expect Faceprints message
        PacketManager::DataPacket data_packet(MsgId::Faceprints);
        Recv(data_packet, MsgId::Faceprints);

        LOG_DEBUG(LOG_TAG, "Got faceprints from device!");
        const auto* desc = reinterpret_cast<ExtractedFaceprintsElement*>(data_packet.payload.message.data_msg.data);

        feature_t vecFlags = desc->featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS];
        LOG_DEBUG(LOG_TAG, "Enrollment flow : vecFlags = %d.", vecFlags);

        faceprints->data.version = desc->version;
        faceprints->data.featuresType = desc->featuresType;
        faceprints->data.flags = FaOperationFlagsEnum::OpFlagEnrollWithoutMask;

        size_t copySize = sizeof(desc->featuresVector);
        static_assert(sizeof(faceprints->data.featuresVector) == sizeof(desc->featuresVector),
                      "adaptive faceprints (without mask) sizes does not match");
        ::memcpy(faceprints->data.featuresVector, desc->featuresVector, copySize);

        // mark the enrolled vector flags as valid without mask.
        faceprints->data.featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS] = FaVectorFlagsEnum::VecFlagValidWithoutMask;
        return EnrollStatus::Success;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return ToEnrollStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return EnrollStatus::Failure;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return EnrollStatus::Failure;
    }
}

Status FaceAuthenticatorCommon::Authenticate(AuthenticationCallback& callback)
{
    return AuthenticateImpl(MsgId::Authenticate, callback);
}

Status FaceAuthenticatorCommon::AuthenticateLoop(AuthenticationCallback& callback)
{
    try
    {
        _cancel_loop = false;
        StartSession();
        AutoCanceler canceler(&_session); // ensure cancel is sent on exit
        PacketManager::FaPacket fa_packet {MsgId::AuthenticateLoop};
        Send(fa_packet);

        Status session_status = Status::Ok;
        PacketManager::Timer ka_timer {KEEP_ALIVE_INTERVAL};

        while (!_cancel_loop)
        {
            // send keep-alive every few seconds
            if (ka_timer.ReachedTimeout())
            {
                LOG_DEBUG(LOG_TAG, "Sending keep-alive");
                SendProgress(true);
                ka_timer.Reset();
            }

            const auto received_id = Recv(fa_packet);

            // if it returns false, it means a 'MsgId::Reply' was received and the loop should end.
            if (!ProcessAuthPacket(received_id, fa_packet, callback, session_status))
            {
                return session_status;
            }
        }

        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        callback.OnResult(ToAuthStatus(ex.status), "", 0);
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        callback.OnResult(AuthenticateStatus::Failure, "", 0);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        callback.OnResult(AuthenticateStatus::Failure, "", 0);
        return Status::Error;
    }
}

Status FaceAuthenticatorCommon::Cancel()
{
    try
    {
        _cancel_loop = true;
        _session.Cancel();
        return Status::Ok;
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
} // namespace RealSenseID

Status FaceAuthenticatorCommon::RemoveUser(const char* user_id)
{
    try
    {
        if (!ValidateUserId(user_id))
        {
            return Status::Error;
        }
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::RemoveUser, user_id, 0};
        Send(fa_packet);
        RecvReplyMsg(fa_packet);
        return static_cast<Status>(fa_packet.GetStatusCode());
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

Status FaceAuthenticatorCommon::RemoveAll()
{
    try
    {
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::RemoveAllUsers};
        Send(fa_packet);

        // remove all might take long if db is large
        // Note: We cannot use the Recv() helper here because we need a custom timeout.
        auto status = _session.RecvFaPacket(fa_packet, std::chrono::seconds(34));
        if (status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed receiving fa packet (status %d)", static_cast<int>(status));
            return ToStatus(status);
        }
        return static_cast<Status>(fa_packet.GetStatusCode());
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

Status FaceAuthenticatorCommon::SetDeviceConfig(const DeviceConfig& device_config)
{
    try
    {
        StartSession();
        // prepare settings buffer to send
        AuthConfigPayload config_payload = CreateAuthConfigPayload(device_config);

        PacketManager::DataPacket data_packet {MsgId::SetDeviceConfig, (char*)&config_payload, sizeof(config_payload)};
        Send(data_packet);

        // wait for reply
        PacketManager::DataPacket data_packet_reply {MsgId::SetDeviceConfig};
        auto msg_id = Recv(data_packet_reply);

        // device returns MsgId::Reply if failed to set the config
        if (msg_id == MsgId::Reply)
        {
            auto& fa_packet = reinterpret_cast<PacketManager::FaPacket&>(data_packet_reply);
            auto status = static_cast<Status>(fa_packet.GetStatusCode());
            LOG_ERROR(LOG_TAG, "SetDeviceConfig Failed with status: %s", Description(status));
            return status;
        }
        // device should return SetDeviceConfig msg id on success
        if (msg_id != MsgId::SetDeviceConfig)
        {
            throw_unexpected_packet(msg_id, MsgId::SetDeviceConfig /* expected */);
        }

        static_assert(sizeof(data_packet_reply.payload.message.data_msg.data) >= sizeof(AuthConfigPayload), "Invalid data message");

        // make sure we succeeded - the response should contain the same values that we sent
        AuthConfigPayload reply_payload;
        ::memcpy(&reply_payload, data_packet_reply.Data().data, sizeof(AuthConfigPayload));
        if (::memcmp(&config_payload, &reply_payload, sizeof(AuthConfigPayload)) != 0)
        {
            LOG_ERROR(LOG_TAG, "Settings at device were not applied");
            return Status::Error;
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

Status FaceAuthenticatorCommon::QueryDeviceConfig(DeviceConfig& device_config)
{
    try
    {
        StartSession();
        PacketManager::DataPacket data_packet {MsgId::QueryDeviceConfig, nullptr, 0};
        Send(data_packet);

        PacketManager::DataPacket data_packet_reply {MsgId::QueryDeviceConfig};
        Recv(data_packet_reply, MsgId::QueryDeviceConfig);

        static_assert(sizeof(data_packet_reply.payload.message.data_msg.data) >= sizeof(AuthConfigPayload), "data size too small");

        // parse the returned settings
        AuthConfigPayload config_payload;
        static_assert(std::is_trivially_copyable<AuthConfigPayload>::value, "AuthConfigPayload must be trivially copyable!");
        ::memcpy(&config_payload, data_packet_reply.payload.message.data_msg.data, sizeof(AuthConfigPayload));

        ParseAuthConfigPayload(config_payload, device_config);
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

Status FaceAuthenticatorCommon::QueryUserIds(char** user_ids, unsigned int& number_of_users)
{
    if (user_ids == nullptr || number_of_users == 0)
    {
        LOG_ERROR(LOG_TAG, "QueryUserIds: Got invalid params (nullptr or zero)");
        number_of_users = 0;
        return Status::Error;
    }

    try
    {
        unsigned int retrieved_user_count = 0;
        unsigned int arrived_users = 0;
        for (unsigned int i = 0; i < number_of_users && retrieved_user_count < number_of_users; i += arrived_users)
        {
            StartSession();
            // retrieve next NUMBER_OF_USERS_TO_QUERY users
            unsigned int settings[2];
            settings[0] = retrieved_user_count;
            settings[1] = QUERY_CHUNK_SIZE;

            PacketManager::DataPacket data_packet {MsgId::GetUserIds, reinterpret_cast<char*>(settings), sizeof(settings)};
            Send(data_packet);
            Recv(data_packet, MsgId::GetUserIds);

            // get number of users from the response
            const char* data = data_packet.Data().data;
            ::memcpy(&arrived_users, data, sizeof(unsigned int));
            if (arrived_users == 0)
            {
                break;
            }

            // extract user ids from the returned chunk. each user id is zero delimited c string.
            for (size_t j = 0, cur_pos = sizeof(unsigned int); j < arrived_users; j++)
            {
                if (retrieved_user_count >= number_of_users)
                {
                    break;
                }
                char* target = user_ids[retrieved_user_count];
                ::strncpy(target, &data[cur_pos], PacketManager::MaxUserIdSize);
                target[PacketManager::MaxUserIdSize] = '\0';
                cur_pos += ::strlen(target) + 1;
                retrieved_user_count++;
            }

            LOG_DEBUG(LOG_TAG, "Got %u user ids. So far:%u", arrived_users, retrieved_user_count);
        }
        assert(retrieved_user_count <= number_of_users);
        number_of_users = retrieved_user_count;
        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        number_of_users = 0;
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        number_of_users = 0;
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        number_of_users = 0;
        return Status::Error;
    }
}

Status FaceAuthenticatorCommon::QueryNumberOfUsers(unsigned int& number_of_users)
{
    try
    {
        StartSession();
        PacketManager::DataPacket get_nusers_packet {MsgId::GetNumberOfUsers};
        Send(get_nusers_packet);

        Recv(get_nusers_packet, MsgId::GetNumberOfUsers);

        uint32_t serialized_n_users = 0;
        ::memcpy(&serialized_n_users, &get_nusers_packet.payload.message.data_msg.data[0], sizeof(serialized_n_users));
        number_of_users = static_cast<unsigned int>(serialized_n_users);
        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        number_of_users = 0;
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        number_of_users = 0;
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        number_of_users = 0;
        return Status::Error;
    }
}

Status FaceAuthenticatorCommon::Standby()
{
    try
    {
        StartSession();
        PacketManager::DataPacket packet {MsgId::StandBy};
        Send(packet);
        // we're not waiting for the device to reply since it should be in standby mode now
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

Status FaceAuthenticatorCommon::Hibernate()
{
    try
    {
        if (!_serial)
        {
            throw std::runtime_error("Serial port not connected");
        }
        const char* const cmd = PacketManager::Commands::hibernate;
        auto send_status = _serial->SendBytes(cmd, ::strlen(cmd));
        if (send_status != PacketManager::SerialStatus::Ok)
        {
            LOG_ERROR(LOG_TAG, "Failed sending sleep command");
        }
        // we're not waiting for the device to reply since it should be in hibernate mode now
        return ToStatus(send_status);
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

Status FaceAuthenticatorCommon::Unlock()
{
    try
    {
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::Unlock, nullptr, '0'};
        Send(fa_packet);
        RecvReplyMsg(fa_packet);
        auto fa_status = fa_packet.GetStatusCode();
        return static_cast<Status>(fa_status);
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

// Do faceprints extraction using enrollment flow, on the device.
Status FaceAuthenticatorCommon::ExtractFaceprintsForEnroll(EnrollFaceprintsExtractionCallback& callback)
{
    try
    {
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::EnrollFaceprintsExtraction, nullptr, '0'};
        Send(fa_packet);

        PacketManager::Timer session_timer {ENROLL_MAX_TIMEOUT};
        bool faceprints_extraction_completed_on_device = false, received_faceprints_in_host = false;
        while (true)
        {
            if (session_timer.ReachedTimeout())
            {
                LOG_ERROR(LOG_TAG, "session timeout");
                callback.OnResult(EnrollStatus::Failure, nullptr);
                Cancel();
                return Status::Error;
            }

            if (faceprints_extraction_completed_on_device && !received_faceprints_in_host)
            {
                PacketManager::DataPacket data_packet(MsgId::Faceprints);
                Recv(data_packet, MsgId::Faceprints);

                LOG_DEBUG(LOG_TAG, "Got faceprints from device!");
                const auto* desc = reinterpret_cast<ExtractedFaceprintsElement*>(data_packet.payload.message.data_msg.data);

                feature_t vecFlags = desc->featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS];
                LOG_DEBUG(LOG_TAG, "Enrollment flow : vecFlags = %d.", vecFlags);

                // set all the members of the enrolled faceprints before it is inserted into the DB.
                ExtractedFaceprints faceprints;

                faceprints.data.version = desc->version;
                faceprints.data.featuresType = desc->featuresType;
                faceprints.data.flags = FaOperationFlagsEnum::OpFlagEnrollWithoutMask;

                size_t copySize = sizeof(desc->featuresVector);

                static_assert(sizeof(faceprints.data.featuresVector) == sizeof(desc->featuresVector),
                              "adaptive faceprints (without mask) sizes does not match");
                ::memcpy(faceprints.data.featuresVector, desc->featuresVector, copySize);

                // mark the enrolled vector flags as valid without mask.
                faceprints.data.featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS] = FaVectorFlagsEnum::VecFlagValidWithoutMask;
                received_faceprints_in_host = true;
                callback.OnResult(EnrollStatus::Success, &faceprints);
                continue;
            }

            auto msg_id = Recv(fa_packet);

            // handle face detected as data packet
            if (msg_id == MsgId::FaceDetected)
            {
                unsigned int ts = 0;
                auto faces = GetDetectedFaces(fa_packet, ts);
                callback.OnFaceDetected(faces, ts);
                continue; // continue to recv next messages
            }

            // handle landmarks detected as data packet
            if (msg_id == MsgId::LandmarksDetected)
            {
                unsigned int ts = 0;
                auto landmarks = GetDetectedLandmarks(fa_packet, ts);
                if (!landmarks.empty())
                    callback.OnLandmarksDetected(landmarks, ts);
                continue; // continue to recv next messages
            }

            // handle face cropped image data packet
            if (msg_id == MsgId::FaceCroppedImage)
            {
                bool inProgress = true;
                bool res = GetFaceCroppedImage(fa_packet, inProgress);
                if (res && !inProgress)
                    callback.OnFaceCroppedImage(_image_downloader.buffer.data(), _image_downloader.w, _image_downloader.h,
                                                _image_downloader.ts);
                continue; // continue to recv next messages
            }

            auto fa_status = fa_packet.GetStatusCode();
            auto enroll_status = static_cast<EnrollStatus>(fa_status);
            const char* log_enroll_status = Description(enroll_status);
            float frameScore = 0.0f;

            switch (msg_id)
            {
            case (MsgId::Reply):
                LOG_DEBUG(LOG_TAG, "Got Reply: %s", log_enroll_status);
                return static_cast<Status>(fa_status);

            case (MsgId::Result):

                if (enroll_status == EnrollStatus::Success)
                {
                    LOG_DEBUG(LOG_TAG, "Faceprints extraction succeeded on device, ready to receive faceprints in host ...");
                    faceprints_extraction_completed_on_device = true;
                }
                else
                {
                    callback.OnResult(enroll_status, nullptr);
                }
                break;

            case (MsgId::Progress):
                callback.OnProgress(static_cast<FacePose>(fa_status));
                break;

            case (MsgId::Hint):
                memcpy(&frameScore, fa_packet.GetReserved(), sizeof(float));
                callback.OnHint(enroll_status, frameScore);
                break;

            default:
                throw_unexpected_packet(msg_id);
            }
        }
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        auto enroll_status = ToEnrollStatus(ex.status);
        callback.OnResult(enroll_status, nullptr);
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        callback.OnResult(EnrollStatus::Failure, nullptr);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        callback.OnResult(EnrollStatus::Failure, nullptr);
        return Status::Error;
    }
}

// Do faceprints extraction using authentication flow on the device. Call user's authenticate callbacks in the process.
Status FaceAuthenticatorCommon::ExtractFaceprintsForAuth(AuthFaceprintsExtractionCallback& callback)
{
    try
    {
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::AuthenticateFaceprintsExtraction};
        Send(fa_packet);

        PacketManager::Timer session_timer {AUTH_MAX_TIMEOUT};
        bool faceprints_extraction_completed_on_device = false, received_faceprints_in_host = false;
        while (true)
        {
            if (session_timer.ReachedTimeout())
            {
                LOG_ERROR(LOG_TAG, "session timeout");
                callback.OnResult(AuthenticateStatus::Failure, nullptr);
                Cancel();
                return Status::Error;
            }

            if (faceprints_extraction_completed_on_device && !received_faceprints_in_host)
            {
                PacketManager::DataPacket data_packet(MsgId::Faceprints);
                Recv(data_packet, MsgId::Faceprints);

                LOG_DEBUG(LOG_TAG, "Got faceprints from device!");
                const auto* received_desc = reinterpret_cast<ExtractedFaceprintsElement*>(data_packet.payload.message.data_msg.data);

                feature_t vecFlags = received_desc->featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS];
                LOG_DEBUG(LOG_TAG, "Authentication flow : vecFlags = %d.", vecFlags);

                received_faceprints_in_host = true;

                ExtractedFaceprints faceprints;
                faceprints.data.version = received_desc->version;
                faceprints.data.featuresType = received_desc->featuresType;
                faceprints.data.flags = static_cast<int>((FaOperationFlagsEnum::OpFlagAuthWithoutMask));

                size_t copySize = sizeof(received_desc->featuresVector);
                static_assert(sizeof(faceprints.data.featuresVector) == sizeof(received_desc->featuresVector),
                              "adaptive faceprints (without mask) sizes does not match");
                ::memcpy(faceprints.data.featuresVector, received_desc->featuresVector, copySize);

                callback.OnResult(AuthenticateStatus::Success, &faceprints);
                continue;
            }

            auto msg_id = Recv(fa_packet);

            // handle face detected as data packet
            if (msg_id == MsgId::FaceDetected)
            {
                unsigned int ts = 0;
                auto faces = GetDetectedFaces(fa_packet, ts);
                callback.OnFaceDetected(faces, ts);
                continue; // continue to recv next messages
            }

            // handle landmarks detected as data packet
            if (msg_id == MsgId::LandmarksDetected)
            {
                unsigned int ts = 0;
                auto landmarks = GetDetectedLandmarks(fa_packet, ts);
                if (!landmarks.empty())
                    callback.OnLandmarksDetected(landmarks, ts);
                continue; // continue to recv next messages
            }

            // handle face distances as data packet
            if (msg_id == MsgId::FaceDistances)
            {
                unsigned int ts = 0;
                auto distances = GetFaceDistances(fa_packet, ts);
                if (!distances.empty())
                    callback.OnFaceDistances(distances, ts);
                continue; // continue to recv next messages
            }

            // handle face cropped image data packet
            if (msg_id == MsgId::FaceCroppedImage)
            {
                bool inProgress = true;
                bool res = GetFaceCroppedImage(fa_packet, inProgress);
                if (res && !inProgress)
                    callback.OnFaceCroppedImage(_image_downloader.buffer.data(), _image_downloader.w, _image_downloader.h,
                                                _image_downloader.ts);
                continue; // continue to recv next messages
            }

            auto fa_status = fa_packet.GetStatusCode();
            auto auth_status = static_cast<AuthenticateStatus>(fa_status);
            const char* log_auth_status = Description(auth_status);
            float frameScore = 0.0f;

            switch (msg_id)
            {
            case (MsgId::Reply):
                LOG_DEBUG(LOG_TAG, "Got Reply: %s", log_auth_status);
                return static_cast<Status>(fa_status);

            case (MsgId::Result):
                if (auth_status == AuthenticateStatus::Success)
                {
                    LOG_DEBUG(LOG_TAG, "Faceprints extraction succeeded on device, ready to receive faceprints in host ...");
                    faceprints_extraction_completed_on_device = true;
                    received_faceprints_in_host = false;
                }
                else
                    callback.OnResult(auth_status, nullptr);
                break;

            case (MsgId::Hint):
                memcpy(&frameScore, fa_packet.GetReserved(), sizeof(float));
                callback.OnHint(auth_status, frameScore);
                break;

            default:
                throw_unexpected_packet(msg_id);
            }
        }
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        auto auth_status = ToAuthStatus(ex.status);
        callback.OnResult(auth_status, nullptr);
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        callback.OnResult(AuthenticateStatus::Failure, nullptr);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        callback.OnResult(AuthenticateStatus::Failure, nullptr);
        return Status::Error;
    }
}

// Perform faceprints extraction loop. Call user's callbacks in the process.
Status FaceAuthenticatorCommon::ExtractFaceprintsForAuthLoop(AuthFaceprintsExtractionCallback& callback)
{
    try
    {
        _cancel_loop = false;
        StartSession();
        AutoCanceler canceler(&_session);

        PacketManager::FaPacket fa_packet {MsgId::AuthenticateFaceprintsExtractionLoop};
        Send(fa_packet);

        PacketManager::Timer ka_timer {KEEP_ALIVE_INTERVAL};
        bool faceprints_extraction_completed_on_device = false, received_faceprints_in_host = false;
        while (!_cancel_loop)
        {
            if (ka_timer.ReachedTimeout())
            {
                LOG_DEBUG(LOG_TAG, "Sending keep-alive");
                SendProgress(true);
                ka_timer.Reset();
            }

            if (faceprints_extraction_completed_on_device && !received_faceprints_in_host)
            {
                PacketManager::DataPacket data_packet(MsgId::Faceprints);
                Recv(data_packet, MsgId::Faceprints);

                LOG_DEBUG(LOG_TAG, "Got faceprints from device!");
                const auto* received_desc = reinterpret_cast<ExtractedFaceprintsElement*>(data_packet.payload.message.data_msg.data);
                feature_t vecFlags = received_desc->featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS];
                LOG_DEBUG(LOG_TAG, "Authentication flow : vecFlags = %d.", vecFlags);
                received_faceprints_in_host = true;
                ExtractedFaceprints faceprints;
                faceprints.data.version = received_desc->version;
                faceprints.data.featuresType = received_desc->featuresType;
                faceprints.data.flags = static_cast<int>((FaOperationFlagsEnum::OpFlagAuthWithoutMask));

                size_t copySize = sizeof(received_desc->featuresVector);
                static_assert(sizeof(faceprints.data.featuresVector) == sizeof(received_desc->featuresVector),
                              "adaptive faceprints (without mask) sizes does not match");
                ::memcpy(faceprints.data.featuresVector, received_desc->featuresVector, copySize);

                callback.OnResult(AuthenticateStatus::Success, &faceprints);
                continue;
            }

            auto msg_id = Recv(fa_packet);

            // handle face detected as data packet
            if (msg_id == MsgId::FaceDetected)
            {
                unsigned int ts = 0;
                auto faces = GetDetectedFaces(fa_packet, ts);
                callback.OnFaceDetected(faces, ts);
                continue; // continue to recv next messages
            }

            // handle landmarks detected as data packet
            if (msg_id == MsgId::LandmarksDetected)
            {
                unsigned int ts = 0;
                auto landmarks = GetDetectedLandmarks(fa_packet, ts);
                if (!landmarks.empty())
                    callback.OnLandmarksDetected(landmarks, ts);
                continue; // continue to recv next messages
            }

            // handle face cropped image data packet
            if (msg_id == MsgId::FaceCroppedImage)
            {
                bool inProgress = true;
                bool res = GetFaceCroppedImage(fa_packet, inProgress);
                if (res && !inProgress)
                    callback.OnFaceCroppedImage(_image_downloader.buffer.data(), _image_downloader.w, _image_downloader.h,
                                                _image_downloader.ts);
                continue; // continue to recv next messages
            }

            auto fa_status = fa_packet.GetStatusCode();
            auto auth_status = static_cast<AuthenticateStatus>(fa_status);
            const char* log_auth_status = Description(auth_status);
            float frameScore = 0.0f;
            auto status = static_cast<Status>(fa_status);

            switch (msg_id)
            {
            case (MsgId::Reply):
                LOG_DEBUG(LOG_TAG, "Got Reply: %s", log_auth_status);
                if (status != Status::Ok)
                    return status;
                break;

            case (MsgId::Result):
                if (auth_status == AuthenticateStatus::Success)
                {
                    LOG_DEBUG(LOG_TAG, "Faceprints extraction succeeded on device, ready to receive faceprints in host ...");
                    faceprints_extraction_completed_on_device = true;
                    received_faceprints_in_host = false;
                }
                else
                    callback.OnResult(auth_status, nullptr);
                break;

            case (MsgId::Hint):
                memcpy(&frameScore, fa_packet.GetReserved(), sizeof(float));
                callback.OnHint(auth_status, frameScore);
                break;

            default:
                throw_unexpected_packet(msg_id);
            }
        }
        return Status::Ok;
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        auto auth_status = ToAuthStatus(ex.status);
        callback.OnResult(auth_status, nullptr);
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        callback.OnResult(AuthenticateStatus::Failure, nullptr);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        callback.OnResult(AuthenticateStatus::Failure, nullptr);
        return Status::Error;
    }
}

MatchResultHost FaceAuthenticatorCommon::MatchFaceprints(MatchElement& new_faceprints, Faceprints& existing_faceprints,
                                                         Faceprints& updated_faceprints, ThresholdsConfidenceEnum matcher_confidence_level)
{
    MatchResultHost finalResult;
    auto result = Matcher::MatchFaceprints(new_faceprints, existing_faceprints, updated_faceprints, matcher_confidence_level);
    finalResult.success = result.success;
    finalResult.should_update = result.should_update;
    finalResult.score = result.score;
    return finalResult;
}

// Validate given user id.
bool FaceAuthenticatorCommon::ValidateUserId(const char* user_id)
{
    if (user_id == nullptr)
    {
        LOG_ERROR(LOG_TAG, "Invalid user id: nullptr");
        return false;
    }

    auto user_id_len = ::strlen(user_id);
    bool is_valid = user_id_len > 0 && user_id_len <= PacketManager::MaxUserIdSize;
    if (!is_valid)
    {
        LOG_ERROR(LOG_TAG, "Invalid user id length. Valid size: 1 - %zu", PacketManager::MaxUserIdSize);
    }
    return is_valid;
}

Status FaceAuthenticatorCommon::SendUserFaceprints(UserFaceprints& features)
{
    try
    {
        const char* user_id = features.user_id;
        if (!ValidateUserId(user_id))
        {
            return Status::Error;
        }
        char buffer[sizeof(DBFaceprintsElement) + PacketManager::MaxUserIdSize + 1] = {0};
        strncpy(buffer, user_id, PacketManager::MaxUserIdSize + 1);
        size_t offset = PacketManager::MaxUserIdSize + 1;
        const DBFaceprintsElement* desc = &(features.faceprints.data);
        memcpy(buffer + offset, (char*)desc, sizeof(DBFaceprintsElement));
        offset += sizeof(*desc);
        PacketManager::DataPacket data_packet {MsgId::SetUserFeatures, buffer, offset};
        Send(data_packet);
        RecvReplyMsg(data_packet);

        auto statusCode = static_cast<char>(data_packet.payload.message.fa_msg.fa_status - '0');
        return static_cast<Status>(statusCode);
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

Status FaceAuthenticatorCommon::SetUsersFaceprints(UserFaceprints* user_features, unsigned int num_of_users)
{
    if (num_of_users == 0 || user_features == nullptr)
    {
        LOG_ERROR(LOG_TAG, "SetUsersFaceprints: Got invalid params (nullptr or zero)");
        return Status::Error;
    }
    // set start index and end index for chunk
    constexpr unsigned int chunk_size = QUERY_CHUNK_SIZE;
    auto n_chunks = (num_of_users / chunk_size) + ((num_of_users % chunk_size) ? 1 : 0);
    unsigned int start_index = 0;
    unsigned int end_index = 0;
    for (unsigned int i = 0; i < n_chunks; i++)
    {
        start_index = i * chunk_size;
        end_index = (std::min)(start_index + chunk_size, num_of_users);
        LOG_INFO(LOG_TAG, "SetUsersFaceprints: Sending %u to %u", start_index + 1, end_index);

        RealSenseID::Status send_status = Status::Ok;

        try
        {
            StartSession();
            for (unsigned int index = start_index; index < end_index; index++)
            {
                send_status = SendUserFaceprints(user_features[index]);
                if (send_status != Status::Ok)
                {
                    LOG_ERROR(LOG_TAG, "SendUserFaceprints for user \"%s\": %s)", user_features[index].user_id, Description(send_status));
                    break;
                }
            }

            // ask the device to save to its storage before proceeding
            auto save_db_packet = std::make_unique<PacketManager::FaPacket>(MsgId::SaveDatabase);
            Send(*save_db_packet);

            // Wait for savedb reply
            RecvReplyMsg(*save_db_packet);
            auto status_code = save_db_packet->GetStatusCode();
            auto save_status = static_cast<Status>(status_code);
            if (save_status != Status::Ok)
            {
                LOG_ERROR(LOG_TAG, "Failed saving DB to device. Status: %d", static_cast<int>(status_code));
                return save_status;
            }

            // break if not all data sent successfully for this chunk
            if (send_status != Status::Ok)
            {
                return send_status;
            }
            // sleep between chunks to let the device time to perform other tasks if needed
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
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
    return Status::Ok;
}


Status FaceAuthenticatorCommon::GetUsersFaceprints(Faceprints* user_features, unsigned int& num_of_users)
{
    bool all_is_well = true;
    PacketManager::SerialStatus bad_status = PacketManager::SerialStatus::RecvFailed;

    try
    {
        auto query_status = QueryNumberOfUsers(num_of_users);
        if (query_status != Status::Ok)
        {
            LOG_ERROR(LOG_TAG, "GetUsersFaceprints: Failed querying number of users");
            return query_status;
        }

        for (unsigned int i = 0; i < num_of_users; i++)
        {
            try
            {
                PacketManager::DataPacket get_features_packet {MsgId::GetUserFeatures, reinterpret_cast<char*>(&i), sizeof(i)};
                Send(get_features_packet);

                PacketManager::DataPacket get_features_return_packet {MsgId::GetUserFeatures};
                Recv(get_features_return_packet, MsgId::GetUserFeatures);

                LOG_DEBUG(LOG_TAG, "Got faceprints from device!");
                auto* desc = reinterpret_cast<DBFaceprintsElement*>(get_features_return_packet.payload.message.data_msg.data);

                user_features[i].data.version = desc->version;
                user_features[i].data.featuresType = static_cast<FaceprintsTypeEnum>(desc->featuresType);

                static_assert(sizeof(user_features[i].data.adaptiveDescriptorWithoutMask) == sizeof(desc->adaptiveDescriptorWithoutMask),
                              "adaptive faceprints sizes (without mask) does not match");
                ::memcpy(user_features[i].data.adaptiveDescriptorWithoutMask, desc->adaptiveDescriptorWithoutMask,
                         sizeof(desc->adaptiveDescriptorWithoutMask));

                static_assert(sizeof(user_features[i].data.enrollmentDescriptor) == sizeof(desc->enrollmentDescriptor),
                              "enrollment faceprints sizes does not match");
                ::memcpy(user_features[i].data.enrollmentDescriptor, desc->enrollmentDescriptor, sizeof(desc->enrollmentDescriptor));
            }
            catch (const SerialError& ex)
            {
                // Inner catch to allow loop to continue for other users
                LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
                all_is_well = false;
                bad_status = ex.status;
                continue;
            }
            catch (const std::exception& ex)
            {
                LOG_EXCEPTION(LOG_TAG, ex);
                all_is_well = false;
                bad_status = PacketManager::SerialStatus::RecvFailed;
                continue;
            }
        }
    }
    catch (const SerialError& ex)
    {
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

    return all_is_well ? Status::Ok : ToStatus(bad_status);
}

// Do enroll session with the device using the given bgr24 face image
EnrollStatus FaceAuthenticatorCommon::EnrollImageImpl(MsgId msgId, const char* user_id, const unsigned char* buffer, unsigned int width,
                                                      unsigned int height)
{
    if (!ValidateUserId(user_id))
    {
        return EnrollStatus::Failure;
    }

    Status imageSendingStatus = SendImageToDevice(buffer, width, height);
    if (Status::Ok != imageSendingStatus)
    {
        LOG_ERROR(LOG_TAG, "Error sending the image to the device. status %d", static_cast<int>(imageSendingStatus));
        return EnrollStatus::Failure;
    }

    try
    {
        // Now that the image was uploaded, send the enroll image request
        StartSession();
        PacketManager::FaPacket fa_packet {msgId, user_id, 0};
        Send(fa_packet);
        RecvReplyMsg(fa_packet);
        return static_cast<EnrollStatus>(fa_packet.GetStatusCode());
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return ToEnrollStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        return EnrollStatus::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return EnrollStatus::Error;
    }
}

bool FaceAuthenticatorCommon::ProcessAuthPacket(MsgId msg_id, const PacketManager::FaPacket& packet, AuthenticationCallback& callback,
                                                Status& out_status)
{
    const auto fa_status = packet.GetStatusCode();
    const auto auth_status = static_cast<AuthenticateStatus>(fa_status);
    unsigned int ts = 0;

    switch (msg_id)
    {
    case MsgId::FaceDetected: {
        callback.OnFaceDetected(GetDetectedFaces(packet, ts), ts);
        break;
    }

    case MsgId::LandmarksDetected: {
        auto landmarks = GetDetectedLandmarks(packet, ts);
        if (!landmarks.empty())
            callback.OnLandmarksDetected(landmarks, ts);
        break;
    }

    case MsgId::FaceCroppedImage: {
        bool inProgress = true;
        if (GetFaceCroppedImage(packet, inProgress) && !inProgress)
        {
            callback.OnFaceCroppedImage(_image_downloader.buffer.data(), _image_downloader.w, _image_downloader.h, _image_downloader.ts);
        }
        break;
    }

    case MsgId::FaceDistances: {
        unsigned int ts = 0;
        auto distances = GetFaceDistances(packet, ts);
        if (!distances.empty())
            callback.OnFaceDistances(distances, ts);
        break;
    }

    case MsgId::Hint: {
        LOG_DEBUG(LOG_TAG, "Got Hint: %s", Description(auth_status));
        float frameScore = 0.0f;
        std::memcpy(&frameScore, packet.GetReserved(), sizeof(float));
        callback.OnHint(auth_status, frameScore);
        break;
    }

    case MsgId::Result: {
        LOG_DEBUG(LOG_TAG, "Got Result: %s", Description(auth_status));
        short score = 0;
        std::memcpy(&score, packet.GetReserved(), sizeof(short));
        callback.OnResult(auth_status, packet.GetUserId(), score);
        break;
    }

    case MsgId::Reply: {
        LOG_DEBUG(LOG_TAG, "Got Reply: %s", Description(auth_status));
        out_status = static_cast<Status>(fa_status);
        return false; // Terminal state reached
    }

    default:
        throw_unexpected_packet(msg_id);
    }

    return true; // Continue processing
}

// Do authenticate session with the device. Call user's authenticate callbacks in the process.
Status FaceAuthenticatorCommon::AuthenticateImpl(MsgId msgId, AuthenticationCallback& callback)
{
    try
    {
        StartSession();
        PacketManager::FaPacket fa_packet {msgId};
        Send(fa_packet);

        PacketManager::Timer session_timer {AUTH_MAX_TIMEOUT};
        Status final_status = Status::Ok;

        while (true)
        {
            if (session_timer.ReachedTimeout())
            {
                LOG_ERROR(LOG_TAG, "session timeout");
                callback.OnResult(AuthenticateStatus::Forbidden, "", 0);
                Cancel();
                return Status::Error;
            }

            auto received_id = Recv(fa_packet);

            // if it returns false, we received a 'Reply' and the session is over.
            if (!ProcessAuthPacket(received_id, fa_packet, callback, final_status))
            {
                return final_status;
            }
        }
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        callback.OnResult(ToAuthStatus(ex.status), "", 0);
        return ToStatus(ex.status);
    }
    catch (const std::exception& ex)
    {
        LOG_EXCEPTION(LOG_TAG, ex);
        callback.OnResult(AuthenticateStatus::Failure, "", 0);
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        callback.OnResult(AuthenticateStatus::Failure, "", 0);
        return Status::Error;
    }
}

#ifdef RSID_ONE2ONE

EnrollStatus FaceAuthenticatorCommon::EnrollImageOneToOne(const char* user_id, const unsigned char* buffer, unsigned int width,
                                                          unsigned int height)
{
    // normalize image using pipeline and send to enroll
    try
    {
        Pipeline::ImageResult normalized;
        auto pipeResult = NormalizeImage(buffer, width, height, normalized);
        if (pipeResult != Pipeline::ReturnCode::Success)
        {
            LOG_ERROR(LOG_TAG, "ProcessImage failed with status %d", pipeResult);
            return EnrollStatus::Failure;
        }
        LOG_INFO(LOG_TAG, "Enrolling image with size %ux%u", normalized.width, normalized.height);
        auto enrollResult = EnrollImageImpl(MsgId::EnrollImageOneToOne, user_id, normalized.data, normalized.width, normalized.height);
        return enrollResult;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR(LOG_TAG, "EnrollImage failed: %s", ex.what());
        return EnrollStatus::Failure;
    }
}

Status FaceAuthenticatorCommon::AuthenticateOneToOne(AuthenticationCallback& callback)
{
    return AuthenticateImpl(MsgId::AuthenticateOneToOne, callback);
}

AuthenticateStatus FaceAuthenticatorCommon::AuthenticateImageOneToOne(const unsigned char* buffer, unsigned int width, unsigned int height,
                                                                      std::string& user_id, short& score)
{
    // normalize image using pipeline and send to authenticate
    user_id.clear();
    try
    {
        Pipeline::ImageResult normalized;
        auto pipeResult = NormalizeImage(buffer, width, height, normalized);
        if (pipeResult != Pipeline::ReturnCode::Success)
        {
            LOG_ERROR(LOG_TAG, "ProcessImage failed with status %d", pipeResult);
            return AuthenticateStatus::Failure;
        }

        LOG_INFO(LOG_TAG, "Authenticating image with size %ux%u", normalized.width, normalized.height);
        Status imageSendingStatus = SendImageToDevice(normalized.data, normalized.width, normalized.height);
        if (Status::Ok != imageSendingStatus)
        {
            LOG_ERROR(LOG_TAG, "Error sending the image to the device. status %d", static_cast<int>(imageSendingStatus));
            return AuthenticateStatus::Failure;
        }

        // image was uploaded, send the authentication image request
        StartSession();
        PacketManager::FaPacket fa_packet {MsgId::AuthenticateImgOneToOne};
        Send(fa_packet);
        Recv(fa_packet);
        auto fa_status = fa_packet.GetStatusCode();
        user_id = fa_packet.GetUserId();
        memcpy(&score, fa_packet.GetReserved(), sizeof(short));
        return AuthenticateStatus(fa_status);
    }
    catch (const SerialError& ex)
    {
        LOG_ERROR(LOG_TAG, "%s (Status: %d)", ex.what(), static_cast<int>(ex.status));
        return AuthenticateStatus::Error;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR(LOG_TAG, "AuthenticateImage failed: %s", ex.what());
        return AuthenticateStatus::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "Unknown exception");
        return AuthenticateStatus::Failure;
    }
}

// normalize image using pipeline and then to extract faceprints using the pipeline
// no interaction with the device..
Status FaceAuthenticatorCommon::ExtractFaceprintsOnHost(const unsigned char* buffer, unsigned int width, unsigned int height,
                                                        ExtractedFaceprints* faceprints)
{
    if (faceprints == nullptr)
    {
        LOG_ERROR(LOG_TAG, "ExtractImageFaceprintsOnHost: faceprints is nullptr");
        return Status::Error;
    }
    try
    {
        Pipeline::ImageResult normalized;
        auto pipeResult = NormalizeImage(buffer, width, height, normalized);
        if (pipeResult != Pipeline::ReturnCode::Success)
        {
            LOG_ERROR(LOG_TAG, "ProcessImage failed with status %d", pipeResult);
            return Status::Error;
        }

        std::vector<short> features;
        pipeResult = GetPipeLine()->ExtractFeatures(normalized, features);
        if (pipeResult != Pipeline::ReturnCode::Success)
        {
            LOG_ERROR(LOG_TAG, "ExtractFeatures failed with status %d", pipeResult);
            return Status::Error;
        }

        // put result in the pExtractedFaceprints var
        faceprints->data.version = RSID_FACEPRINTS_VERSION;
        faceprints->data.featuresType = 0;
        faceprints->data.flags = OpFlagEnrollWithoutMask;
        std::memset(faceprints->data.featuresVector, 0, sizeof(faceprints->data.featuresVector));
        if (features.size() != RSID_NUM_OF_RECOGNITION_FEATURES)
        {
            LOG_ERROR(LOG_TAG, "ExtractFeatures size mismatch. Expected %u. Got: %u", RSID_NUM_OF_RECOGNITION_FEATURES, features.size());
            return Status::Error;
        }
        static_assert(std::is_same_v<decltype(features)::value_type, feature_t>, "features vector must be feature_t");
        std::memcpy(faceprints->data.featuresVector, features.data(), features.size() * sizeof(feature_t));
        return Status::Ok;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR(LOG_TAG, "ExtractImageFaceprints failed: %s", ex.what());
        return Status::Error;
    }
}

Status FaceAuthenticatorCommon::DetectFace(const unsigned char* buffer, unsigned int width, unsigned int height, FaceRect& result)
{
    try
    {
        auto pipe = GetPipeLine();
        Pipeline::FaceBox faceBox;
        Pipeline::Image input {static_cast<uint16_t>(width), static_cast<uint16_t>(height), 3, (uint8_t*)buffer, Pipeline::Image::BGR};
        auto pipeResult = pipe->DetectFace(input, faceBox);

        if (pipeResult != Pipeline::ReturnCode::Success)
        {
            if (pipeResult != Pipeline::ReturnCode::NoFaceDetected)
            {
                LOG_ERROR(LOG_TAG, "DetectFace() failed with status %d (%s)", static_cast<int>(pipeResult),
                          Pipeline::Api::ToString(pipeResult));
            }
            result = FaceRect {0, 0, 0, 0};
            return Status::Error;
        }

        result.x = faceBox.x;
        result.y = faceBox.y;
        result.w = faceBox.w;
        result.h = faceBox.h;
        return Status::Ok;
    }
    catch (const std::exception& ex)
    {
        LOG_ERROR(LOG_TAG, "DetectFace failed: %s", ex.what());
        return Status::Error;
    }
    catch (...)
    {
        LOG_ERROR(LOG_TAG, "DetectFace failed with unknown exception");
        return Status::Error;
    }
}

// create and initialize pipeline once
std::shared_ptr<Pipeline::Api> FaceAuthenticatorCommon::GetPipeLine()
{
    static auto pipe = Pipeline::Api::Create();
    return pipe;
}

// create normalized image using pipeline
// throw on error
Pipeline::ReturnCode FaceAuthenticatorCommon::NormalizeImage(const unsigned char* buffer, unsigned int width, unsigned int height,
                                                             Pipeline::ImageResult& result)
{
    auto pipe = GetPipeLine();
    Pipeline::Image input {static_cast<uint16_t>(width), static_cast<uint16_t>(height), 3, (uint8_t*)buffer, Pipeline::Image::BGR};
    return pipe->ProcessImage(input, result);
}

#endif // RSID_ONE2ONE

void FaceAuthenticatorCommon::SendProgress(bool msg)
{
    // send progress message to let device know that chunk was received
    PacketManager::FaPacket fa_packet {MsgId::Progress};
    fa_packet.payload.message.fa_msg.fa_status = msg ? '0' : '1';
    Send(fa_packet);
}

bool FaceAuthenticatorCommon::GetFaceCroppedImage(const PacketManager::SerialPacket& packet, bool& inProgress)
{
    assert(packet.header.id == MsgId::FaceCroppedImage);

    auto* data = packet.payload.message.data_msg.data;
    auto constexpr chunk_size = static_cast<size_t>(7.5 * 1024);
    static_assert(chunk_size <= sizeof(packet.payload.message.data_msg.data), "invalid chunk size");
    static_assert(chunk_size > 128, "invalid chunk size");
    size_t image_chunk_size = chunk_size - 10;
    // first 10 bytes are: chunk_n(2), width(2), height(2), ts(4)
    uint16_t chunk_n, width, height;
    uint32_t ts = 0;
    ::memcpy(&chunk_n, &data[0], sizeof(chunk_n));
    ::memcpy(&width, &data[2], sizeof(width));
    ::memcpy(&height, &data[4], sizeof(height));
    ::memcpy(&ts, &data[6], sizeof(ts));
    size_t image_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;

    if (image_size == 0 || image_size > MAX_UPLOAD_IMG_SIZE)
    {
        LOG_ERROR(LOG_TAG, "Invalid image size %u", image_size);
        return false;
    }

    auto n_chunks = image_size / image_chunk_size;
    auto last_chunk_size = image_size % image_chunk_size;
    if (last_chunk_size > 0)
    {
        n_chunks++;
    }
    LOG_DEBUG(LOG_TAG, "Received chunk %u/%u", chunk_n + 1, n_chunks);

    // first chunk. allocate buffer and
    if (chunk_n == 0)
    {
        _image_downloader.OnStart(static_cast<unsigned>(width), static_cast<unsigned>(height), ts);
    }

    bool is_last_chunk = ((chunk_n + 1u) == n_chunks);
    uint16_t cur_chunk_size = (is_last_chunk && (last_chunk_size > 0)) ? (uint16_t)last_chunk_size : (uint16_t)image_chunk_size;

    if (!_image_downloader.AddChunk(chunk_n, (unsigned char*)&data[10], cur_chunk_size))
    {
        LOG_DEBUG(LOG_TAG, "Failed to add chunk %u/%u", chunk_n + 1, n_chunks);
        return false;
    }

    if (is_last_chunk)
    {
        LOG_DEBUG(LOG_TAG, "Image downloaded. %ux%u, %u bytes, %u chunks %zu millis", _image_downloader.w, _image_downloader.h,
                  _image_downloader.buffer.size(), n_chunks, _image_downloader.timer.Elapsed().count());
        inProgress = false;
    }

    return true;
}

// Start a session with the device.
// Try several times if needed.
void FaceAuthenticatorCommon::StartSession()
{
    if (!_serial)
    {
        throw std::runtime_error("Serial port not connected");
    }
    auto status = PacketManager::SerialStatus::RecvTimeout;
    constexpr int max_tries = 3;
    constexpr std::chrono::milliseconds retry_delay(250);
    for (int tries = 0; tries < max_tries; tries++)
    {
        LOG_DEBUG(LOG_TAG, "Session start (try #%d)", tries + 1);
        status = _session.Start(_serial.get());
        if (status == PacketManager::SerialStatus::Ok)
        {
            return; // success
        }

        // attempt to cancel previous session before retrying
        auto ignored = _session.SendCancel();
        (void)ignored;
        if (tries < max_tries - 1)
        {
            std::this_thread::sleep_for(retry_delay);
        }
    }
    throw SerialError(status, "Failed starting session");
}

void FaceAuthenticatorCommon::Send(PacketManager::SerialPacket& packet)
{
    auto status = _session.SendPacket(packet);
    if (status != PacketManager::SerialStatus::Ok)
    {
        throw SerialError(status, "Failed sending serial packet");
    }
}

MsgId FaceAuthenticatorCommon::Recv(PacketManager::SerialPacket& packet)
{
    auto status = _session.RecvPacket(packet);
    if (status != PacketManager::SerialStatus::Ok)
    {
        throw SerialError(status, "Failed receiving serial packet");
    }
    return packet.header.id;
}

void FaceAuthenticatorCommon::Recv(PacketManager::SerialPacket& packet, MsgId expected_id)
{
    auto received_id = Recv(packet);
    if (received_id != expected_id)
    {
        throw_unexpected_packet(received_id, expected_id);
    }
}

void FaceAuthenticatorCommon::RecvReplyMsg(PacketManager::SerialPacket& packet)
{
    Recv(packet, MsgId::Reply);
}

} // namespace Impl
} // namespace RealSenseID