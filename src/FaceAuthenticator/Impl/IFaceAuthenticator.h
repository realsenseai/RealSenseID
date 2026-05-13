// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseID/DeviceConfig.h"
#include "RealSenseID/FaceAuthenticator.h"

#ifdef RSID_SECURE
#include "PacketManager/SecureSession.h"
using Session = RealSenseID::PacketManager::SecureSession;
#else
#include "PacketManager/NonSecureSession.h"
using Session = RealSenseID::PacketManager::NonSecureSession;
#endif // RSID_SECURE

namespace RealSenseID
{
namespace Impl
{
class IFaceAuthenticator
{
public:
    virtual ~IFaceAuthenticator() = default;
    virtual Status Connect(const SerialConfig& config) = 0;
    virtual void Disconnect() = 0;

#ifdef RSID_SECURE
    virtual Status Pair(const char* ecdsaHostPubKey, const char* ecdsaHostPubKeySig, char* ecdsaDevicePubKey) = 0;
    virtual Status Unpair() = 0;
#endif

    virtual Status Enroll(EnrollmentCallback& callback, const char* user_id) = 0;
    virtual EnrollStatus EnrollImage(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height) = 0;
    virtual EnrollStatus EnrollImageFeatureExtraction(const char* user_id, const unsigned char* buffer, unsigned int width,
                                                      unsigned int height, ExtractedFaceprints* faceprints) = 0;
    virtual Status Authenticate(AuthenticationCallback& callback) = 0;
    virtual Status AuthenticateLoop(AuthenticationCallback& callback) = 0;
    virtual Status Cancel() = 0;
    virtual Status RemoveUser(const char* user_id) = 0;
    virtual Status RemoveAll() = 0;

    virtual Status SetDeviceConfig(const DeviceConfig& device_config) = 0;
    virtual Status QueryDeviceConfig(DeviceConfig& device_config) = 0;
    virtual Status QueryUserIds(char** user_ids, unsigned int& number_of_users) = 0;
    virtual Status QueryNumberOfUsers(unsigned int& number_of_users) = 0;
    virtual Status Standby() = 0;
    virtual Status Hibernate() = 0;
    virtual Status Unlock() = 0;

    virtual EnrollStatus EnrollImageOneToOne(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height) = 0;
    virtual Status AuthenticateOneToOne(AuthenticationCallback& callback) = 0;
    virtual AuthenticateStatus AuthenticateImageOneToOne(const unsigned char* buffer, unsigned int width, unsigned int height,
                                                         std::string& user_id, short& score) = 0;
    virtual Status DetectFace(const unsigned char* buffer, unsigned int width, unsigned int height, FaceRect& result, bool expand_roi) = 0;

    virtual Status SendImageToDevice(const unsigned char* buffer, unsigned int width, unsigned int height) = 0;
    virtual Status ExtractFaceprintsForEnroll(EnrollFaceprintsExtractionCallback& callback) = 0;
    virtual Status ExtractFaceprintsForAuth(AuthFaceprintsExtractionCallback& callback) = 0;
    virtual Status ExtractFaceprintsForAuthLoop(AuthFaceprintsExtractionCallback& callback) = 0;

    virtual MatchResultHost MatchFaceprints(MatchElement& new_faceprints, Faceprints& existing_faceprints,
                                            Faceprints& updated_faceprints) = 0;

    virtual Status GetUsersFaceprints(Faceprints* user_features, unsigned int& num_of_users) = 0;
    virtual Status SetUsersFaceprints(UserFaceprints* users_faceprints, unsigned int num_of_users) = 0;
    virtual Status DumpAndMount() = 0;
    virtual Status MountDebug() = 0;
    virtual Status DetectPersons(const RealSenseID::FaceAuthenticator::PersonCallback& callback, bool loop) = 0;
    virtual Status DetectPoses(const RealSenseID::FaceAuthenticator::PoseCallback& callback, bool loop) = 0;
    virtual Status DetectBodyParts(const RealSenseID::FaceAuthenticator::BodyPartCallback& callback, bool loop) = 0;
    virtual Status DecodeBarcodes(const RealSenseID::FaceAuthenticator::BarcodeCallback& callback, bool loop) = 0;
};

} // namespace Impl
} // namespace RealSenseID
