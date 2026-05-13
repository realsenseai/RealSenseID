// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "rsid_export.h"
#ifdef __cplusplus
extern "C"
{
#endif //__cplusplus

    typedef enum
    {
        RSID_DeviceType_Unknown,
        RSID_DeviceType_F45x,
        RSID_DeviceType_F50x
    } rsid_device_type;


    typedef enum
    {
        RSID_Rotation_0_Deg = 0, // default
        RSID_Rotation_180_Deg = 1,
        RSID_Rotation_90_Deg = 2,
        RSID_Rotation_270_Deg = 3
    } rsid_camera_rotation_type;

    typedef enum
    {
        RSID_SecLevel_High = 0,   // high security, no mask support, all AS algo(s) will be activated.
        RSID_SecLevel_Medium = 1, // default mode to support masks, only main AS algo will be activated.
        RSID_SecLevel_Low = 2     // low device to run recognition only without AS.
    } rsid_security_level_type;

    typedef enum
    {
        RSID_AlgoMode_All = 0,            // default mode to run all algo(s)
        RSID_AlgoMode_SpoofOnly = 1,      // run Anti-Spoofing algo(s) only.
        RSID_AlgoMode_RecognitionOnly = 2 // configures device to run recognition only without AS.
    } rsid_algo_mode_type;


    typedef enum
    {
        RSID_FaceSelection_Single = 0, // default, run authentication on closest face
        RSID_FaceSelection_All = 1     // run authentication on all (up to 5) detected faces
    } rsid_face_selection_policy;

    typedef enum
    {
        RSID_FacePolicy_None = 0, // default
        RSID_FacePolicy_Moderate = 1,
        RSID_FacePolicy_Strict = 2
    } rsid_frontal_face_policy_type;

    typedef enum
    {
        RSID_PersonMotionMode_Static = 0, // default
        RSID_PersonMotionMode_Walkthrough = 1
    } rsid_person_motion_mode_type;

    typedef enum
    {
        MJPEG_1080P = 0,
        MJPEG_720P = 1,
        RAW10_1080P = 2,
    } rsid_preview_mode;

    typedef enum
    {
        RSID_DumpNone = 0,
        RSID_DumpCroppedFace = 1,
        RSID_DumpFullFrame = 2,
        RSID_DumpDubug = 3,
    } rsid_dump_mode;

    typedef enum
    {
        RSID_Ok = 100,
        RSID_Error,
        RSID_SerialError,
        RSID_SecurityError,
        RSID_VersionMismatch,
        RSID_CrcError,
        RSID_TooManySpoofs,
        RSID_NotSupported,
        RSID_DatabaseFull,
        RSID_DuplicateUserId,
        RSID_DuplicateFaceprints,
        RSID_InvalidSettings
    } rsid_status;

    typedef enum
    {
        RSID_Auth_Success,
        RSID_Auth_NoFaceDetected,
        RSID_Auth_FaceDetected,
        RSID_Auth_PersonNotFound,
        RSID_Auth_PersonFound,
        RSID_Auth_BarcodeNotFound,
        RSID_Auth_BarcodeFound,
        RSID_Auth_LedFlowSuccess,
        RSID_Auth_FaceIsTooFarToTheTop,
        RSID_Auth_FaceIsTooFarToTheBottom,
        RSID_Auth_FaceIsTooFarToTheRight,
        RSID_Auth_FaceIsTooFarToTheLeft,
        RSID_Auth_FaceTiltIsTooUp,
        RSID_Auth_FaceTiltIsTooDown,
        RSID_Auth_FaceTiltIsTooRight,
        RSID_Auth_FaceTiltIsTooLeft,
        RSID_Auth_FaceIsNotFrontal,
        RSID_Auth_CameraStarted,
        RSID_Auth_CameraStopped,
        RSID_Auth_Spoof,
        RSID_Auth_Forbidden,
        RSID_Auth_DeviceError,
        RSID_Auth_Failure,
        RSID_Auth_TooManySpoofs,
        RSID_Auth_InvalidFeatures,
        RSID_Auth_AmbiguousFace,
        RSID_Auth_Sunglasses = 50,
        RSID_Auth_MedicalMask,
        RSID_Auth_FaceTooFar = 61,
        RSID_Auth_CalcDistanceFailure = 62,
        RSID_Auth_FaceTooClose = 63,
        RSID_Auth_Serial_Ok = RSID_Ok,
        RSID_Auth_Serial_Error,
        RSID_Auth_Serial_SerialError,
        RSID_Auth_Serial_SecurityError,
        RSID_Auth_Serial_VersionMismatch,
        RSID_Auth_Serial_CrcError,
        RSID_Auth_Spoof_2D = 120,
        RSID_Auth_Spoof_3D,
        RSID_Auth_Spoof_LR,
        RSID_Auth_Spoof_Disparity,
        RSID_Auth_Spoof_Vision,
        RSID_Auth_Spoof_Surface,
        RSID_Auth_Spoof_Plane_Disparity,
        RSID_Auth_Spoof_2D_Right
    } rsid_auth_status;

    typedef enum
    {
        RSID_Enroll_Success,
        RSID_Enroll_NoFaceDetected,
        RSID_Enroll_FaceDetected,
        RSID_Enroll_PersonNotFound,
        RSID_Enroll_PersonFound,
        RSID_Enroll_BarcodeNotFound,
        RSID_Enroll_BarcodeFound,
        RSID_Enroll_LedFlowSuccess,
        RSID_Enroll_FaceIsTooFarToTheTop,
        RSID_Enroll_FaceIsTooFarToTheBottom,
        RSID_Enroll_FaceIsTooFarToTheRight,
        RSID_Enroll_FaceIsTooFarToTheLeft,
        RSID_Enroll_FaceTiltIsTooUp,
        RSID_Enroll_FaceTiltIsTooDown,
        RSID_Enroll_FaceTiltIsTooRight,
        RSID_Enroll_FaceTiltIsTooLeft,
        RSID_Enroll_FaceIsNotFrontal,
        RSID_Enroll_CameraStarted,
        RSID_Enroll_CameraStopped,
        RSID_Enroll_MultipleFacesDetected,
        RSID_Enroll_Failure,
        RSID_Enroll_DeviceError,
        RSID_Enroll_Spoof,
        RSID_Enroll_InvalidFeatures,
        RSID_Enroll_AmbiguousFace,
        RSID_Enroll_Sunglasses = 50,
        RSID_Enroll_MedicalMask,
        // Distance
        RSID_Enroll_FaceTooClose = 63,
        RSID_Enroll_Serial_Ok = RSID_Ok,
        RSID_Enroll_Serial_Error,
        RSID_Enroll_Serial_SerialError,
        RSID_Enroll_Serial_SecurityError,
        RSID_Enroll_Serial_VersionMismatch,
        RSID_Enroll_Serial_CrcError,
        RSID_Enroll_TooManySpoofs,
        RSID_Enroll_NotSupported,
        RSID_Enroll_DatabaseFull,
        RSID_Enroll_DuplicateUserId,
        RSID_Enroll_DuplicateFaceprints,
        RSID_Enroll_Spoof_2D = 120,
        RSID_Enroll_Spoof_3D,
        RSID_Enroll_Spoof_LR,
        RSID_Enroll_Spoof_Disparity,
        RSID_Enroll_Spoof_Vision,
        RSID_Enroll_Spoof_Surface,
        RSID_Enroll_Spoof_Plane_Disparity,
        RSID_Enroll_Spoof_2D_Right
    } rsid_enroll_status;


    typedef enum
    {
        RSID_Face_Center,
        RSID_Face_Up,
        RSID_Face_Down,
        RSID_Face_Left,
        RSID_Face_Right
    } rsid_face_pose;

    /* log callback support*/
    typedef enum
    {
        RSID_LogLevel_Trace,
        RSID_LogLevel_Debug,
        RSID_LogLevel_Info,
        RSID_LogLevel_Warning,
        RSID_LogLevel_Error,
        RSID_LogLevel_Critical,
        RSID_LogLevel_Off
    } rsid_log_level;

    typedef enum
    {
        RSID_Continuous,
        RSID_Opfw_First,
        RSID_Require_Intermediate_Fw,
        RSID_Not_Allowed
    } rsid_update_policy;

    typedef struct rsid_match_result
    {
        int success;
        int should_update;
        int score;
    } rsid_match_result;


    // c string representations of the statuses
    RSID_C_API const char* rsid_status_str(rsid_status status);
    RSID_C_API const char* rsid_auth_status_str(rsid_auth_status status);
    RSID_C_API const char* rsid_enroll_status_str(rsid_enroll_status status);
    RSID_C_API const char* rsid_face_pose_str(rsid_face_pose pose);
    RSID_C_API const char* rsid_auth_settings_rotation(rsid_camera_rotation_type rotation);
    RSID_C_API const char* rsid_auth_settings_level(rsid_security_level_type level);
    RSID_C_API const char* rsid_auth_settings_algo_mode(rsid_algo_mode_type mode);

#ifdef __cplusplus
}
#endif //__cplusplus