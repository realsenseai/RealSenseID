/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

#ifndef RSID_H
#define RSID_H

/**
 * RealSenseID Embedded C SDK
 *
 * Pure C implementation of the RealSenseID host protocol for embedded MCU hosts.
 * No C++ runtime, no dynamic allocation, no OS dependencies.
 * User provides UART read/write via rsid_platform_t.
 *
 * Requirements: C99 compiler, little-endian platform.
 *
 * Usage:
 *   static rsid_ctx_t ctx;              (~9 KB - allocate statically)
 *   ctx.platform.send = my_send;       (set up UART callbacks)
 *   ctx.platform.recv = my_recv;
 *   ctx.platform.get_time_ms = my_ms;
 *   ctx.platform.sleep_ms = my_sleep;
 *   rsid_init(&ctx);
 *   rsid_authenticate(&ctx, &cb, NULL);
 */

#include <stdint.h>

/* SDK version — must match Host SDK (include/RealSenseID/Version.h) */
#define RSID_EMBEDDED_VER_MAJOR 3
#define RSID_EMBEDDED_VER_MINOR 3
#define RSID_EMBEDDED_VER_PATCH 0
#define RSID_EMBEDDED_VERSION   (RSID_EMBEDDED_VER_MAJOR * 10000 + RSID_EMBEDDED_VER_MINOR * 100 + RSID_EMBEDDED_VER_PATCH)

#define RSID_STR_HELPER(x) #x
#define RSID_STR(x)        RSID_STR_HELPER(x)

#define RSID_MAX_ROIS           5  /* Maximum number of detection ROIs */
#define RSID_MAX_USER_ID        30 /* Maximum length of a user ID string (not counting null terminator) */
#define RSID_NUM_FACE_LANDMARKS 5  /* Number of landmark points per face (matches C++ NUM_FACE_LANDMARKS) */

#ifdef __cplusplus
extern "C"
{
#endif

    /* ======================================================================
     * Platform interface
     * User-provided callbacks for communicating with the device.
     * All callbacks receive app_ctx as their last argument.
     * ====================================================================== */

    /**
     * User-provided platform interface for UART communication with the RealSenseID device.
     *
     * Required: send(), recv(), get_time_ms(), sleep_ms(), purge().
     * Optional: debug(), app_ctx — set to NULL if not needed.
     *
     * Example setup:
     *   rsid_platform_t platform;
     *   platform.send        = my_uart_send;
     *   platform.recv        = my_uart_recv;
     *   platform.get_time_ms = my_millis;
     *   platform.sleep_ms    = my_sleep;
     *   platform.purge       = my_uart_purge;
     *   platform.debug       = my_debug;           // optional (NULL ok, only called when compiled with -DRSID_DEBUG=1)
     *   platform.app_ctx   = &my_uart_handle;    // optional (NULL ok)
     */
    typedef struct
    {
        int (*send)(const uint8_t* data, uint32_t len, void* app_ctx);
        int (*recv)(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx);
        uint32_t (*get_time_ms)(void* app_ctx);
        void (*sleep_ms)(uint32_t ms, void* app_ctx);
        void (*purge)(void* app_ctx);
        void (*debug)(const char* msg, void* app_ctx); /* optional (NULL ok) */
        void* app_ctx;                                 /* optional (NULL ok) */
    } rsid_platform_t;

    /* ======================================================================
     * Context
     * ====================================================================== */

#define RSID_PACKET_BUF_SIZE 8184 /* sizeof(rsid_serial_packet_t) — keep in sync with rsid_packet.h */

    /**
     * SDK context. Holds platform config, session state, and a reusable packet buffer.
     * Requires ~9 KB of RAM.
     * Set ctx.platform before calling rsid_init().
     *
     * Thread safety: not thread-safe. Do not call API functions on the same ctx from
     * multiple threads. rsid_cancel() is the only function safe to call from another thread.
     */
    typedef struct rsid_ctx
    {
        rsid_platform_t platform; /* User-provided UART callbacks — fill before calling rsid_init() */
        struct
        {
            uint32_t last_sent_seq;            /* Last sequence number we sent */
            uint32_t last_recv_seq;            /* Last sequence number we received */
            volatile uint8_t cancel_requested; /* Set asynchronously; checked before each recv */
            uint8_t packet_buf[RSID_PACKET_BUF_SIZE];
        } _internal; /* SDK internal state — do not access directly */
    } rsid_ctx_t;

    /* ======================================================================
     * Status enums
     * ====================================================================== */

    /* General operation status — returned by all rsid_*() API functions.
     * Values must match C++ RealSenseID::Status (wire protocol compatibility). */
    typedef enum
    {
        RSID_Ok = 100,            /* Operation completed successfully */
        RSID_Error,               /* General/unspecified error */
        RSID_SerialError,         /* UART communication failure (timeout, framing, etc.) */
        RSID_SecurityError,       /* Security/authentication handshake failure */
        RSID_VersionMismatch,     /* Host and device protocol versions are incompatible */
        RSID_CrcError,            /* Packet CRC validation failed (data corruption) */
        RSID_TooManySpoofs,       /* Too many spoof attempts; device locked temporarily */
        RSID_NotSupported,        /* Requested feature is not available on this device/firmware */
        RSID_DatabaseFull,        /* No room for more enrolled users */
        RSID_DuplicateUserId,     /* A user with this ID is already enrolled */
        RSID_DuplicateFaceprints, /* Face is too similar to an already-enrolled user */
        RSID_InvalidSettings      /* One or more config values are out of range */
    } rsid_status;

    /* Authentication result/hint status — passed to on_result and on_hint callbacks.
     * Values must match C++ RealSenseID::AuthenticateStatus. */
    typedef enum
    {
        /* --- Final results (0-7) --- */
        RSID_Auth_Success = 0,     /* Face matched an enrolled user */
        RSID_Auth_NoFaceDetected,  /* No face found in camera frame */
        RSID_Auth_FaceDetected,    /* A face was detected (intermediate, not final) */
        RSID_Auth_PersonNotFound,  /* Face found but does not match any enrolled user */
        RSID_Auth_PersonFound,     /* Reserved */
        RSID_Auth_BarcodeNotFound, /* Barcode mode: no barcode detected */
        RSID_Auth_BarcodeFound,    /* Barcode mode: barcode detected */
        RSID_Auth_LedFlowSuccess,  /* LED-based liveness check passed */

        /* --- Face positioning hints (8-16) --- */
        RSID_Auth_FaceIsTooFarToTheTop,    /* Move face down */
        RSID_Auth_FaceIsTooFarToTheBottom, /* Move face up */
        RSID_Auth_FaceIsTooFarToTheRight,  /* Move face left */
        RSID_Auth_FaceIsTooFarToTheLeft,   /* Move face right */
        RSID_Auth_FaceTiltIsTooUp,         /* Tilt head down */
        RSID_Auth_FaceTiltIsTooDown,       /* Tilt head up */
        RSID_Auth_FaceTiltIsTooRight,      /* Tilt head left */
        RSID_Auth_FaceTiltIsTooLeft,       /* Tilt head right */
        RSID_Auth_FaceIsNotFrontal,        /* Face is angled away from camera */

        /* --- Camera state notifications (17-18) --- */
        RSID_Auth_CameraStarted, /* Camera has been activated */
        RSID_Auth_CameraStopped, /* Camera has been deactivated */

        /* --- Failure reasons (19-25) --- */
        RSID_Auth_Spoof,           /* Generic spoof (fake face) detected */
        RSID_Auth_Forbidden,       /* Operation not allowed in current state */
        RSID_Auth_DeviceError,     /* Internal device error */
        RSID_Auth_Failure,         /* General authentication failure */
        RSID_Auth_TooManySpoofs,   /* Too many consecutive spoof attempts */
        RSID_Auth_InvalidFeatures, /* Extracted face features are invalid */
        RSID_Auth_AmbiguousFace,   /* Multiple faces or ambiguous match */

        /* --- Accessory detection (50-51) --- */
        RSID_Auth_Sunglasses = 50, /* User is wearing sunglasses */
        RSID_Auth_MedicalMask,     /* User is wearing a medical mask */

        /* --- Distance hints (61-63) --- */
        RSID_Auth_FaceTooFar = 61,          /* Face is too far from camera */
        RSID_Auth_CalcDistanceFailure = 62, /* Could not calculate face distance */
        RSID_Auth_FaceTooClose = 63,        /* Face is too close to camera */

        /* --- Serial/platform errors (100+) --- */
        RSID_Auth_Serial_Ok = RSID_Ok,    /* 100 */
        RSID_Auth_Serial_Error,           /* 101 */
        RSID_Auth_Serial_SerialError,     /* 102 */
        RSID_Auth_Serial_SecurityError,   /* 103 */
        RSID_Auth_Serial_VersionMismatch, /* 104 */
        RSID_Auth_Serial_CrcError,        /* 105 */

        /* --- Spoof detection detail codes (120+) --- */
        RSID_Auth_Spoof_2D = 120,        /* 2D image spoof (printed photo, screen) */
        RSID_Auth_Spoof_3D,              /* 3D model/mask spoof */
        RSID_Auth_Spoof_LR,              /* Left-right disparity spoof */
        RSID_Auth_Spoof_Disparity,       /* Depth disparity check failed */
        RSID_Auth_Spoof_Vision,          /* Vision-based spoof detection */
        RSID_Auth_Spoof_Surface,         /* Surface texture spoof */
        RSID_Auth_Spoof_Plane_Disparity, /* Planar disparity check failed */
        RSID_Auth_Spoof_2D_Right         /* 2D spoof detected on right sensor */
    } rsid_auth_status;

    /* Enrollment result/hint status — passed to on_result and on_hint callbacks.
     * Values must match C++ RealSenseID::EnrollStatus. */
    typedef enum
    {
        /* --- Final results (0-7) --- */
        RSID_Enroll_Success = 0,     /* Enrollment completed successfully */
        RSID_Enroll_NoFaceDetected,  /* No face found in camera frame */
        RSID_Enroll_FaceDetected,    /* A face was detected (intermediate, not final) */
        RSID_Enroll_PersonNotFound,  /* Reserved */
        RSID_Enroll_PersonFound,     /* Reserved */
        RSID_Enroll_BarcodeNotFound, /* Barcode mode: no barcode detected */
        RSID_Enroll_BarcodeFound,    /* Barcode mode: barcode detected */
        RSID_Enroll_LedFlowSuccess,  /* LED-based liveness check passed */

        /* --- Face positioning hints (8-16) --- */
        RSID_Enroll_FaceIsTooFarToTheTop,    /* Move face down */
        RSID_Enroll_FaceIsTooFarToTheBottom, /* Move face up */
        RSID_Enroll_FaceIsTooFarToTheRight,  /* Move face left */
        RSID_Enroll_FaceIsTooFarToTheLeft,   /* Move face right */
        RSID_Enroll_FaceTiltIsTooUp,         /* Tilt head down */
        RSID_Enroll_FaceTiltIsTooDown,       /* Tilt head up */
        RSID_Enroll_FaceTiltIsTooRight,      /* Tilt head left */
        RSID_Enroll_FaceTiltIsTooLeft,       /* Tilt head right */
        RSID_Enroll_FaceIsNotFrontal,        /* Face is angled away from camera */

        /* --- Camera state notifications (17-18) --- */
        RSID_Enroll_CameraStarted, /* Camera has been activated */
        RSID_Enroll_CameraStopped, /* Camera has been deactivated */

        /* --- Failure reasons (19-24) --- */
        RSID_Enroll_MultipleFacesDetected, /* More than one face in frame */
        RSID_Enroll_Failure,               /* General enrollment failure */
        RSID_Enroll_DeviceError,           /* Internal device error */
        RSID_Enroll_Spoof,                 /* Generic spoof detected */
        RSID_Enroll_InvalidFeatures,       /* Extracted face features are invalid */
        RSID_Enroll_AmbiguousFace,         /* Face is ambiguous or partially occluded */

        /* --- Accessory detection (50-51) --- */
        RSID_Enroll_Sunglasses = 50, /* User is wearing sunglasses */
        RSID_Enroll_MedicalMask,     /* User is wearing a medical mask */

        /* --- Distance hints (63) --- */
        RSID_Enroll_FaceTooClose = 63, /* Face is too close to camera */

        /* --- Serial/platform errors (100+) --- */
        RSID_Enroll_Serial_Ok = RSID_Ok,    /* 100 */
        RSID_Enroll_Serial_Error,           /* 101 */
        RSID_Enroll_Serial_SerialError,     /* 102 */
        RSID_Enroll_Serial_SecurityError,   /* 103 */
        RSID_Enroll_Serial_VersionMismatch, /* 104 */
        RSID_Enroll_Serial_CrcError,        /* 105 */
        RSID_Enroll_TooManySpoofs,          /* 106 */
        RSID_Enroll_NotSupported,           /* 107 */
        RSID_Enroll_DatabaseFull,           /* 108 */
        RSID_Enroll_DuplicateUserId,        /* 109 */
        RSID_Enroll_DuplicateFaceprints,    /* 110 */

        /* --- Spoof detection detail codes (120+) --- */
        RSID_Enroll_Spoof_2D = 120,        /* 2D image spoof */
        RSID_Enroll_Spoof_3D,              /* 3D model/mask spoof */
        RSID_Enroll_Spoof_LR,              /* Left-right disparity spoof */
        RSID_Enroll_Spoof_Disparity,       /* Depth disparity check failed */
        RSID_Enroll_Spoof_Vision,          /* Vision-based spoof detection */
        RSID_Enroll_Spoof_Surface,         /* Surface texture spoof */
        RSID_Enroll_Spoof_Plane_Disparity, /* Planar disparity check failed */
        RSID_Enroll_Spoof_2D_Right         /* 2D spoof detected on right sensor */
    } rsid_enroll_status;

    /* Face pose — requested during enrollment. */
    typedef enum
    {
        RSID_Face_Center = 0, /* Look straight at the camera */
        RSID_Face_Up,         /* Tilt head up */
        RSID_Face_Down,       /* Tilt head down */
        RSID_Face_Left,       /* Turn head left */
        RSID_Face_Right       /* Turn head right */
    } rsid_face_pose;

    /* ======================================================================
     * Configuration enums and types
     * ====================================================================== */

    typedef enum
    {
        RSID_Rotation_0_Deg = 0,
        RSID_Rotation_180_Deg = 1,
        RSID_Rotation_90_Deg = 2,
        RSID_Rotation_270_Deg = 3
    } rsid_camera_rotation_type;

    typedef enum
    {
        RSID_SecLevel_High = 0,
        RSID_SecLevel_Medium = 1,
        RSID_SecLevel_Low = 2
    } rsid_security_level_type;

    typedef enum
    {
        RSID_AlgoMode_All = 0,
        RSID_AlgoMode_SpoofOnly = 1,
        RSID_AlgoMode_RecognitionOnly = 2
    } rsid_algo_mode_type;

    typedef enum
    {
        RSID_FaceSelection_Single = 0,
        RSID_FaceSelection_All = 1
    } rsid_face_selection_policy;

    typedef enum
    {
        RSID_DumpNone = 0,
        RSID_DumpCroppedFace = 1,
        RSID_DumpFullFrame = 2,
        RSID_DumpDebug = 3
    } rsid_dump_mode;

    typedef enum
    {
        RSID_FacePolicy_None = 0,
        RSID_FacePolicy_Moderate = 1,
        RSID_FacePolicy_Strict = 2
    } rsid_frontal_face_policy_type;

    typedef enum
    {
        RSID_PersonMotionMode_Static = 0,
        RSID_PersonMotionMode_Walkthrough = 1
    } rsid_person_motion_mode_type;

    /* Maximum supported value for rsid_device_config_t::distance_limit_cm.
     * Matches C++ AuthConfigCore::MAX_DISTANCE_CM. 0 = no distance limit. */
#define RSID_MAX_DISTANCE_CM 150

    /* Face bounding rectangle (returned by on_face_detected callback). */
    typedef struct
    {
        uint32_t x;
        uint32_t y;
        uint32_t w;
        uint32_t h;
    } rsid_face_rect;

    /* Facial landmark points (returned by on_landmarks_detected callback). */
    typedef struct
    {
        uint32_t lm_x[RSID_NUM_FACE_LANDMARKS];
        uint32_t lm_y[RSID_NUM_FACE_LANDMARKS];
    } rsid_face_landmarks;

    /* Detection region of interest. */
    typedef struct
    {
        uint16_t x;
        uint16_t y;
        uint16_t width;
        uint16_t height;
    } rsid_detection_roi;

    /* Device configuration — persists on the device across reboots. */
    typedef struct
    {
        rsid_camera_rotation_type camera_rotation;
        rsid_security_level_type security_level;
        rsid_algo_mode_type algo_mode;
        rsid_face_selection_policy face_selection_policy;
        rsid_dump_mode dump_mode;
        rsid_frontal_face_policy_type frontal_face_policy;
        rsid_person_motion_mode_type person_motion_mode;
        uint8_t max_spoofs;
        int gpio_auth_toggling;
        uint16_t match_thresh;
        uint16_t manual_exposure_time_us;
        uint16_t manual_gain;
        uint8_t rect_enable;
        uint8_t landmarks_enable;
        rsid_detection_roi rois[RSID_MAX_ROIS];
        uint8_t num_rois;
        uint8_t distance_limit_cm; /* 0 = no limit, 1..RSID_MAX_DISTANCE_CM (150) = distance in centimeters */
        uint8_t distance_enabled;
    } rsid_device_config_t;

    /* ======================================================================
     * Callbacks
     * ====================================================================== */

    /**
     * Callbacks invoked during an authentication operation.
     * All callbacks are optional (may be NULL).
     */
    typedef struct
    {
        void (*on_result)(rsid_auth_status status, const char* user_id, short score, void* ctx);
        void (*on_hint)(rsid_auth_status hint, float frame_score, void* ctx);
        void (*on_face_detected)(const rsid_face_rect* faces, unsigned int num_faces, unsigned int ts, void* ctx);
        void (*on_landmarks_detected)(const rsid_face_landmarks* landmarks, unsigned int num_faces, unsigned int ts, void* ctx);
        void (*on_face_distances)(const double* distances, unsigned int num_faces, unsigned int ts, void* ctx);
    } rsid_auth_callbacks_t;

    /**
     * Callbacks invoked during an enrollment operation.
     * All callbacks are optional (may be NULL).
     */
    typedef struct
    {
        void (*on_result)(rsid_enroll_status status, void* ctx);
        void (*on_progress)(rsid_face_pose pose, void* ctx);
        void (*on_hint)(rsid_enroll_status hint, float frame_score, void* ctx);
        void (*on_face_detected)(const rsid_face_rect* faces, unsigned int num_faces, unsigned int ts, void* ctx);
        void (*on_landmarks_detected)(const rsid_face_landmarks* landmarks, unsigned int num_faces, unsigned int ts, void* ctx);
        void (*on_face_distances)(const double* distances, unsigned int num_faces, unsigned int ts, void* ctx);
    } rsid_enroll_callbacks_t;

    /* ======================================================================
     * API functions
     * ====================================================================== */

    const char* rsid_version(void);
    rsid_status rsid_init(rsid_ctx_t* ctx);

    /* Face authentication */
    rsid_status rsid_enroll(rsid_ctx_t* ctx, const char* user_id, const rsid_enroll_callbacks_t* cb, void* cb_ctx);
    rsid_status rsid_authenticate(rsid_ctx_t* ctx, const rsid_auth_callbacks_t* cb, void* cb_ctx);
    rsid_status rsid_authenticate_loop(rsid_ctx_t* ctx, const rsid_auth_callbacks_t* cb, void* cb_ctx);
    rsid_status rsid_cancel(rsid_ctx_t* ctx);

    /* User management */
    rsid_status rsid_remove_user(rsid_ctx_t* ctx, const char* user_id);
    rsid_status rsid_remove_all(rsid_ctx_t* ctx);
    rsid_status rsid_query_number_of_users(rsid_ctx_t* ctx, unsigned int* count);
    rsid_status rsid_query_user_ids(rsid_ctx_t* ctx, char (*user_ids)[RSID_MAX_USER_ID + 1], unsigned int* count);

    /* Device configuration */
    rsid_status rsid_set_device_config(rsid_ctx_t* ctx, const rsid_device_config_t* config);
    rsid_status rsid_query_device_config(rsid_ctx_t* ctx, rsid_device_config_t* config);

    /* Device control */
    rsid_status rsid_ping(rsid_ctx_t* ctx);
    rsid_status rsid_query_firmware_version(rsid_ctx_t* ctx, char* output, unsigned int output_len);
    rsid_status rsid_query_bsp_version(rsid_ctx_t* ctx, char* output, unsigned int output_len);
    rsid_status rsid_query_serial_number(rsid_ctx_t* ctx, char* output, unsigned int output_len);
    rsid_status rsid_query_otp_version(rsid_ctx_t* ctx, uint8_t* version);
    rsid_status rsid_get_temperature(rsid_ctx_t* ctx, float* soc_temp, float* board_temp);
    rsid_status rsid_get_color_gains(rsid_ctx_t* ctx, int* red, int* blue);
    rsid_status rsid_set_color_gains(rsid_ctx_t* ctx, int red, int blue);
    rsid_status rsid_reboot(rsid_ctx_t* ctx);
    rsid_status rsid_standby(rsid_ctx_t* ctx);
    rsid_status rsid_hibernate(rsid_ctx_t* ctx);
    rsid_status rsid_unlock(rsid_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* RSID_H */
