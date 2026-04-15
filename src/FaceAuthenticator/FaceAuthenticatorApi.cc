// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DeviceConfig.h"
#include "Impl/FaceAuthenticatorCommon.h"
#include "Impl/FaceAuthenticatorF45x.h"
#include "Impl/FaceAuthenticatorF50x.h"
#include "Logger.h"

#include <stdexcept>

static const char* LOG_TAG = "FaceAuthenticatorApi";

namespace RealSenseID
{
#ifdef RSID_SECURE
FaceAuthenticator::FaceAuthenticator(SignatureCallback* callback, DeviceType device_type)
{
    switch (device_type)
    {
    case DeviceType::F45x:
        _impl = new Impl::FaceAuthenticatorF45x(callback);
        break;
    case DeviceType::F50x:
        throw std::invalid_argument("RSID_SECURE is not supported in F50x");
    default:
        throw std::invalid_argument("Unknown device type");
    }
}
#else
FaceAuthenticator::FaceAuthenticator(DeviceType device_type)
{
    switch (device_type)
    {
    case DeviceType::F45x:
        _impl = new Impl::FaceAuthenticatorF45x();
        break;
    case DeviceType::F50x:
        _impl = new Impl::FaceAuthenticatorF50x();
        break;
    default:
        LOG_ERROR(LOG_TAG, "Unknown device type");
        throw std::invalid_argument("Unknown device type");
    }
}
#endif

FaceAuthenticator::~FaceAuthenticator()
{
    delete _impl;
    _impl = nullptr;
}

// Move constructor
FaceAuthenticator::FaceAuthenticator(FaceAuthenticator&& other) noexcept
{
    _impl = other._impl;
    other._impl = nullptr;
}

// Move assignment
FaceAuthenticator& FaceAuthenticator::operator=(FaceAuthenticator&& other) noexcept
{
    if (this != &other)
    {
        delete _impl;
        _impl = other._impl;
        other._impl = nullptr;
    }
    return *this;
}


Status FaceAuthenticator::Connect(const SerialConfig& config)
{
    return _impl->Connect(config);
}

void FaceAuthenticator::Disconnect()
{
    _impl->Disconnect();
}

#ifdef RSID_SECURE
Status FaceAuthenticator::Pair(const char* ecdsa_host_pubKey, const char* ecdsa_host_pubkey_sig, char* ecdsa_device_pubkey)
{
    return _impl->Pair(ecdsa_host_pubKey, ecdsa_host_pubkey_sig, ecdsa_device_pubkey);
}

Status FaceAuthenticator::Unpair()
{
    return _impl->Unpair();
}
#endif // RSID_SECURE


Status FaceAuthenticator::Enroll(EnrollmentCallback& callback, const char* user_id)
{
    return _impl->Enroll(callback, user_id);
}

EnrollStatus FaceAuthenticator::EnrollImage(const char* user_id, const unsigned char* buffer, unsigned int width, unsigned int height)
{
    return _impl->EnrollImage(user_id, buffer, width, height);
}

EnrollStatus FaceAuthenticator::EnrollImageFeatureExtraction(const char* user_id, const unsigned char* buffer, unsigned int width,
                                                             unsigned int height, ExtractedFaceprints* pExtractedFaceprints)
{
    return _impl->EnrollImageFeatureExtraction(user_id, buffer, width, height, pExtractedFaceprints);
}

Status FaceAuthenticator::Authenticate(AuthenticationCallback& callback)
{
    return _impl->Authenticate(callback);
}

Status FaceAuthenticator::AuthenticateLoop(AuthenticationCallback& callback)
{
    return _impl->AuthenticateLoop(callback);
}

Status FaceAuthenticator::DetectPersons(const PersonCallback& callback, bool loop)
{
    return _impl->DetectPersons(callback, loop);
}

Status FaceAuthenticator::DetectPoses(const PoseCallback& callback, bool loop)
{
    return _impl->DetectPoses(callback, loop);
}

Status FaceAuthenticator::DecodeBarcodes(const BarcodeCallback& callback, bool loop)
{
    return _impl->DecodeBarcodes(callback, loop);
}

Status FaceAuthenticator::DetectBodyParts(const BodyPartCallback& callback, bool loop)
{
    return _impl->DetectBodyParts(callback, loop);
}

Status FaceAuthenticator::Cancel()
{
    return _impl->Cancel();
}

Status FaceAuthenticator::RemoveUser(const char* user_id)
{
    return _impl->RemoveUser(user_id);
}

Status FaceAuthenticator::RemoveAll()
{
    return _impl->RemoveAll();
}

Status FaceAuthenticator::SetDeviceConfig(const DeviceConfig& device_config)
{
    return _impl->SetDeviceConfig(device_config);
}

Status FaceAuthenticator::QueryDeviceConfig(DeviceConfig& device_config)
{
    return _impl->QueryDeviceConfig(device_config);
}

Status FaceAuthenticator::QueryUserIds(char** user_ids, unsigned int& number_of_users)
{
    return _impl->QueryUserIds(user_ids, number_of_users);
}

Status FaceAuthenticator::QueryNumberOfUsers(unsigned int& number_of_users)
{
    return _impl->QueryNumberOfUsers(number_of_users);
}

Status FaceAuthenticator::Standby()
{
    return _impl->Standby();
}

Status FaceAuthenticator::Hibernate()
{
    return _impl->Hibernate();
}

Status FaceAuthenticator::Unlock()
{
    return _impl->Unlock();
}

Status FaceAuthenticator::ExtractFaceprintsForEnroll(EnrollFaceprintsExtractionCallback& callback)
{
    return _impl->ExtractFaceprintsForEnroll(callback);
}

Status FaceAuthenticator::ExtractFaceprintsForAuth(AuthFaceprintsExtractionCallback& callback)
{
    return _impl->ExtractFaceprintsForAuth(callback);
}

Status FaceAuthenticator::ExtractFaceprintsForAuthLoop(AuthFaceprintsExtractionCallback& callback)
{
    return _impl->ExtractFaceprintsForAuthLoop(callback);
}

MatchResultHost FaceAuthenticator::MatchFaceprints(MatchElement& new_faceprints, Faceprints& existing_faceprints,
                                                   Faceprints& updated_faceprints, ThresholdsConfidenceEnum matcher_confidence_level)
{
    return _impl->MatchFaceprints(new_faceprints, existing_faceprints, updated_faceprints, matcher_confidence_level);
}

Status FaceAuthenticator::GetUsersFaceprints(Faceprints* user_features, unsigned int& num_of_users)
{
    return _impl->GetUsersFaceprints(user_features, num_of_users);
}

Status FaceAuthenticator::SetUsersFaceprints(UserFaceprints* user_features, unsigned int num_of_users)
{
    return _impl->SetUsersFaceprints(user_features, num_of_users);
}

Status FaceAuthenticator::DumpAndMount()
{
    return _impl->DumpAndMount();
}

Status FaceAuthenticator::MountDebug()
{
    return _impl->MountDebug();
}

#ifdef RSID_ONE2ONE
EnrollStatus FaceAuthenticator::EnrollImageOneToOne(const char* user_id, const unsigned char* buffer, unsigned int width,
                                                    unsigned int height)
{
    return _impl->EnrollImageOneToOne(user_id, buffer, width, height);
}

Status FaceAuthenticator::AuthenticateOneToOne(AuthenticationCallback& callback)
{
    return _impl->AuthenticateOneToOne(callback);
}

AuthenticateStatus FaceAuthenticator::AuthenticateImageOneToOne(const unsigned char* buffer, unsigned int width, unsigned int height,
                                                                std::string& user_id, short& score)
{
    return _impl->AuthenticateImageOneToOne(buffer, width, height, user_id, score);
}
Status FaceAuthenticator::ExtractFaceprintsOnHost(const unsigned char* buffer, unsigned int width, unsigned int height,
                                                  ExtractedFaceprints* pExtractedFaceprints)
{
    return _impl->ExtractFaceprintsOnHost(buffer, width, height, pExtractedFaceprints);
}

Status FaceAuthenticator::DetectFace(const unsigned char* buffer, unsigned int width, unsigned int height, FaceRect& result)
{
    return _impl->DetectFace(buffer, width, height, result);
}
#endif // RSID_ONE2ONE
} // namespace RealSenseID