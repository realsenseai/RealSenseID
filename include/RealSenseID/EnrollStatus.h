// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "RealSenseIDExports.h"

namespace RealSenseID
{
/**
 * Statuses returned from FaceAuthenticator enroll operation.
 */
enum class RSID_API EnrollStatus
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
    MultipleFacesDetected,
    Failure,
    DeviceError,
    Spoof,
    InvalidFeatures,
    AmbiguousFace,
    /// Accessories - must start at 50!
    Sunglasses = 50,
    MedicalMask,
    // Distance
    FaceTooClose = 63,
    /// serial statuses
    Ok = 100,
    Error,
    SerialError,
    SecurityError,
    VersionMismatch,
    CrcError,
    TooManySpoofs,
    NotSupported,
    DatabaseFull,
    DuplicateUserId,
    DuplicateFaceprints,
    /// Spoofs
    Spoof_2D = 120,
    Spoof_3D,
    Spoof_LR,
    Spoof_Disparity,
    Spoof_Vision,
    Spoof_Surface,
    Spoof_Plane_Disparity,
    Spoof_2D_Right
};

/**
 * Return c string description of the status
 *
 * @param status to describe.
 */
RSID_API const char* Description(EnrollStatus status);

template <typename OStream>
inline OStream& operator<<(OStream& os, const EnrollStatus& status)
{
    os << Description(status);
    return os;
}
} // namespace RealSenseID
