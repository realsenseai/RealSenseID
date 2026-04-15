// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/DeviceConfig.h"
#include "RealSenseID/AuthenticationCallback.h"
#include "RealSenseID/AuthFaceprintsExtractionCallback.h"
#include "RealSenseID/EnrollFaceprintsExtractionCallback.h"
#include "RealSenseID/EnrollmentCallback.h"
#include "RealSenseID/Faceprints.h"
#include "RealSenseID/SerialConfig.h"
#include "RealSenseID/SignatureCallback.h"
#include "RealSenseID/Status.h"
#include "RealSenseID/MatcherDefines.h"

#ifdef RSID_SECURE
#include "PacketManager/SecureSession.h"
using Session = RealSenseID::PacketManager::SecureSession;
#else
#include "PacketManager/NonSecureSession.h"
using Session = RealSenseID::PacketManager::NonSecureSession;
#endif // RSID_SECURE

#ifdef RSID_ONE2ONE
#include "RealSenseID/pipeline.h"
#endif

#include "IFaceAuthenticator.h"

#include <memory>
#include <atomic>
#include <chrono>
#include <string>
#include <stdexcept>


namespace RealSenseID
{
namespace Impl
{
struct DownloadImage
{
    std::vector<unsigned char> buffer;
    unsigned int w = 0;
    unsigned int h = 0;
    unsigned int ts = 0;
    unsigned int last_chunk = 0;
    PacketManager::Timer timer;

    void OnStart(unsigned int width, unsigned int height, unsigned timestamp);
    bool AddChunk(unsigned short chunk_n, unsigned char* data, size_t size);
};

class FaceAuthenticatorCommon : public IFaceAuthenticator
{
public:
    explicit FaceAuthenticatorCommon(SignatureCallback* callback);
    ~FaceAuthenticatorCommon() override = default;

    FaceAuthenticatorCommon(const FaceAuthenticatorCommon&) = delete;
    FaceAuthenticatorCommon& operator=(const FaceAuthenticatorCommon&) = delete;
    FaceAuthenticatorCommon(FaceAuthenticatorCommon&&) = delete;
    FaceAuthenticatorCommon& operator=(FaceAuthenticatorCommon&&) = delete;

    Status Connect(const SerialConfig& config) override;
    void Disconnect() override;

#ifdef RSID_SECURE
    Status Pair(const char* ecdsaHostPubKey, const char* ecdsaHostPubKeySig, char* ecdsaDevicePubKey) override;
    Status Unpair() override;
#endif

    Status Enroll(EnrollmentCallback& callback, const char* user_id) override;
    EnrollStatus EnrollImage(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height) override;
    EnrollStatus EnrollImageFeatureExtraction(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height,
                                              ExtractedFaceprints* faceprints) override;
    Status Authenticate(AuthenticationCallback& callback) override;
    Status AuthenticateLoop(AuthenticationCallback& callback) override;
    Status Cancel() override;
    Status RemoveUser(const char* user_id) override;
    Status RemoveAll() override;

    Status SetDeviceConfig(const DeviceConfig& device_config) override;
    Status QueryDeviceConfig(DeviceConfig& device_config) override;
    Status QueryUserIds(char** user_ids, unsigned int& number_of_users) override;
    Status QueryNumberOfUsers(unsigned int& number_of_users) override;
    Status Standby() override;
    Status Hibernate() override;
    Status Unlock() override;

#ifdef RSID_ONE2ONE
    EnrollStatus EnrollImageOneToOne(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height) override;
    Status AuthenticateOneToOne(AuthenticationCallback& callback) override;
    AuthenticateStatus AuthenticateImageOneToOne(const unsigned char* buffer, unsigned int width, unsigned int height, std::string& user_id,
                                                 short& score) override;
    Status ExtractFaceprintsOnHost(const unsigned char* buffer, unsigned int width, unsigned int height,
                                   ExtractedFaceprints* pExtractedFaceprints) override;

    Status DetectFace(const unsigned char* buffer, unsigned int width, unsigned int height, FaceRect& result) override;

#endif // RSID_ONE2ONE

    Status SendImageToDevice(const unsigned char* buffer, unsigned int width, unsigned int height) override;
    Status ExtractFaceprintsForEnroll(EnrollFaceprintsExtractionCallback& callback) override;
    Status ExtractFaceprintsForAuth(AuthFaceprintsExtractionCallback& callback) override;
    Status ExtractFaceprintsForAuthLoop(AuthFaceprintsExtractionCallback& callback) override;

    MatchResultHost MatchFaceprints(MatchElement& new_faceprints, Faceprints& existing_faceprints, Faceprints& updated_faceprints,
                                    ThresholdsConfidenceEnum matcher_confidence_level) override;

    Status GetUsersFaceprints(Faceprints* user_features, unsigned int& num_of_users) override;
    Status SetUsersFaceprints(UserFaceprints* users_faceprints, unsigned int num_of_users) override;

protected:
#ifdef RSID_SECURE
    // in secure mode sleep less to take into account start session duration
    const std::chrono::milliseconds _loop_interval_no_face {1500};
    const std::chrono::milliseconds _loop_interval_with_face {100};
#else
    const std::chrono::milliseconds _loop_interval_no_face {2100};
    const std::chrono::milliseconds _loop_interval_with_face {600};
#endif
    std::atomic<bool> _cancel_loop {false};
    std::unique_ptr<PacketManager::SerialConnection> _serial;
    Session _session;
    DownloadImage _image_downloader;

    static bool ValidateUserId(const char* user_id);
    Status SendUserFaceprints(UserFaceprints& features);
    EnrollStatus EnrollImageImpl(PacketManager::MsgId msgId, const char* user_id, const unsigned char* buffer, unsigned int width,
                                 unsigned int height);
    bool ProcessAuthPacket(PacketManager::MsgId msg_id, const PacketManager::FaPacket& packet, AuthenticationCallback& callback,
                           Status& out_status);
    Status AuthenticateImpl(PacketManager::MsgId msgId, AuthenticationCallback& callback);

#ifdef RSID_ONE2ONE
    // one to one pipeline singleton
    static std::shared_ptr<Pipeline::Api> GetPipeLine();
    // normalized image from buffer to the result image
    // return ReturnCode::Ok on success
    Pipeline::ReturnCode NormalizeImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                                        Pipeline::ImageResult& result);
#endif

    void SendProgress(bool status);
    bool GetFaceCroppedImage(const PacketManager::SerialPacket& packet, bool& inProgress);

    // helpers for sending and receiving packets
    struct SerialError : public std::runtime_error
    {
        PacketManager::SerialStatus status;
        SerialError(PacketManager::SerialStatus s, const char* msg) : std::runtime_error(msg), status(s)
        {
        }
    };

    // all those will throw SerialError on failure
    void StartSession();
    void Send(PacketManager::SerialPacket& packet);
    PacketManager::MsgId Recv(PacketManager::SerialPacket& packet);
    void Recv(PacketManager::SerialPacket& packet, PacketManager::MsgId expected_id);
    void RecvReplyMsg(PacketManager::SerialPacket& packet);


    // RAII to cancel the session at the exit of detection/auth loops
    struct AutoCanceler
    {
        Session* _session;
        AutoCanceler(Session* session);
        ~AutoCanceler();
        void DoNotCancel();
    };
};
} // namespace Impl
} // namespace RealSenseID
