// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
struct RSID_API DeviceConfig
{
    /**
     * @enum CameraRotation
     * @brief Camera rotation.
     */
    enum class CameraRotation
    {
        Rotation_0_Deg = 0, // default
        Rotation_180_Deg = 1,
        Rotation_90_Deg = 2,
        Rotation_270_Deg = 3
    };

    /**
     * @enum SecurityLevel
     * @brief SecurityLevel to allow. (default is Low)
     */
    enum class SecurityLevel
    {
        High = 0,   // high security level
        Medium = 1, // medium security level
        Low = 2,    // low security level
    };

    /**
     * @enum AlgoFlow
     * @brief Algorithms which will be used during authentication
     */
    enum class AlgoFlow
    {
        All = 0,               // recognition, spoof and face detection
        FaceDetectionOnly = 1, // face detection only (default)
        SpoofOnly = 2,         // spoof only
        RecognitionOnly = 3,   // recognition only
    };

    /**
     * @enum FaceSelectionPolicy
     * @brief Controls whether to run authentication on all (up to 5) detected faces vs single (closest) face
     */
    enum class FaceSelectionPolicy
    {
        Single = 0, // default, run authentication on closest face
        All = 1     // run authentication on all (up to 5) detected faces
    };

    enum class DumpMode
    {
        None = 0,        // default
        CroppedFace = 1, // sends snapshot of the detected face (as jpg)
        FullFrame = 2,   // sends left+right raw frames with metadata
        Debug = 3        // F500 or newer only. Store diagnostic images in internal storage for later retrieval via DumpAndMount().
    };

    /**
     * @brief Defines three confidence levels used by the Matcher during authentication.
     *
     * Each confidence level corresponds to a different set of thresholds, providing the user with the flexibility to
     * choose between three different False Positive Rates (FPR): Low, Medium, and High. Currently, all sets use the
     * thresholds associated with the "Low" confidence level by default.
     */
    enum class MatcherConfidenceLevel
    {
        High = 0,
        Medium = 1,
        Low = 2 // default
    };

    /**
     * @brief Defines the policy for frontal face orientation.
     *
     * - None: No restriction on face angle (default).
     * - Moderate: Allow some deviation from a forward-facing orientation.
     * - Strict: The face should be directly oriented toward the camera.
     */
    enum class FrontalFacePolicy
    {
        None = 0, // default
        Moderate = 1,
        Strict = 2
    };

    /**
     * @enum PersonMotionMode
     * @brief Describes the expected movement behavior of a person within the camera frame.
     *
     * - Static: The person remains mostly stationary within the frame.
     * - Walkthrough: The person moves through the frame, such as walking past the camera.
     */
    enum class PersonMotionMode
    {
        Static = 0, // default
        Walkthrough = 1
    };

    /**
     * @struct Roi
     * @brief Describes the region of interest (ROI) within the camera frame.
     *
     * - x: The x-coordinate of the top-right corner of the ROI.
     * - y: The y-coordinate of the top-right corner of the ROI.
     * - width: The width of the ROI.
     * - height: The height of the ROI.
     */
    struct Roi
    {
        unsigned short x = 0;
        unsigned short y = 0;
        unsigned short width = 1080;
        unsigned short height = 1920;
    };

    /**
     * @enum DistanceLimit
     * @brief Controls the maximum distance for face authentication.
     */
    enum class DistanceLimit
    {
        NoLimit = 0, // default, no distance limit
        Short = 1,   // 70cm
        Mid = 2,     // 100cm
        Long = 3     // 130cm
    };

    CameraRotation camera_rotation = CameraRotation::Rotation_0_Deg;
    SecurityLevel security_level = SecurityLevel::Low;
    AlgoFlow algo_flow = AlgoFlow::FaceDetectionOnly;
    FaceSelectionPolicy face_selection_policy = FaceSelectionPolicy::Single;
    DumpMode dump_mode = DumpMode::None;
    MatcherConfidenceLevel matcher_confidence_level = MatcherConfidenceLevel::Low;
    FrontalFacePolicy frontal_face_policy = FrontalFacePolicy::None;
    PersonMotionMode person_motion_mode = PersonMotionMode::Static;
    DistanceLimit distance_limit = DistanceLimit::NoLimit;

    /**
     * @brief Specifies the maximum number of consecutive spoofing attempts allowed before the device rejects further
     * authentication requests.
     *
     * Setting this value to 0 disables the check, which is the default behavior. If the number of consecutive spoofing
     * attempts reaches max_spoofs, the device will reject any subsequent authentication requests. To reset this
     * behavior and allow further authentication attempts, the device must be unlocked using the Unlock() API call.
     */
    unsigned char max_spoofs = 0;

    /**
     * @brief Specifies the feature matching threshold.
     *
     * Setting this value to 0 will use device recommended threshold
     * */
    unsigned short match_thresh = 0;

    /**
     * @brief Controls whether GPIO toggling is enabled(1) or disabled(0, default) after successful authentication.
     *
     * Set this value to 1 to enable toggling of GPIO pin #1 after each successful authentication.
     * Set this value to 0 to disable GPIO toggling (default).
     *
     * @note Only GPIO pin #1 can be toggled. Other values are not supported.
     */
    int gpio_auth_toggling = 0;

    /**
     * @brief Manual exposure time in microseconds.
     *
     * This value allows for fine-tuning the camera's exposure settings.
     * Setting this value to 0 also means that the camera will use automatic exposure.
     */
    unsigned short manual_exposure_time_us = 0; // 0 means auto exposure

    /**
     * @brief Manual gain value.
     *
     * This value allows for fine-tuning the camera's gain settings.
     * Setting this value to 0 also means that the camera will use automatic gain.
     */
    unsigned short manual_gain = 0; // 0 means auto gain

    /**
     * @brief Specifies whether to enable face rectangle retrieval
     *
     * Setting this value to 0 disables receiving rectangle using the OnFaceDetected callback
     */
    unsigned char rect_enable = 0x01;

    /**
     * @brief Specifies whether to enable face landmarks retrieval
     *
     * Setting this value to 0 disables receiving landmarks using the OnLandmarksDetected callback
     */
    unsigned char landmarks_enable = 0;

    /**
     * @brief ROI parameters — up to MAX_ROIS regions; only detection_rois[0..num_rois-1] are active.
     * Multi-ROI (num_rois > 1) is only supported when security_level == Low.
     */
    static constexpr unsigned int MAX_ROIS = 5;
    Roi detection_rois[MAX_ROIS];
    unsigned char num_rois = 1;

    /**
     * @brief Enable/disable distance calculation/check
     */
    bool distance_enabled = false;
};

RSID_API const char* Description(DeviceConfig::CameraRotation rotation);
RSID_API const char* Description(DeviceConfig::SecurityLevel level);
RSID_API const char* Description(DeviceConfig::AlgoFlow algoMode);
RSID_API const char* Description(DeviceConfig::FaceSelectionPolicy policy);
RSID_API const char* Description(DeviceConfig::DumpMode dump_mode);
RSID_API const char* Description(DeviceConfig::MatcherConfidenceLevel matcher_confidence_level);
RSID_API const char* Description(DeviceConfig::FrontalFacePolicy policy);
RSID_API const char* Description(DeviceConfig::DistanceLimit limit);

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::CameraRotation& rotation)
{
    os << Description(rotation);
    return os;
}

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::AlgoFlow& mode)
{
    os << Description(mode);
    return os;
}

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::SecurityLevel& level)
{
    os << Description(level);
    return os;
}

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::FaceSelectionPolicy& policy)
{
    os << Description(policy);
    return os;
}

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::DumpMode& dump_mode)
{
    os << Description(dump_mode);
    return os;
}
template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::MatcherConfidenceLevel& matcher_confidence_level)
{
    os << Description(matcher_confidence_level);
    return os;
}
template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::FrontalFacePolicy& policy)
{
    os << Description(policy);
    return os;
}

template <typename OStream>
inline OStream& operator<<(OStream& os, const DeviceConfig::DistanceLimit& limit)
{
    os << Description(limit);
    return os;
}

} // namespace RealSenseID
