// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "RealSenseID/SerialConfig.h"
#include "RealSenseID/DiscoverDevices.h"
#include "RealSenseID/EnrollmentCallback.h"
#include "RealSenseID/AuthenticationCallback.h"
#include "RealSenseID/FaceAuthenticator.h"
#include "RealSenseID/DeviceConfig.h"
#include "RealSenseID/Version.h"
#include "RealSenseID/Logging.h"
#include "RealSenseID/Faceprints.h"
#include "RealSenseID/MatcherDefines.h"
#include "RealSenseID/AuthFaceprintsExtractionCallback.h"
#include "RealSenseID/EnrollFaceprintsExtractionCallback.h"
#include "RealSenseID/SignatureCallback.h"
#include "rsid_c/rsid_client.h"

#include <memory>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string.h>

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 26812) // supress msvc's unscoped enum warnings
#endif                           // _WIN32

namespace
{
using RealSenseID::ExtractedFaceprints;
using RealSenseID::Faceprints;
using RealSenseID::FaOperationFlagsEnum;
using RealSenseID::FaVectorFlagsEnum;
using RealSenseID::MatchElement;

constexpr unsigned int NUM_LANDMARKS = 5;
constexpr unsigned int MAX_USERS = 10000;

// copy FaceRects to give c array of rsid_face_rects and return number the faces copied
size_t to_c_faces(const std::vector<RealSenseID::FaceRect>& faces, rsid_face_rect target[], size_t target_size)
{
    size_t i;
    for (i = 0; i < faces.size() && i < target_size; i++)
    {
        const auto& face = faces[i];
        target[i] = {face.x, face.y, face.w, face.h};
    }
    return i;
}

// copy FaceLandmarks to give c array of rsid_face_landmarks and return number the faces copied
size_t to_c_landmarks(const std::vector<RealSenseID::FaceLandmarks>& landmarks, rsid_face_landmarks target[], size_t target_size)
{
    size_t i, l;
    for (i = 0; i < landmarks.size() && i < target_size; i++)
    {
        const auto& lms = landmarks[i];
        for (l = 0; l < NUM_LANDMARKS; l++)
        {
            target[i].lm_x[l] = lms.lm_x[l];
            target[i].lm_y[l] = lms.lm_y[l];
        }
    }
    return i;
}

// helper to convert the face vector to c array of rsid_face_rect structs and call the c callback
void handle_face_detected_clbk(rsid_face_detected_clbk user_clbk, const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts,
                               void* ctx)
{
    if (user_clbk != nullptr && !faces.empty())
    {
        rsid_face_rect c_faces[RSID_MAX_FACES];
        auto n_faces = to_c_faces(faces, c_faces, RSID_MAX_FACES);
        user_clbk(c_faces, n_faces, ts, ctx);
    }
}

// helper to convert the landmarks vector to c array of rsid_face_landmarks structs and call the c callback
void handle_landmarks_detected_clbk(rsid_landmarks_detected_clbk user_clbk, const std::vector<RealSenseID::FaceLandmarks>& landmarks,
                                    const unsigned int ts, void* ctx)
{
    if (user_clbk != nullptr && !landmarks.empty())
    {
        rsid_face_landmarks c_landmarks[RSID_MAX_FACES];
        auto n_faces = to_c_landmarks(landmarks, c_landmarks, RSID_MAX_FACES);
        user_clbk(c_landmarks, n_faces, ts, ctx);
    }
}

// copy PersonRects to given c array of rsid_person_rects and return number the persons copied
size_t to_c_persons(const std::vector<RealSenseID::PersonRect>& persons, rsid_person_rect targets[], size_t target_size)
{
    size_t i;
    for (i = 0; i < persons.size() && i < target_size; i++)
    {
        const auto& person = persons[i];
        auto& target = targets[i];
        target = {person.x, person.y, person.w, person.h, person.id, person.distance};
        target.body_part = static_cast<rsid_body_part_type>(person.body_part);
        target.score = person.score;
    }
    return i;
}

// copy poses to given c array of rsid_pose and return number the poses copied
size_t to_c_poses(const std::vector<RealSenseID::PersonPose>& poses, rsid_person_pose targets[], size_t target_size)
{
    size_t i;
    for (i = 0; i < poses.size() && i < target_size; i++)
    {
        const auto& pose = poses[i];
        auto& target = targets[i];
        target.x = pose.x;
        target.y = pose.y;
        target.w = pose.w;
        target.h = pose.h;
        std::copy(std::begin(pose.lm_x), std::end(pose.lm_x), std::begin(target.lm_x));
        std::copy(std::begin(pose.lm_y), std::end(pose.lm_y), std::begin(target.lm_y));
        std::copy(std::begin(pose.lm_score), std::end(pose.lm_score), std::begin(target.lm_score));
    }
    return i;
}

void handle_face_distances_clbk(rsid_face_distances_clbk user_clbk, const std::vector<double>& distances, unsigned int ts, void* ctx)
{
    if (user_clbk != nullptr && !distances.empty())
    {
        user_clbk(distances.data(), distances.size(), ts, ctx);
    }
}

class EnrollClbk : public RealSenseID::EnrollmentCallback
{
    rsid_enroll_args _enroll_args;

public:
    explicit EnrollClbk(rsid_enroll_args args) : _enroll_args {args}
    {
    }

    void OnResult(const RealSenseID::EnrollStatus status) override
    {
        if (_enroll_args.status_clbk)
            _enroll_args.status_clbk(static_cast<rsid_enroll_status>(status), _enroll_args.ctx);
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        if (_enroll_args.progress_clbk)
            _enroll_args.progress_clbk(static_cast<rsid_face_pose>(pose), _enroll_args.ctx);
    }

    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override
    {
        if (_enroll_args.hint_clbk)
            _enroll_args.hint_clbk(static_cast<rsid_enroll_status>(hint), frameScore, _enroll_args.ctx);
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        handle_face_detected_clbk(_enroll_args.face_detected_clbk, faces, ts, _enroll_args.ctx);
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        handle_landmarks_detected_clbk(_enroll_args.landmarks_detected_clbk, landmarks, ts, _enroll_args.ctx);
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_enroll_args.face_cropped_image_clbk)
            _enroll_args.face_cropped_image_clbk(buffer, width, height, ts, _enroll_args.ctx);
    }
};

class AuthClbk : public RealSenseID::AuthenticationCallback
{
    rsid_auth_args _auth_args;

public:
    explicit AuthClbk(rsid_auth_args args) : _auth_args {args}
    {
    }

    void OnResult(const RealSenseID::AuthenticateStatus status, const char* userId, short score) override
    {
        if (_auth_args.result_clbk)
            _auth_args.result_clbk(static_cast<rsid_auth_status>(status), (const char*)userId, score, _auth_args.ctx);
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        if (_auth_args.hint_clbk)
            _auth_args.hint_clbk(static_cast<rsid_auth_status>(hint), frameScore, _auth_args.ctx);
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        handle_face_detected_clbk(_auth_args.face_detected_clbk, faces, ts, _auth_args.ctx);
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        handle_landmarks_detected_clbk(_auth_args.landmarks_detected_clbk, landmarks, ts, _auth_args.ctx);
    }

    void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts) override
    {
        handle_face_distances_clbk(_auth_args.face_distances_clbk, distances, ts, _auth_args.ctx);
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_auth_args.face_cropped_image_clbk)
            _auth_args.face_cropped_image_clbk(buffer, width, height, ts, _auth_args.ctx);
    }
};

void copy_to_c_faceprints_ple_ple(const RealSenseID::ExtractedFaceprints& faceprints, rsid_extracted_faceprints_t* c_faceprints)
{
    c_faceprints->version = faceprints.data.version;
    c_faceprints->featuresType = static_cast<int>(faceprints.data.featuresType);
    c_faceprints->flags = faceprints.data.flags;

    static_assert(sizeof(c_faceprints->featuresVector) == sizeof(faceprints.data.featuresVector),
                  "adaptive faceprints (without mask) sizes does not match");
    ::memcpy(c_faceprints->featuresVector, faceprints.data.featuresVector, sizeof(faceprints.data.featuresVector));
}

void copy_to_c_faceprints_ple_dble_for_enroll(const RealSenseID::ExtractedFaceprints& faceprints, rsid_faceprints_t* c_faceprints)
{
    c_faceprints->version = faceprints.data.version;
    c_faceprints->featuresType = static_cast<int>(faceprints.data.featuresType);
    c_faceprints->flags = faceprints.data.flags;

    static_assert(sizeof(c_faceprints->adaptiveDescriptorWithoutMask) == sizeof(faceprints.data.featuresVector),
                  "adaptive faceprints (without mask) sizes does not match");
    ::memcpy(c_faceprints->adaptiveDescriptorWithoutMask, faceprints.data.featuresVector, sizeof(faceprints.data.featuresVector));

    static_assert(sizeof(c_faceprints->enrollmentDescriptor) == sizeof(faceprints.data.featuresVector),
                  "enrollment faceprints sizes does not match");
    ::memcpy(c_faceprints->enrollmentDescriptor, faceprints.data.featuresVector, sizeof(faceprints.data.featuresVector));
}


void copy_to_c_faceprints_dble_dble(const RealSenseID::Faceprints& faceprints, rsid_faceprints_t* c_faceprints)
{
    c_faceprints->version = faceprints.data.version;
    c_faceprints->featuresType = static_cast<int>(faceprints.data.featuresType);
    c_faceprints->flags = faceprints.data.flags;

    static_assert(sizeof(c_faceprints->adaptiveDescriptorWithoutMask) == sizeof(faceprints.data.adaptiveDescriptorWithoutMask),
                  "adaptive faceprints (without mask) sizes does not match");
    ::memcpy(c_faceprints->adaptiveDescriptorWithoutMask, faceprints.data.adaptiveDescriptorWithoutMask,
             sizeof(faceprints.data.adaptiveDescriptorWithoutMask));

    static_assert(sizeof(c_faceprints->enrollmentDescriptor) == sizeof(faceprints.data.enrollmentDescriptor),
                  "enrollment faceprints sizes does not match");
    ::memcpy(c_faceprints->enrollmentDescriptor, faceprints.data.enrollmentDescriptor, sizeof(faceprints.data.enrollmentDescriptor));
}

void copy_to_cpp_faceprints_dble_dble(const rsid_faceprints_t* c_faceprints, RealSenseID::Faceprints& faceprints)
{
    faceprints.data.version = c_faceprints->version;
    faceprints.data.featuresType = static_cast<RealSenseID::FaceprintsTypeEnum>(c_faceprints->featuresType);
    faceprints.data.flags = c_faceprints->flags;

    static_assert(sizeof(c_faceprints->adaptiveDescriptorWithoutMask) == sizeof(faceprints.data.adaptiveDescriptorWithoutMask),
                  "adaptive faceprints sizes (without mask) does not match");
    ::memcpy(faceprints.data.adaptiveDescriptorWithoutMask, c_faceprints->adaptiveDescriptorWithoutMask,
             sizeof(c_faceprints->adaptiveDescriptorWithoutMask));

    static_assert(sizeof(c_faceprints->enrollmentDescriptor) == sizeof(faceprints.data.enrollmentDescriptor),
                  "enrollment faceprints sizes does not match");
    ::memcpy(faceprints.data.enrollmentDescriptor, c_faceprints->enrollmentDescriptor, sizeof(c_faceprints->enrollmentDescriptor));
}

class AuthFaceprintsExtClbk : public RealSenseID::AuthFaceprintsExtractionCallback
{
    rsid_faceprints_ext_args _faceprints_ext_args;

public:
    explicit AuthFaceprintsExtClbk(rsid_faceprints_ext_args args) : _faceprints_ext_args {args}
    {
    }

    void OnResult(const RealSenseID::AuthenticateStatus status, const ExtractedFaceprints* faceprints) override
    {
        if (_faceprints_ext_args.result_clbk)
        {
            rsid_extracted_faceprints_t c_faceprints;

            if (status == RealSenseID::AuthenticateStatus::Success)
            {
                copy_to_c_faceprints_ple_ple(*faceprints, &c_faceprints);
                _faceprints_ext_args.result_clbk(static_cast<rsid_auth_status>(status), &c_faceprints, _faceprints_ext_args.ctx);
            }
            else
                _faceprints_ext_args.result_clbk(static_cast<rsid_auth_status>(status), nullptr, _faceprints_ext_args.ctx);
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        if (_faceprints_ext_args.hint_clbk)
            _faceprints_ext_args.hint_clbk(static_cast<rsid_auth_status>(hint), 0.0, _faceprints_ext_args.ctx);
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        handle_face_detected_clbk(_faceprints_ext_args.face_detected_clbk, faces, ts, _faceprints_ext_args.ctx);
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        handle_landmarks_detected_clbk(_faceprints_ext_args.landmarks_detected_clbk, landmarks, ts, _faceprints_ext_args.ctx);
    }

    void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts) override
    {
        handle_face_distances_clbk(_faceprints_ext_args.face_distances_clbk, distances, ts, _faceprints_ext_args.ctx);
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_faceprints_ext_args.face_cropped_image_clbk)
            _faceprints_ext_args.face_cropped_image_clbk(buffer, width, height, ts, _faceprints_ext_args.ctx);
    }
};

class AuthLoopFaceprintsExtClbk : public RealSenseID::AuthFaceprintsExtractionCallback
{
    rsid_faceprints_ext_args _faceprints_ext_args;

public:
    explicit AuthLoopFaceprintsExtClbk(rsid_faceprints_ext_args args) : _faceprints_ext_args {args}
    {
    }

    void OnResult(const RealSenseID::AuthenticateStatus status, const ExtractedFaceprints* faceprints) override
    {
        if (_faceprints_ext_args.result_clbk)
        {
            rsid_extracted_faceprints_t c_faceprints;

            if (status == RealSenseID::AuthenticateStatus::Success)
            {
                copy_to_c_faceprints_ple_ple(*faceprints, &c_faceprints);
                _faceprints_ext_args.result_clbk(static_cast<rsid_auth_status>(status), &c_faceprints, _faceprints_ext_args.ctx);
            }
            else
                _faceprints_ext_args.result_clbk(static_cast<rsid_auth_status>(status), nullptr, _faceprints_ext_args.ctx);
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint, float frameScore) override
    {
        if (_faceprints_ext_args.hint_clbk)
            _faceprints_ext_args.hint_clbk(static_cast<rsid_auth_status>(hint), 0, _faceprints_ext_args.ctx);
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        handle_face_detected_clbk(_faceprints_ext_args.face_detected_clbk, faces, ts, _faceprints_ext_args.ctx);
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        handle_landmarks_detected_clbk(_faceprints_ext_args.landmarks_detected_clbk, landmarks, ts, _faceprints_ext_args.ctx);
    }

    void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts) override
    {
        handle_face_distances_clbk(_faceprints_ext_args.face_distances_clbk, distances, ts, _faceprints_ext_args.ctx);
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_faceprints_ext_args.face_cropped_image_clbk)
            _faceprints_ext_args.face_cropped_image_clbk(buffer, width, height, ts, _faceprints_ext_args.ctx);
    }
};

class EnrollFaceprintsExtClbk : public RealSenseID::EnrollFaceprintsExtractionCallback
{
    rsid_enroll_ext_args _enroll_ext_args;

public:
    explicit EnrollFaceprintsExtClbk(rsid_enroll_ext_args args) : _enroll_ext_args {args}
    {
    }

    void OnResult(const RealSenseID::EnrollStatus status, const ExtractedFaceprints* faceprints) override
    {
        if (_enroll_ext_args.status_clbk)
        {
            rsid_faceprints_t c_faceprints;

            if (status == RealSenseID::EnrollStatus::Success)
            {
                // copy_to_c_faceprints_ple_ple(*faceprints, &c_faceprints);
                copy_to_c_faceprints_ple_dble_for_enroll(*faceprints, &c_faceprints);
                _enroll_ext_args.status_clbk(static_cast<rsid_enroll_status>(status), &c_faceprints, _enroll_ext_args.ctx);
            }
            else
                _enroll_ext_args.status_clbk(static_cast<rsid_enroll_status>(status), nullptr, _enroll_ext_args.ctx);
        }
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        if (_enroll_ext_args.progress_clbk)
            _enroll_ext_args.progress_clbk(static_cast<rsid_face_pose>(pose), _enroll_ext_args.ctx);
    }

    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override
    {
        if (_enroll_ext_args.hint_clbk)
            _enroll_ext_args.hint_clbk(static_cast<rsid_enroll_status>(hint), frameScore, _enroll_ext_args.ctx);
    }

    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int ts) override
    {
        handle_face_detected_clbk(_enroll_ext_args.face_detected_clbk, faces, ts, _enroll_ext_args.ctx);
    }

    void OnLandmarksDetected(const std::vector<RealSenseID::FaceLandmarks>& landmarks, const unsigned int ts) override
    {
        handle_landmarks_detected_clbk(_enroll_ext_args.landmarks_detected_clbk, landmarks, ts, _enroll_ext_args.ctx);
    }

    void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height,
                            const unsigned int ts) override
    {
        if (_enroll_ext_args.face_cropped_image_clbk)
            _enroll_ext_args.face_cropped_image_clbk(buffer, width, height, ts, _enroll_ext_args.ctx);
    }
};

// Signature callbacks - called by the lib to sign coming messages to the device,
// and to verify incoming messages from the device.
class WrapperSignatureClbk : public RealSenseID::SignatureCallback
{
    rsid_signature_clbk _user_clbk;

public:
    explicit WrapperSignatureClbk(rsid_signature_clbk* clbk) : _user_clbk {*clbk}
    {
    }

    ~WrapperSignatureClbk() override = default;

    // Called by the lib to get a signature for the given buffer.
    // The generated signature should be copied to the given *outSig (already pre-allocated, 32 bytes, output buffer).
    // Return value: true on success, false otherwise.
    bool Sign(const unsigned char* buffer, const unsigned int length, unsigned char* outSig) override
    {
        if (_user_clbk.sign_clbk)
            return _user_clbk.sign_clbk(buffer, length, outSig, _user_clbk.ctx) != 0;
        return false;
    }

    // Called by the lib to verify the buffer and the given signature.
    // Return value: true on verify success, false otherwise.
    bool Verify(const unsigned char* buffer, const unsigned int bufferLen, const unsigned char* sig, const unsigned int sigLen) override
    {
        if (_user_clbk.verify_clbk)
            return _user_clbk.verify_clbk(buffer, bufferLen, sig, sigLen, _user_clbk.ctx) != 0;
        return false;
    }
};

#ifdef RSID_SECURE

// Context class
// Will be stored as opaque pointer in the rsid_face_authenticator struct.
class SecureAuthContext
{
    std::unique_ptr<WrapperSignatureClbk> _signature_callback;
    std::unique_ptr<RealSenseID::FaceAuthenticator> _authenticator;

public:
    SecureAuthContext(rsid_signature_clbk* clbk, RealSenseID::DeviceType deviceType) :
        _signature_callback {std::make_unique<WrapperSignatureClbk>(clbk)},
        _authenticator {std::make_unique<RealSenseID::FaceAuthenticator>(_signature_callback.get(), deviceType)}
    {
    }

    WrapperSignatureClbk* sign_clbk()
    {
        return _signature_callback.get();
    }

    RealSenseID::FaceAuthenticator* authenticator()
    {
        return _authenticator.get();
    }
};
using auth_context_t = SecureAuthContext;
#else
// Context class
// Will be stored as opaque pointer in the rsid_face_authenticator struct.
class AuthContext
{
    std::unique_ptr<RealSenseID::FaceAuthenticator> _authenticator;

public:
    explicit AuthContext(RealSenseID::DeviceType deviceType) : _authenticator {std::make_unique<RealSenseID::FaceAuthenticator>(deviceType)}
    {
    }

    RealSenseID::FaceAuthenticator* authenticator()
    {
        return _authenticator.get();
    }
};
using auth_context_t = AuthContext;

#endif // RSID_SECURE


RealSenseID::SerialConfig from_c_struct(const rsid_serial_config* serial_config)
{
    RealSenseID::SerialConfig config;
    config.port = serial_config->port;
    return config;
}

RealSenseID::DeviceConfig device_config_from_c_struct(const rsid_device_config* device_config)
{
    RealSenseID::DeviceConfig config;
    config.camera_rotation = static_cast<RealSenseID::DeviceConfig::CameraRotation>(device_config->camera_rotation);
    config.security_level = static_cast<RealSenseID::DeviceConfig::SecurityLevel>(device_config->security_level);
    config.algo_flow = static_cast<RealSenseID::DeviceConfig::AlgoFlow>(device_config->algo_mode);
    config.face_selection_policy = static_cast<RealSenseID::DeviceConfig::FaceSelectionPolicy>(device_config->face_selection_policy);
    config.dump_mode = static_cast<RealSenseID::DeviceConfig::DumpMode>(device_config->dump_mode);
    config.max_spoofs = device_config->max_spoofs;
    config.match_thresh = device_config->match_thresh;
    config.rect_enable = device_config->rect_enable;
    config.landmarks_enable = device_config->landmarks_enable;
    config.gpio_auth_toggling = device_config->gpio_auth_toggling;
    config.frontal_face_policy = static_cast<RealSenseID::DeviceConfig::FrontalFacePolicy>(device_config->frontal_face_policy);
    config.person_motion_mode = static_cast<RealSenseID::DeviceConfig::PersonMotionMode>(device_config->person_motion_mode);
    unsigned char n = device_config->num_rois;
    if (n == 0 || n > RealSenseID::DeviceConfig::MAX_ROIS)
        n = 1;
    config.num_rois = n;
    for (unsigned char ri = 0; ri < n; ri++)
    {
        config.detection_rois[ri].x = device_config->rois[ri].x;
        config.detection_rois[ri].y = device_config->rois[ri].y;
        config.detection_rois[ri].width = device_config->rois[ri].width;
        config.detection_rois[ri].height = device_config->rois[ri].height;
    }
    config.manual_exposure_time_us = device_config->manual_exposure_time_us;
    config.manual_gain = device_config->manual_gain;
    config.manual_gain = device_config->manual_gain;
    config.distance_limit_cm = device_config->distance_limit_cm;
    config.distance_enabled = device_config->distance_enabled ? true : false;
    return config;
}


RealSenseID::FaceAuthenticator* get_auth_impl(rsid_authenticator* authenticator)
{
    auto* auth_ctx = static_cast<auth_context_t*>(authenticator->_impl);
    return auth_ctx->authenticator();
}
} // namespace

#ifdef RSID_SECURE
static rsid_authenticator* create_authenticator_impl(rsid_signature_clbk* signature_clbk, rsid_device_type device_type)
{
    auth_context_t* auth_ctx = nullptr;
    try
    {
        auth_ctx = new auth_context_t(signature_clbk, static_cast<RealSenseID::DeviceType>(device_type));
        auto* rv = new rsid_authenticator();
        rv->_impl = static_cast<void*>(auth_ctx);
        return rv;
    }
    catch (...)
    {
        if (auth_ctx)
        {
            try
            {
                delete (auth_ctx);
            }
            catch (...)
            {
            }
        }
        return nullptr;
    }
}


RSID_C_API rsid_authenticator* rsid_create_authenticator_F45x(rsid_signature_clbk* signature_clbk)
{
    return create_authenticator_impl(signature_clbk, RSID_DeviceType_F45x);
}

RSID_C_API rsid_authenticator* rsid_create_authenticator_F50x(rsid_signature_clbk* signature_clbk)
{
    return create_authenticator_impl(signature_clbk, RSID_DeviceType_F50x);
}

RSID_C_API rsid_authenticator* rsid_create_authenticator(rsid_signature_clbk* signature_clbk)
{
    return create_authenticator_impl(signature_clbk, RSID_DeviceType_F45x);
}
#else
static rsid_authenticator* create_authenticator_impl(rsid_device_type device_type)
{
    auth_context_t* auth_ctx = nullptr;
    try
    {
        auth_ctx = new auth_context_t(static_cast<RealSenseID::DeviceType>(device_type));
        auto* rv = new rsid_authenticator();
        rv->_impl = static_cast<void*>(auth_ctx);
        return rv;
    }
    catch (...)
    {
        if (auth_ctx)
        {
            try
            {
                delete (auth_ctx);
            }
            catch (...)
            {
            }
        }
        return nullptr;
    }
}


RSID_C_API rsid_authenticator* rsid_create_authenticator()
{
    return create_authenticator_impl(RSID_DeviceType_F45x);
}

RSID_C_API rsid_authenticator* rsid_create_authenticator_F45x()
{
    return create_authenticator_impl(RSID_DeviceType_F45x);
}

RSID_C_API rsid_authenticator* rsid_create_authenticator_F50x()
{
    return create_authenticator_impl(RSID_DeviceType_F50x);
}


#endif // RSID_SECURE

rsid_status rsid_set_device_config(rsid_authenticator* authenticator, const rsid_device_config* device_config)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto config = device_config_from_c_struct(device_config);
    auto status = auth_impl->SetDeviceConfig(config);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_query_device_config(rsid_authenticator* authenticator, rsid_device_config* device_config)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto config = device_config_from_c_struct(device_config);
    auto status = auth_impl->QueryDeviceConfig(config);

    device_config->camera_rotation = static_cast<rsid_camera_rotation_type>(config.camera_rotation);
    device_config->security_level = static_cast<rsid_security_level_type>(config.security_level);
    device_config->algo_mode = static_cast<rsid_algo_mode_type>(config.algo_flow);
    device_config->face_selection_policy = static_cast<rsid_face_selection_policy>(config.face_selection_policy);
    device_config->dump_mode = static_cast<rsid_dump_mode>(config.dump_mode);
    device_config->max_spoofs = config.max_spoofs;
    device_config->match_thresh = config.match_thresh;
    device_config->rect_enable = config.rect_enable;
    device_config->landmarks_enable = config.landmarks_enable;
    device_config->gpio_auth_toggling = config.gpio_auth_toggling;
    device_config->frontal_face_policy = static_cast<rsid_frontal_face_policy_type>(config.frontal_face_policy);
    device_config->person_motion_mode = static_cast<rsid_person_motion_mode_type>(config.person_motion_mode);
    device_config->num_rois = config.num_rois;
    for (unsigned char ri = 0; ri < config.num_rois && ri < RealSenseID::DeviceConfig::MAX_ROIS; ri++)
    {
        device_config->rois[ri].x = config.detection_rois[ri].x;
        device_config->rois[ri].y = config.detection_rois[ri].y;
        device_config->rois[ri].width = config.detection_rois[ri].width;
        device_config->rois[ri].height = config.detection_rois[ri].height;
    }
    device_config->manual_exposure_time_us = config.manual_exposure_time_us;
    device_config->manual_gain = config.manual_gain;
    device_config->manual_gain = config.manual_gain;
    device_config->distance_limit_cm = config.distance_limit_cm;
    device_config->distance_enabled = config.distance_enabled ? 1 : 0;
    return static_cast<rsid_status>(status);
}

void rsid_destroy_authenticator(rsid_authenticator* authenticator)
{
    if (authenticator == nullptr)
    {
        return;
    }

    try
    {
        auto* auth_ctx = static_cast<auth_context_t*>(authenticator->_impl);
        delete (auth_ctx);
    }
    catch (...)
    {
    }
    delete authenticator;
}

rsid_status rsid_connect(rsid_authenticator* authenticator, const rsid_serial_config* serial_config)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto config = from_c_struct(serial_config);
    auto status = auth_impl->Connect(config);
    return static_cast<rsid_status>(status);
}

void rsid_disconnect(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auth_impl->Disconnect();
}

#ifdef RSID_SECURE
rsid_status rsid_pair(rsid_authenticator* authenticator, rsid_pairing_args* pairing_args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto status = auth_impl->Pair(pairing_args->host_pubkey, pairing_args->host_pubkey_sig, pairing_args->device_pubkey_result);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_unpair(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto status = auth_impl->Unpair();
    return static_cast<rsid_status>(status);
}
#endif // RSID_SECURE

rsid_status rsid_enroll(rsid_authenticator* authenticator, const rsid_enroll_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    EnrollClbk enrollCallback(*args);
    auto status = auth_impl->Enroll(enrollCallback, args->user_id);
    return static_cast<rsid_status>(status);
}

rsid_enroll_status rsid_extract_faceprints_from_image(rsid_authenticator* authenticator, const char* user_id, const unsigned char* buffer,
                                                      unsigned width, unsigned height, rsid_faceprints_t* c_faceprints)
{
    auto* auth_impl = get_auth_impl(authenticator);
    ExtractedFaceprints extractedFaceprints;
    RealSenseID::EnrollStatus status = auth_impl->EnrollImageFeatureExtraction(user_id, buffer, width, height, &extractedFaceprints);

    if (RealSenseID::EnrollStatus::Success == status)
    {
        copy_to_c_faceprints_ple_dble_for_enroll(extractedFaceprints, c_faceprints);
    }

    return static_cast<rsid_enroll_status>(status);
}

rsid_enroll_status rsid_enroll_image(rsid_authenticator* authenticator, const char* user_id, const unsigned char* buffer, unsigned width,
                                     unsigned height)
{
    auto* auth_impl = get_auth_impl(authenticator);
    RealSenseID::EnrollStatus status;
    status = auth_impl->EnrollImage(user_id, buffer, width, height);

    return static_cast<rsid_enroll_status>(status);
}


rsid_enroll_status rsid_enroll_image_one_to_one(rsid_authenticator* authenticator, const char* user_id, const unsigned char* buffer,
                                                unsigned width, unsigned height)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto status = auth_impl->EnrollImageOneToOne(user_id, buffer, width, height);
    return static_cast<rsid_enroll_status>(status);
}

rsid_status rsid_authenticate_one_to_one(rsid_authenticator* authenticator, const rsid_auth_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    AuthClbk authCallback(*args);
    auto status = auth_impl->AuthenticateOneToOne(authCallback);
    return static_cast<rsid_status>(status);
}

rsid_auth_status rsid_authenticate_image_one_to_one(rsid_authenticator* authenticator, const unsigned char* buffer, unsigned width,
                                                    unsigned height, char* user_id, short* score)
{
    auto* auth_impl = get_auth_impl(authenticator);
    std::string userID;
    auto status = auth_impl->AuthenticateImageOneToOne(buffer, width, height, userID, *score);
    if (userID.length() >= RealSenseID::MAX_USERID_LENGTH)
        return rsid_auth_status::RSID_Auth_Failure;

    ::strncpy(user_id, userID.c_str(), userID.length());
    return static_cast<rsid_auth_status>(status);
}

RSID_C_API rsid_status rsid_detect_face(rsid_authenticator* authenticator, const unsigned char* buffer, unsigned width, unsigned height,
                                        rsid_face_rect* face_rect, int expand_roi)
{
    if (buffer == nullptr || face_rect == nullptr)
    {
        return static_cast<rsid_status>(RealSenseID::Status::Error);
    }

    auto* auth_impl = get_auth_impl(authenticator);
    RealSenseID::FaceRect result;
    auto status = auth_impl->DetectFace(buffer, width, height, result, expand_roi != 0);
    face_rect->x = static_cast<uint32_t>(result.x);
    face_rect->y = static_cast<uint32_t>(result.y);
    face_rect->w = static_cast<uint32_t>(result.w);
    face_rect->h = static_cast<uint32_t>(result.h);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_authenticate(rsid_authenticator* authenticator, const rsid_auth_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    AuthClbk authCallback(*args);
    auto status = auth_impl->Authenticate(authCallback);
    return static_cast<rsid_status>(status);
}

static RealSenseID::Faceprints convert_to_cpp_faceprints_dble(const rsid_faceprints_t* rsid_faceprints_instance)
{
    RealSenseID::Faceprints faceprints;
    faceprints.data.version = rsid_faceprints_instance->version;
    faceprints.data.featuresType = (RealSenseID::FaceprintsTypeEnum)rsid_faceprints_instance->featuresType;

    static_assert(sizeof(faceprints.data.adaptiveDescriptorWithoutMask) == sizeof(rsid_faceprints_instance->adaptiveDescriptorWithoutMask),
                  "updated faceprints (without mask) sizes does not match");
    ::memcpy(&faceprints.data.adaptiveDescriptorWithoutMask[0], &rsid_faceprints_instance->adaptiveDescriptorWithoutMask[0],
             sizeof(faceprints.data.adaptiveDescriptorWithoutMask));

    static_assert(sizeof(faceprints.data.enrollmentDescriptor) == sizeof(rsid_faceprints_instance->enrollmentDescriptor),
                  "enrollment faceprints sizes does not match");
    ::memcpy(&faceprints.data.enrollmentDescriptor[0], &rsid_faceprints_instance->enrollmentDescriptor[0],
             sizeof(faceprints.data.enrollmentDescriptor));

    return faceprints;
}

static RealSenseID::MatchElement convert_to_cpp_faceprints_match_element(rsid_faceprints_match_element_t* rsid_faceprints_instance)
{
    RealSenseID::MatchElement faceprints;
    faceprints.data.version = rsid_faceprints_instance->version;
    faceprints.data.featuresType = rsid_faceprints_instance->featuresType;

    static_assert(sizeof(faceprints.data.featuresVector) == sizeof(rsid_faceprints_instance->featuresVector),
                  "matched faceprints sizes does not match");
    ::memcpy(&faceprints.data.featuresVector[0], &rsid_faceprints_instance->featuresVector[0], sizeof(faceprints.data.featuresVector));

    faceprints.data.flags = FaOperationFlagsEnum::OpFlagAuthWithoutMask;

    return faceprints;
}

rsid_status rsid_extract_faceprints_for_enroll(rsid_authenticator* authenticator, rsid_enroll_ext_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    EnrollFaceprintsExtClbk enroll_callback(*args);
    auto status = auth_impl->ExtractFaceprintsForEnroll(enroll_callback);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_extract_faceprints_for_auth(rsid_authenticator* authenticator, rsid_faceprints_ext_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    AuthFaceprintsExtClbk auth_callback(*args);
    auto status = auth_impl->ExtractFaceprintsForAuth(auth_callback);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_extract_faceprints_for_auth_loop(rsid_authenticator* authenticator, rsid_faceprints_ext_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    AuthLoopFaceprintsExtClbk auth_callback(*args);
    auto status = auth_impl->ExtractFaceprintsForAuthLoop(auth_callback);
    return static_cast<rsid_status>(status);
}

rsid_match_result rsid_match_faceprints(rsid_authenticator* authenticator, rsid_match_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);

    MatchElement new_faceprints = convert_to_cpp_faceprints_match_element(&args->new_faceprints);
    Faceprints existing_faceprints = convert_to_cpp_faceprints_dble(&args->existing_faceprints);
    Faceprints updated_faceprints = convert_to_cpp_faceprints_dble(&args->updated_faceprints);

    auto result = auth_impl->MatchFaceprints(new_faceprints, existing_faceprints, updated_faceprints);

    rsid_match_result match_result;
    match_result.should_update = result.should_update;
    match_result.success = result.success;
    match_result.score = (int)result.score;

    // save the updated vector to your DB here.
    if (result.success && result.should_update)
    {
        // write the updated adaptive faceprints on the args->updated_faceprints so Authenticator.cs
        // will have the updated vector!

        rsid_faceprints_t* rsid_updated_faceprints = &args->updated_faceprints;

        // update withoutMask[] vector
        static_assert(sizeof(rsid_updated_faceprints->adaptiveDescriptorWithoutMask) ==
                          sizeof(updated_faceprints.data.adaptiveDescriptorWithoutMask),
                      "adaptive faceprints (without mask) sizes does not match");
        ::memcpy(&rsid_updated_faceprints->adaptiveDescriptorWithoutMask[0], &updated_faceprints.data.adaptiveDescriptorWithoutMask[0],
                 sizeof(updated_faceprints.data.adaptiveDescriptorWithoutMask));

        // Does the DB entry of the user is RGB type ?
        // if yes - update also the enrollement vector.
        bool isEnrolledTypeInDbIsRgb = (RealSenseID::FaceprintsTypeEnum::RGB == existing_faceprints.data.featuresType);

        if (isEnrolledTypeInDbIsRgb)
        {
            // LOG_DEBUG(LOG_TAG, "---> Updating RGB image-based enrollment vector in the DB...");
            static_assert(sizeof(rsid_updated_faceprints->enrollmentDescriptor) == sizeof(updated_faceprints.data.enrollmentDescriptor),
                          "enrollment faceprints sizes does not match");
            ::memcpy(&rsid_updated_faceprints->enrollmentDescriptor[0], &updated_faceprints.data.enrollmentDescriptor[0],
                     sizeof(updated_faceprints.data.enrollmentDescriptor));

            rsid_updated_faceprints->featuresType = updated_faceprints.data.featuresType;
        }
    }

    return match_result;
}

rsid_status rsid_authenticate_loop(rsid_authenticator* authenticator, const rsid_auth_args* args)
{
    auto* auth_impl = get_auth_impl(authenticator);
    AuthClbk authCallback(*args);
    auto status = auth_impl->AuthenticateLoop(authCallback);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_detect_persons(rsid_authenticator* authenticator, rsid_person_detection_clbk callback, int loop, void* ctx)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto person_callback = [callback, ctx](const std::vector<RealSenseID::PersonRect>& persons, unsigned int ts,
                                           RealSenseID::AuthenticateStatus status) -> bool {
        if (callback == nullptr)
            return false;

        rsid_person_rect c_persons[RSID_MAX_PERSONS];
        auto n_persons = to_c_persons(persons, c_persons, RSID_MAX_PERSONS);
        int result = callback(c_persons, n_persons, ts, static_cast<rsid_auth_status>(status), ctx);
        return result != 0;
    };
    auto status = auth_impl->DetectPersons(person_callback, loop != 0);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_detect_poses(rsid_authenticator* authenticator, rsid_pose_detection_clbk callback, int loop, void* ctx)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto pose_callback = [callback, ctx](const std::vector<RealSenseID::PersonPose>& poses, unsigned int ts,
                                         RealSenseID::AuthenticateStatus status) -> bool {
        if (callback == nullptr)
            return false;

        rsid_person_pose c_poses[RSID_MAX_PERSONS];
        auto n_poses = to_c_poses(poses, c_poses, RSID_MAX_PERSONS);
        int result = callback(c_poses, n_poses, ts, static_cast<rsid_auth_status>(status), ctx);
        return result != 0;
    };
    auto status = auth_impl->DetectPoses(pose_callback, loop != 0);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_decode_barcodes(rsid_authenticator* authenticator, rsid_barcode_detection_clbk callback, int loop, void* ctx)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto barcode_callback = [callback, ctx](const std::vector<std::string>& barcodes, unsigned int ts,
                                            RealSenseID::AuthenticateStatus status) -> bool {
        if (callback == nullptr)
            return false;

        const char* c_barcodes[RSID_MAX_BARCODES];
        size_t n_barcodes = std::min(static_cast<int>(barcodes.size()), RSID_MAX_BARCODES);
        for (size_t i = 0; i < n_barcodes; i++)
        {
            c_barcodes[i] = barcodes[i].c_str();
        }
        int result = callback(c_barcodes, n_barcodes, ts, static_cast<rsid_auth_status>(status), ctx);
        return result != 0;
    };
    auto status = auth_impl->DecodeBarcodes(barcode_callback, loop != 0);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_detect_body_parts(rsid_authenticator* authenticator, rsid_body_part_detection_clbk callback, int loop, void* ctx)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto body_part_callback = [callback, ctx](const std::vector<RealSenseID::PersonRect>& body_parts, unsigned int ts,
                                              RealSenseID::AuthenticateStatus status) -> bool {
        if (callback == nullptr)
            return false;

        rsid_person_rect c_body_parts[RSID_MAX_PERSONS];
        auto n_body_parts = to_c_persons(body_parts, c_body_parts, RSID_MAX_PERSONS);
        int result = callback(c_body_parts, n_body_parts, ts, static_cast<rsid_auth_status>(status), ctx);
        return result != 0;
    };
    auto status = auth_impl->DetectBodyParts(body_part_callback, loop != 0);
    return static_cast<rsid_status>(status);
}

rsid_status rsid_cancel(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    auto status = auth_impl->Cancel();
    return static_cast<rsid_status>(status);
}

rsid_status rsid_remove_user(rsid_authenticator* authenticator, const char* user_id)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->RemoveUser(user_id));
}

rsid_status rsid_remove_all_users(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->RemoveAll());
}

// c strings
const char* rsid_status_str(rsid_status status)
{
    return RealSenseID::Description(static_cast<RealSenseID::Status>(status));
}

const char* rsid_auth_status_str(rsid_auth_status status)
{
    return RealSenseID::Description(static_cast<RealSenseID::AuthenticateStatus>(status));
}

const char* rsid_enroll_status_str(rsid_enroll_status status)
{
    return RealSenseID::Description(static_cast<RealSenseID::EnrollStatus>(status));
}

const char* rsid_face_pose_str(rsid_face_pose pose)
{
    return RealSenseID::Description(static_cast<RealSenseID::FacePose>(pose));
}

const char* rsid_auth_settings_rotation(rsid_camera_rotation_type rotation)
{
    return RealSenseID::Description(static_cast<RealSenseID::DeviceConfig::CameraRotation>(rotation));
}

const char* rsid_auth_settings_level(rsid_security_level_type level)
{
    return RealSenseID::Description(static_cast<RealSenseID::DeviceConfig::SecurityLevel>(level));
}

const char* rsid_auth_settings_algo_mode(rsid_algo_mode_type mode)
{
    return RealSenseID::Description(static_cast<RealSenseID::DeviceConfig::AlgoFlow>(mode));
}

const char* rsid_auth_settings_mode(rsid_algo_mode_type mode)
{
    return RealSenseID::Description(static_cast<RealSenseID::DeviceConfig::AlgoFlow>(mode));
}
const char* rsid_version()
{
    return RealSenseID::Version();
}

const char* rsid_compatible_firmware_version(const rsid_device_type device)
{
    return RealSenseID::CompatibleFirmwareVersion(static_cast<RealSenseID::DeviceType>(device));
}

int rsid_is_fw_compatible_with_host(const rsid_device_type device, const char* fw_version)
{
    if (fw_version == nullptr)
        return 0;
    return RealSenseID::IsFwCompatibleWithHost(static_cast<RealSenseID::DeviceType>(device), fw_version);
}

void rsid_set_log_clbk(rsid_log_clbk clbk, rsid_log_level min_level, int do_formatting)
{
    auto log_clbk = [clbk](RealSenseID::LogLevel level, const char* msg) {
        if (clbk)
            clbk(static_cast<rsid_log_level>(level), msg);
    };

    auto required_level = static_cast<RealSenseID::LogLevel>(min_level);
    auto required_formatting = static_cast<bool>(do_formatting);
    RealSenseID::SetLogCallback(log_clbk, required_level, required_formatting);
}

rsid_status rsid_query_user_ids(rsid_authenticator* authenticator, char** user_ids, unsigned int* number_of_users)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->QueryUserIds(user_ids, *number_of_users));
}

// concat user ids to single buffer for easier usage from managed languages
// result buf size must be number_of_users * 31
rsid_status rsid_query_user_ids_to_buf(rsid_authenticator* authenticator, char* result_buf, unsigned int* number_of_users)
{
    auto* auth_impl = get_auth_impl(authenticator);

    // allocate needed array of user ids
    char** user_ids = new char*[*number_of_users];
    for (unsigned i = 0; i < *number_of_users; i++)
    {
        user_ids[i] = new char[RealSenseID::MAX_USERID_LENGTH];
    }
    unsigned int nusers_in_out = *number_of_users;
    auto status = auth_impl->QueryUserIds(user_ids, nusers_in_out);
    if (status != RealSenseID::Status::Ok)
    {
        // free allocated memory and return on error
        for (unsigned int i = 0; i < *number_of_users; i++)
        {
            delete user_ids[i];
        }
        delete[] user_ids;
        *number_of_users = 0;
        return static_cast<rsid_status>(status);
    }

    for (unsigned int i = 0; i < (std::min)(nusers_in_out, *number_of_users); i++)
    {
        ::memcpy((char*)&result_buf[i * RealSenseID::MAX_USERID_LENGTH], user_ids[i], RealSenseID::MAX_USERID_LENGTH);
    }

    // free allocated memory
    for (unsigned int i = 0; i < *number_of_users; i++)
    {
        delete user_ids[i];
    }
    delete[] user_ids;
    *number_of_users = nusers_in_out;
    return rsid_status::RSID_Ok;
}


rsid_status rsid_query_number_of_users(rsid_authenticator* authenticator, unsigned int* number_of_users)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->QueryNumberOfUsers(*number_of_users));
}

rsid_status rsid_get_users_faceprints(rsid_authenticator* authenticator, rsid_faceprints_t* user_features)
{
    std::vector<RealSenseID::Faceprints> user_descriptors(MAX_USERS);
    auto* auth_impl = get_auth_impl(authenticator);
    unsigned int num_of_users = 0;
    auto status = static_cast<rsid_status>(auth_impl->GetUsersFaceprints(user_descriptors.data(), num_of_users));
    for (unsigned int i = 0; i < num_of_users; i++)
    {
        copy_to_c_faceprints_dble_dble(user_descriptors[i], &user_features[i]);
    }
    return status;
}

rsid_status rsid_dump_and_mount(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->DumpAndMount());
}

rsid_status rsid_set_users_faceprints(rsid_authenticator* authenticator, rsid_user_faceprints_dble* user_features,
                                      const unsigned int number_of_users)
{
    auto* auth_impl = get_auth_impl(authenticator);
    std::vector<RealSenseID::UserFaceprints_t> user_descriptors(MAX_USERS);
    for (unsigned int i = 0; i < number_of_users; i++)
    {
        copy_to_cpp_faceprints_dble_dble(&user_features[i].faceprints, user_descriptors[i].faceprints);

        // user_descriptors[i].user_id = user_features[i].user_id;
        ::memcpy(&user_descriptors[i].user_id[0], user_features[i].user_id, sizeof(char) * RealSenseID::MAX_USERID_LENGTH);
    }
    auto status = static_cast<rsid_status>(auth_impl->SetUsersFaceprints(user_descriptors.data(), number_of_users));
    return status;
}


rsid_status rsid_standby(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->Standby());
}

rsid_status rsid_hibernate(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->Hibernate());
}

rsid_status rsid_unlock(rsid_authenticator* authenticator)
{
    auto* auth_impl = get_auth_impl(authenticator);
    return static_cast<rsid_status>(auth_impl->Unlock());
}

rsid_device_type rsid_discover_device_type(const char* serial_port)
{
    static_assert(static_cast<int>(RealSenseID::DeviceType::Unknown) == rsid_device_type::RSID_DeviceType_Unknown, "");
    static_assert(static_cast<int>(RealSenseID::DeviceType::F45x) == rsid_device_type::RSID_DeviceType_F45x, "");
    static_assert(static_cast<int>(RealSenseID::DeviceType::F50x) == rsid_device_type::RSID_DeviceType_F50x, "");
    return static_cast<rsid_device_type>(RealSenseID::DiscoverDeviceType(serial_port));
}

int rsid_discover_devices(rsid_device_info devices[], int array_size)
{
    if (devices == nullptr || array_size <= 0)
    {
        return -1;
    }

    auto items = RealSenseID::DiscoverDevices();
    if (items.empty())
    {
        return 0;
    }

    if (static_cast<int>(items.size()) > array_size)
    {
        return -1;
    }

    static_assert(sizeof(RealSenseID::DeviceInfo::serialPort) == sizeof(rsid_device_info::serial_port), "serial_port!=serialPort");
    static_assert(sizeof(RealSenseID::DeviceInfo::serialNumber) == sizeof(rsid_device_info::serial_number), "serial_number!=serialNumber");
    int count = 0;
    for (const auto& item : items)
    {
        auto& dev = devices[count++];

        ::memcpy(dev.serial_port, item.serialPort, sizeof(dev.serial_port) - 1);
        dev.serial_port[sizeof(dev.serial_port) - 1] = '\0';

        dev.device_type = static_cast<rsid_device_type>(item.deviceType);

        ::memcpy(dev.serial_number, item.serialNumber, sizeof(dev.serial_number) - 1);
        dev.serial_number[sizeof(dev.serial_number) - 1] = '\0';
        dev.camera_number = item.cameraNumber;
    }
    return count;
}

#ifdef _WIN32
#pragma warning(pop)
#endif
