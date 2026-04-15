// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
/**
 * Statuses returned from FaceAuthenticator authenticate operation.
 */
enum class RSID_API AuthenticateStatus
{
    Success,
    NoFaceDetected,
    FaceDetected,
    PersonNotFound,
    PersonFound,
    BarcodeNotFound,
    BarcodeFound,
    LedFlowSuccess,
    FaceIsTooFarToTheTop,
    FaceIsTooFarToTheBottom,
    FaceIsTooFarToTheRight,
    FaceIsTooFarToTheLeft,
    FaceTiltIsTooUp,
    FaceTiltIsTooDown,
    FaceTiltIsTooRight,
    FaceTiltIsTooLeft,
    FaceIsNotFrontal,
    CameraStarted,
    CameraStopped,
    Spoof,
    Forbidden,
    DeviceError,
    Failure,
    TooManySpoofs,
    InvalidFeatures,
    AmbiguousFace,
    /// Accessories
    Sunglasses = 50,
    MedicalMask,
    /// Distance
    FaceTooFar = 61,
    CalcDistanceFailure = 62,
    FaceTooClose = 63,
    /// serial statuses
    Ok = 100,
    Error,
    SerialError,
    SecurityError,
    VersionMismatch,
    CrcError,
    /// Spoofs
    Spoof_2D = 120,
    Spoof_3D,
    Spoof_LR,
    Spoof_Disparity,
    Spoof_Vision,
    Spoof_Surface,
    Spoof_Plane_Disparity,
    Spoof_2D_Right,
    /* Note: Should not exceed 127 - to be a legal ascii*/
};

/**
 * Return c string description of the status
 *
 * @param status to describe.
 */
RSID_API const char* Description(AuthenticateStatus status);

template <typename OStream>
inline OStream& operator<<(OStream& os, const AuthenticateStatus& status)
{
    os << Description(status);
    return os;
}
} // namespace RealSenseID
