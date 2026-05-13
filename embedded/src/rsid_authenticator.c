/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/*
 * rsid_authenticator.c — Face authentication API implementation.
 *
 * Implements enroll, authenticate, user management, and device config operations.
 * All operations follow a common pattern:
 *
 *   1. start_session(ctx)        — handshake with device (see rsid_session.c)
 *   2. rsid_packet_init_*(pkt)   — build the command packet
 *   3. send_packet(ctx)          — send via session (auto-increments seq number)
 *   4. recv_packet(ctx) loop     — receive responses until Reply('Y') terminal packet
 *   5. return status             — map serial/fa status to public rsid_status
 *
 * The packet buffer is shared for both send and recv — never use both simultaneously.
 */

#include "rsid_common.h"

/* Timeouts (milliseconds) — must match C++ FaceAuthenticatorCommon constants */
#define RSID_ENROLL_TIMEOUT_MS     12000
#define RSID_AUTH_TIMEOUT_MS       10000
#define RSID_REMOVE_ALL_TIMEOUT_MS 34000
#define RSID_GPIO_WIRE_ENABLED     0x0b /* Wire encoding for gpio_auth_toggling enabled (matches C++ AuthConfigPayload) */

/* Max users returned per QueryUserIds request — matches C++ QUERY_CHUNK_SIZE */
#define RSID_QUERY_CHUNK_SIZE 50

/* Max faces/landmarks/distances per frame */
#define RSID_MAX_FACES 5

/* Wire-format sizes for face data packets */
#define RSID_FACE_HEADER_SIZE (1 + sizeof(uint32_t)) /* n_faces(1) + ts(4) */
#define RSID_FACE_RECT_SIZE   (4 * sizeof(uint32_t)) /* x, y, w, h */
#define RSID_LANDMARKS_SIZE   (sizeof(rsid_face_landmarks))

/* Map serial status to auth status for error callbacks */
static rsid_auth_status serial_to_auth_status(rsid_serial_status ss)
{
    switch (ss)
    {
    case RSID_SERIAL_OK:
        return RSID_Auth_Serial_Ok;
    case RSID_SERIAL_SEND_FAILED:
    case RSID_SERIAL_RECV_FAILED:
    case RSID_SERIAL_RECV_TIMEOUT:
        return RSID_Auth_Serial_SerialError;
    case RSID_SERIAL_VERSION_MISMATCH:
        return RSID_Auth_Serial_VersionMismatch;
    case RSID_SERIAL_CRC_ERROR:
        return RSID_Auth_Serial_CrcError;
    case RSID_SERIAL_SECURITY_ERROR:
        return RSID_Auth_Serial_SecurityError;
    default:
        return RSID_Auth_Serial_Error;
    }
}

/* Map serial status to enroll status for error callbacks */
static rsid_enroll_status serial_to_enroll_status(rsid_serial_status ss)
{
    switch (ss)
    {
    case RSID_SERIAL_OK:
        return RSID_Enroll_Serial_Ok;
    case RSID_SERIAL_SEND_FAILED:
    case RSID_SERIAL_RECV_FAILED:
    case RSID_SERIAL_RECV_TIMEOUT:
        return RSID_Enroll_Serial_SerialError;
    case RSID_SERIAL_VERSION_MISMATCH:
        return RSID_Enroll_Serial_VersionMismatch;
    case RSID_SERIAL_CRC_ERROR:
        return RSID_Enroll_Serial_CrcError;
    case RSID_SERIAL_SECURITY_ERROR:
        return RSID_Enroll_Serial_SecurityError;
    default:
        return RSID_Enroll_Serial_Error;
    }
}

/*
 * Convert device-sourced FA status byte to typed enums with bounds checking.
 * Rejects values outside the known protocol ranges. Gaps within ranges are
 * not validated — new values can be added within bands without updating these.
 */
static rsid_status fa_to_status(rsid_platform_t* platform, char raw)
{
    if (raw >= RSID_Ok && raw <= RSID_InvalidSettings)
        return (rsid_status)raw;
    RSID_DBG(platform, "unknown status from device");
    return RSID_Error;
}

static rsid_auth_status fa_to_auth_status(rsid_platform_t* platform, char raw)
{
    if (raw >= RSID_Auth_Success && raw <= RSID_Auth_Spoof_2D_Right)
        return (rsid_auth_status)raw;
    RSID_DBG(platform, "unknown auth status from device");
    return RSID_Auth_Failure;
}

static rsid_enroll_status fa_to_enroll_status(rsid_platform_t* platform, char raw)
{
    if (raw >= RSID_Enroll_Success && raw <= RSID_Enroll_Spoof_2D_Right)
        return (rsid_enroll_status)raw;
    RSID_DBG(platform, "unknown enroll status from device");
    return RSID_Enroll_Failure;
}

static rsid_face_pose fa_to_face_pose(rsid_platform_t* platform, char raw)
{
    if (raw >= RSID_Face_Center && raw <= RSID_Face_Right)
        return (rsid_face_pose)raw;
    RSID_DBG(platform, "unknown face pose from device");
    return RSID_Face_Center;
}

/* Report a serial error to the auth callback and return the mapped status */
static rsid_status report_auth_error(rsid_ctx_t* ctx, rsid_serial_status ss, const rsid_auth_callbacks_t* cb, void* cb_ctx)
{
    RSID_DBG(&ctx->platform, "auth serial error (status=%d)", (int)ss);
    if (cb && cb->on_result)
        cb->on_result(serial_to_auth_status(ss), "", 0, cb_ctx);
    return serial_to_status(ss);
}

/* Report a serial error to the enroll callback and return the mapped status */
static rsid_status report_enroll_error(rsid_ctx_t* ctx, rsid_serial_status ss, const rsid_enroll_callbacks_t* cb, void* cb_ctx)
{
    RSID_DBG(&ctx->platform, "enroll serial error (status=%d)", (int)ss);
    if (cb && cb->on_result)
        cb->on_result(serial_to_enroll_status(ss), cb_ctx);
    return serial_to_status(ss);
}

/* Start session with retries */
static rsid_serial_status start_session(rsid_ctx_t* ctx)
{
    return rsid_session_start(ctx);
}

/* Send packet via session */
static rsid_serial_status send_packet(rsid_ctx_t* ctx)
{
    return rsid_session_send(ctx);
}

/* Receive packet via session with default timeout */
static rsid_serial_status recv_packet(rsid_ctx_t* ctx)
{
    return rsid_session_recv(ctx, RSID_DEFAULT_RECV_TIMEOUT_MS);
}

/* Validate user_id: non-null, non-empty, length <= RSID_MAX_USER_ID */
static int validate_user_id(const char* user_id)
{
    size_t len;
    if (user_id == NULL)
        return 0;
    len = rsid_strnlen(user_id, RSID_MAX_USER_ID + 1);
    return (len > 0 && len <= RSID_MAX_USER_ID);
}

/*
 * Parse face rectangles from a FaceDetected ('g') data packet.
 * Wire layout of data_msg.data (matches C++ GetDetectedFaces):
 *   [0]     uint8_t   n_faces
 *   [1..4]  uint32_t  timestamp
 *   [5..]   n_faces * { uint32_t x, y, w, h } (16 bytes each)
 */
static unsigned int parse_face_detected(const rsid_serial_packet_t* pkt, rsid_face_rect* faces, unsigned int max_faces,
                                        unsigned int* out_ts)
{
    const char* data = pkt->payload.message.data_msg.data;
    uint16_t payload_size = pkt->header.payload_size;
    unsigned int n_faces;
    uint32_t ts = 0;
    unsigned int i;
    const char* cursor;

    if (payload_size < sizeof(pkt->payload.sequence_number) + RSID_FACE_HEADER_SIZE)
        return 0;

    n_faces = (unsigned int)(unsigned char)data[0];
    memcpy(&ts, data + 1, sizeof(uint32_t));

    if (n_faces > max_faces)
        n_faces = max_faces;

    /* Validate payload has enough bytes for the claimed face count */
    {
        uint32_t needed = (uint32_t)(sizeof(pkt->payload.sequence_number) + RSID_FACE_HEADER_SIZE + n_faces * RSID_FACE_RECT_SIZE);
        if (needed > payload_size)
            n_faces = 0;
    }

    cursor = data + RSID_FACE_HEADER_SIZE;
    for (i = 0; i < n_faces; i++)
    {
        memcpy(&faces[i].x, cursor, sizeof(uint32_t));
        memcpy(&faces[i].y, cursor + sizeof(uint32_t), sizeof(uint32_t));
        memcpy(&faces[i].w, cursor + 2 * sizeof(uint32_t), sizeof(uint32_t));
        memcpy(&faces[i].h, cursor + 3 * sizeof(uint32_t), sizeof(uint32_t));
        cursor += RSID_FACE_RECT_SIZE;
    }

    *out_ts = ts;
    return n_faces;
}

/*
 * Parse face landmarks from a LandmarksDetected ('h') data packet.
 * Wire layout (matches C++ GetDetectedLandmarks):
 *   [0]     uint8_t   n_faces
 *   [1..4]  uint32_t  timestamp
 *   [5..]   n_faces * rsid_face_landmarks (40 bytes each: 5 x uint32_t + 5 y uint32_t)
 */
static unsigned int parse_landmarks_detected(const rsid_serial_packet_t* pkt, rsid_face_landmarks* landmarks, unsigned int max_faces,
                                             unsigned int* out_ts)
{
    const char* data = pkt->payload.message.data_msg.data;
    uint16_t payload_size = pkt->header.payload_size;
    unsigned int n_faces;
    uint32_t ts = 0;
    unsigned int i;
    const char* cursor;

    if (payload_size < sizeof(pkt->payload.sequence_number) + RSID_FACE_HEADER_SIZE)
        return 0;

    n_faces = (unsigned int)(unsigned char)data[0];
    memcpy(&ts, data + 1, sizeof(uint32_t));

    if (n_faces > max_faces)
        n_faces = max_faces;

    {
        uint32_t needed = (uint32_t)(sizeof(pkt->payload.sequence_number) + RSID_FACE_HEADER_SIZE + n_faces * RSID_LANDMARKS_SIZE);
        if (needed > payload_size)
            n_faces = 0;
    }

    cursor = data + RSID_FACE_HEADER_SIZE;
    for (i = 0; i < n_faces; i++)
    {
        memcpy(&landmarks[i], cursor, RSID_LANDMARKS_SIZE);
        cursor += RSID_LANDMARKS_SIZE;
    }

    *out_ts = ts;
    return n_faces;
}

/*
 * Parse face distances from a FaceDistances ('m') data packet.
 * Wire layout (matches C++ GetFaceDistances):
 *   [0]     uint8_t   n_distances
 *   [1..4]  uint32_t  timestamp
 *   [5..]   n_distances * double (8 bytes each)
 */
static unsigned int parse_face_distances(const rsid_serial_packet_t* pkt, double* distances, unsigned int max_count, unsigned int* out_ts)
{
    const char* data = pkt->payload.message.data_msg.data;
    uint16_t payload_size = pkt->header.payload_size;
    unsigned int n;
    uint32_t ts = 0;
    unsigned int i;
    const char* cursor;

    if (payload_size < sizeof(pkt->payload.sequence_number) + RSID_FACE_HEADER_SIZE)
        return 0;

    n = (unsigned int)(unsigned char)data[0];
    memcpy(&ts, data + 1, sizeof(uint32_t));

    if (n > max_count)
        n = max_count;

    {
        uint32_t needed = (uint32_t)(sizeof(pkt->payload.sequence_number) + RSID_FACE_HEADER_SIZE + n * sizeof(double));
        if (needed > payload_size)
            n = 0;
    }

    cursor = data + RSID_FACE_HEADER_SIZE;
    for (i = 0; i < n; i++)
    {
        memcpy(&distances[i], cursor, sizeof(double));
        cursor += sizeof(double);
    }

    *out_ts = ts;
    return n;
}

/* ---- Public API ---- */
const char* rsid_version(void)
{
    return RSID_STR(RSID_EMBEDDED_VER_MAJOR) "." RSID_STR(RSID_EMBEDDED_VER_MINOR) "." RSID_STR(RSID_EMBEDDED_VER_PATCH);
}

rsid_status rsid_init(rsid_ctx_t* ctx)
{
    if (!validate_ctx(ctx))
        return RSID_Error;

    ctx->_internal.last_sent_seq = 0;
    ctx->_internal.last_recv_seq = 0;
    ctx->_internal.cancel_requested = 0;
    return RSID_Ok;
}

rsid_status rsid_enroll(rsid_ctx_t* ctx, const char* user_id, const rsid_enroll_callbacks_t* cb, void* cb_ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    uint32_t start_ms;

    if (!validate_ctx(ctx))
        return RSID_Error;

    if (!validate_user_id(user_id))
    {
        RSID_DBG(&ctx->platform, "enroll: bad user_id");
        return RSID_Error;
    }

    RSID_DBG(&ctx->platform, "enroll user='%s'", user_id);
    pkt = ctx_packet(ctx);

    /* Start session */
    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return report_enroll_error(ctx, ss, cb, cb_ctx);

    /* Send Enroll FA packet */
    rsid_packet_init_fa(pkt, RSID_MSGID_ENROLL, user_id, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return report_enroll_error(ctx, ss, cb, cb_ctx);

    /* Receive loop */
    start_ms = ctx->platform.get_time_ms(ctx->platform.app_ctx);
    while (1)
    {
        char msg_id;
        char fa_status;
        rsid_enroll_status enroll_status;

        if ((ctx->platform.get_time_ms(ctx->platform.app_ctx) - start_ms) >= RSID_ENROLL_TIMEOUT_MS)
        {
            RSID_DBG(&ctx->platform, "enroll timeout");
            if (cb && cb->on_result)
                cb->on_result(RSID_Enroll_Failure, cb_ctx);
            rsid_cancel(ctx);
            return RSID_Error;
        }

        ss = recv_packet(ctx);
        if (ss != RSID_SERIAL_OK)
            return report_enroll_error(ctx, ss, cb, cb_ctx);

        msg_id = pkt->header.msg_id;

        switch (msg_id)
        {
        case RSID_MSGID_REPLY:
            return fa_to_status(&ctx->platform, rsid_packet_get_status_code(pkt));

        case RSID_MSGID_RESULT: {
            fa_status = rsid_packet_get_status_code(pkt);
            enroll_status = fa_to_enroll_status(&ctx->platform, fa_status);
            if (cb && cb->on_result)
                cb->on_result(enroll_status, cb_ctx);
            break;
        }

        case RSID_MSGID_PROGRESS: {
            fa_status = rsid_packet_get_status_code(pkt);
            if (cb && cb->on_progress)
                cb->on_progress(fa_to_face_pose(&ctx->platform, fa_status), cb_ctx);
            break;
        }

        case RSID_MSGID_HINT: {
            float frame_score = 0.0f;
            fa_status = rsid_packet_get_status_code(pkt);
            enroll_status = fa_to_enroll_status(&ctx->platform, fa_status);
            memcpy(&frame_score, rsid_packet_get_reserved(pkt), sizeof(float));
            if (cb && cb->on_hint)
                cb->on_hint(enroll_status, frame_score, cb_ctx);
            break;
        }

        case RSID_MSGID_FACE_DETECTED: {
            rsid_face_rect faces[RSID_MAX_FACES];
            unsigned int ts = 0;
            unsigned int n = parse_face_detected(pkt, faces, RSID_MAX_FACES, &ts);
            if (cb && cb->on_face_detected)
                cb->on_face_detected(faces, n, ts, cb_ctx);
            break;
        }

        case RSID_MSGID_LANDMARKS_DETECTED: {
            rsid_face_landmarks landmarks[RSID_MAX_FACES];
            unsigned int ts = 0;
            unsigned int n = parse_landmarks_detected(pkt, landmarks, RSID_MAX_FACES, &ts);
            if (n > 0 && cb && cb->on_landmarks_detected)
                cb->on_landmarks_detected(landmarks, n, ts, cb_ctx);
            break;
        }

        case RSID_MSGID_FACE_DISTANCES: {
            double distances[RSID_MAX_FACES];
            unsigned int ts = 0;
            unsigned int n = parse_face_distances(pkt, distances, RSID_MAX_FACES, &ts);
            if (n > 0 && cb && cb->on_face_distances)
                cb->on_face_distances(distances, n, ts, cb_ctx);
            break;
        }

        default:
            RSID_DBG(&ctx->platform, "received unknown msg_id in enroll: %d ('%c')", (int)msg_id, msg_id);
            break;
        }
    }
}

/* Process a received auth packet. Returns 0 if terminal (Reply received), 1 to continue. */
static int process_auth_packet(rsid_platform_t* platform, rsid_serial_packet_t* pkt, const rsid_auth_callbacks_t* cb, void* cb_ctx,
                               rsid_status* out_status)
{
    char msg_id = pkt->header.msg_id;
    char fa_status;
    rsid_auth_status auth_status;

    switch (msg_id)
    {
    case RSID_MSGID_FACE_DETECTED:
        if (cb && cb->on_face_detected)
        {
            rsid_face_rect faces[RSID_MAX_FACES];
            unsigned int ts = 0;
            unsigned int n = parse_face_detected(pkt, faces, RSID_MAX_FACES, &ts);
            cb->on_face_detected(faces, n, ts, cb_ctx);
        }
        break;

    case RSID_MSGID_LANDMARKS_DETECTED:
        if (cb && cb->on_landmarks_detected)
        {
            rsid_face_landmarks landmarks[RSID_MAX_FACES];
            unsigned int ts = 0;
            unsigned int n = parse_landmarks_detected(pkt, landmarks, RSID_MAX_FACES, &ts);
            if (n > 0)
                cb->on_landmarks_detected(landmarks, n, ts, cb_ctx);
        }
        break;

    case RSID_MSGID_FACE_DISTANCES:
        if (cb && cb->on_face_distances)
        {
            double distances[RSID_MAX_FACES];
            unsigned int ts = 0;
            unsigned int n = parse_face_distances(pkt, distances, RSID_MAX_FACES, &ts);
            if (n > 0)
                cb->on_face_distances(distances, n, ts, cb_ctx);
        }
        break;

    case RSID_MSGID_HINT: {
        float frame_score = 0.0f;
        fa_status = rsid_packet_get_status_code(pkt);
        auth_status = fa_to_auth_status(platform, fa_status);
        memcpy(&frame_score, rsid_packet_get_reserved(pkt), sizeof(float));
        if (cb && cb->on_hint)
            cb->on_hint(auth_status, frame_score, cb_ctx);
        break;
    }

    case RSID_MSGID_RESULT: {
        short score = 0;
        fa_status = rsid_packet_get_status_code(pkt);
        auth_status = fa_to_auth_status(platform, fa_status);
        memcpy(&score, rsid_packet_get_reserved(pkt), sizeof(short));
        if (cb && cb->on_result)
            cb->on_result(auth_status, rsid_packet_get_user_id(pkt), score, cb_ctx);
        break;
    }

    case RSID_MSGID_REPLY:
        fa_status = rsid_packet_get_status_code(pkt);
        *out_status = fa_to_status(platform, fa_status);
        return 0; /* End of session */

    default:
        RSID_DBG(platform, "received unknown msg_id in auth: %d ('%c')", (int)msg_id, msg_id);
        break;
    }

    return 1; /* Continue */
}

rsid_status rsid_authenticate(rsid_ctx_t* ctx, const rsid_auth_callbacks_t* cb, void* cb_ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    uint32_t start_ms;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "auth start");
    pkt = ctx_packet(ctx);

    /* Start session */
    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return report_auth_error(ctx, ss, cb, cb_ctx);

    /* Send Authenticate FA packet */
    rsid_packet_init_fa(pkt, RSID_MSGID_AUTHENTICATE, NULL, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return report_auth_error(ctx, ss, cb, cb_ctx);

    /* Receive loop */
    start_ms = ctx->platform.get_time_ms(ctx->platform.app_ctx);
    while (1)
    {
        rsid_status final_status = RSID_Ok;

        if ((ctx->platform.get_time_ms(ctx->platform.app_ctx) - start_ms) >= RSID_AUTH_TIMEOUT_MS)
        {
            RSID_DBG(&ctx->platform, "auth timeout");
            if (cb && cb->on_result)
                cb->on_result(RSID_Auth_Forbidden, "", 0, cb_ctx);
            rsid_cancel(ctx);
            return RSID_Error;
        }

        ss = recv_packet(ctx);
        if (ss != RSID_SERIAL_OK)
            return report_auth_error(ctx, ss, cb, cb_ctx);

        if (!process_auth_packet(&ctx->platform, pkt, cb, cb_ctx, &final_status))
            return final_status;
    }
}

rsid_status rsid_authenticate_loop(rsid_ctx_t* ctx, const rsid_auth_callbacks_t* cb, void* cb_ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    uint32_t ka_start;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "auth-loop start");
    pkt = ctx_packet(ctx);

    /* Start session */
    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return report_auth_error(ctx, ss, cb, cb_ctx);

    /* Send AuthenticateLoop FA packet */
    rsid_packet_init_fa(pkt, RSID_MSGID_AUTHENTICATE_LOOP, NULL, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return report_auth_error(ctx, ss, cb, cb_ctx);

    ka_start = ctx->platform.get_time_ms(ctx->platform.app_ctx);

    while (!ctx->_internal.cancel_requested)
    {
        rsid_status final_status = RSID_Ok;

        /* Send keep-alive every interval to prevent device-side timeout */
        if ((ctx->platform.get_time_ms(ctx->platform.app_ctx) - ka_start) >= RSID_KEEP_ALIVE_INTERVAL_MS)
        {
            rsid_packet_init_fa(pkt, RSID_MSGID_PROGRESS, NULL, 1);
            ss = send_packet(ctx);
            RSID_DBG(&ctx->platform, ss == RSID_SERIAL_OK ? "keep-alive sent" : "keep-alive send failed");
            ka_start = ctx->platform.get_time_ms(ctx->platform.app_ctx);
        }

        ss = recv_packet(ctx);
        if (ss != RSID_SERIAL_OK)
        {
            /* If recv failed due to cancel, exit cleanly */
            if (ctx->_internal.cancel_requested)
                break;
            return report_auth_error(ctx, ss, cb, cb_ctx);
        }

        if (!process_auth_packet(&ctx->platform, pkt, cb, cb_ctx, &final_status))
            return final_status;
    }

    /* Cancelled — rsid_cancel() already sent __FACE_CANCEL__ to the device */
    RSID_DBG(&ctx->platform, "auth-loop cancelled");
    return RSID_Ok;
}

rsid_status rsid_cancel(rsid_ctx_t* ctx)
{
    if (!validate_ctx(ctx))
        return RSID_Error;

    ctx->_internal.cancel_requested = 1;
    rsid_session_send_cancel(ctx);
    return RSID_Ok;
}

rsid_status rsid_remove_user(rsid_ctx_t* ctx, const char* user_id)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "remove user='%s'", user_id ? user_id : "(null)");
    pkt = ctx_packet(ctx);

    if (!validate_user_id(user_id))
    {
        RSID_DBG(&ctx->platform, "remove: bad user_id");
        return RSID_Error;
    }

    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    rsid_packet_init_fa(pkt, RSID_MSGID_REMOVE_USER, user_id, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    /* Wait for Reply */
    ss = recv_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    if (pkt->header.msg_id != RSID_MSGID_REPLY)
    {
        RSID_DBG(&ctx->platform, "remove: unexpected reply");
        return RSID_Error;
    }

    return fa_to_status(&ctx->platform, rsid_packet_get_status_code(pkt));
}

rsid_status rsid_remove_all(rsid_ctx_t* ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;

    if (!validate_ctx(ctx))
        return RSID_Error;

    RSID_DBG(&ctx->platform, "remove all");
    pkt = ctx_packet(ctx);

    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    rsid_packet_init_fa(pkt, RSID_MSGID_REMOVE_ALL, NULL, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    /* RemoveAll can take a long time for large databases */
    ss = rsid_session_recv(ctx, RSID_REMOVE_ALL_TIMEOUT_MS);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    if (pkt->header.msg_id != RSID_MSGID_REPLY)
    {
        RSID_DBG(&ctx->platform, "remove-all: unexpected reply");
        return RSID_Error;
    }

    return fa_to_status(&ctx->platform, rsid_packet_get_status_code(pkt));
}

rsid_status rsid_query_number_of_users(rsid_ctx_t* ctx, unsigned int* count)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    uint32_t n = 0;

    if (ctx == NULL || count == NULL)
    {
        RSID_DBG(ctx ? &ctx->platform : NULL, "num-users: invalid args");
        return RSID_Error;
    }
    pkt = ctx_packet(ctx);
    *count = 0;

    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    rsid_packet_init_data(pkt, RSID_MSGID_GET_NUM_USERS, NULL, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    ss = recv_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    if (pkt->header.msg_id != RSID_MSGID_GET_NUM_USERS)
    {
        RSID_DBG(&ctx->platform, "num-users: unexpected reply");
        return RSID_Error;
    }

    if (pkt->header.payload_size < sizeof(pkt->payload.sequence_number) + sizeof(uint32_t))
    {
        RSID_DBG(&ctx->platform, "num-users: payload too small");
        return RSID_Error;
    }

    memcpy(&n, pkt->payload.message.data_msg.data, sizeof(uint32_t));
    *count = (unsigned int)n;
    return RSID_Ok;
}

/*
 * Retrieve enrolled user IDs in chunks of 50. Each chunk requires a new session
 * because the device closes the session after responding. The request sends
 * {offset, chunk_size} as two uint32_t values; the response contains
 * {count} followed by NUL-terminated user ID strings packed sequentially.
 */
rsid_status rsid_query_user_ids(rsid_ctx_t* ctx, char (*user_ids)[RSID_MAX_USER_ID + 1], unsigned int* count)
{
    rsid_serial_packet_t* pkt;
    unsigned int max_users;
    unsigned int retrieved = 0;
    unsigned int chunk_size = RSID_QUERY_CHUNK_SIZE;

    if (ctx == NULL || user_ids == NULL || count == NULL || *count == 0)
    {
        RSID_DBG(ctx ? &ctx->platform : NULL, "user-ids: invalid args");
        return RSID_Error;
    }
    pkt = ctx_packet(ctx);

    max_users = *count;
    *count = 0;

    while (retrieved < max_users)
    {
        rsid_serial_status ss;
        uint32_t settings[2];
        uint32_t arrived = 0;
        const char* data;
        uint32_t j;
        size_t cur_pos;

        ss = start_session(ctx);
        if (ss != RSID_SERIAL_OK)
            return serial_to_status(ss);

        settings[0] = retrieved;
        settings[1] = chunk_size;

        rsid_packet_init_data(pkt, RSID_MSGID_GET_USER_IDS, (const char*)settings, sizeof(settings));
        ss = send_packet(ctx);
        if (ss != RSID_SERIAL_OK)
            return serial_to_status(ss);

        ss = recv_packet(ctx);
        if (ss != RSID_SERIAL_OK)
            return serial_to_status(ss);

        if (pkt->header.msg_id != RSID_MSGID_GET_USER_IDS)
        {
            RSID_DBG(&ctx->platform, "user-ids: unexpected reply");
            return RSID_Error;
        }

        data = pkt->payload.message.data_msg.data;
        {
            /* data_len = actual received data bytes (payload minus sequence number) */
            uint16_t data_len = pkt->header.payload_size > sizeof(pkt->payload.sequence_number)
                                    ? pkt->header.payload_size - (uint16_t)sizeof(pkt->payload.sequence_number)
                                    : 0;

            if (data_len < sizeof(uint32_t))
            {
                RSID_DBG(&ctx->platform, "user-ids: payload too small");
                return RSID_Error;
            }

            memcpy(&arrived, data, sizeof(uint32_t));

            if (arrived == 0)
                break;

            cur_pos = sizeof(uint32_t);
            for (j = 0; j < arrived && retrieved < max_users; j++)
            {
                size_t remaining;
                size_t id_len;
                if (cur_pos >= data_len)
                {
                    RSID_DBG(&ctx->platform, "user-ids: data overflow");
                    return RSID_Error;
                }
                remaining = data_len - cur_pos;
                id_len = rsid_strnlen(&data[cur_pos], remaining);
                if (id_len == 0)
                {
                    RSID_DBG(&ctx->platform, "user-ids: empty id");
                    return RSID_Error;
                }
                if (id_len == remaining)
                {
                    RSID_DBG(&ctx->platform, "user-ids: missing NUL");
                    return RSID_Error;
                }
                if (id_len > RSID_MAX_USER_ID)
                {
                    RSID_DBG(&ctx->platform, "user-ids: id too long");
                    return RSID_Error;
                }
                memcpy(user_ids[retrieved], &data[cur_pos], id_len);
                user_ids[retrieved][id_len] = '\0';
                cur_pos += id_len + 1; /* skip past NUL terminator */
                retrieved++;
            }
        }
    }

    *count = retrieved;
    return RSID_Ok;
}

/*
 * Wire-format config payload — must match C++ AuthConfigPayload (60 bytes).
 * Sent inside a data_msg for SetConfig ('s') and QueryConfig ('q') messages.
 */
#pragma pack(push, 1)
typedef struct
{
    uint16_t x;
    uint16_t y;
    uint16_t w;
    uint16_t h;
} rsid_config_roi_entry_t;

typedef struct
{
    uint8_t camera_rotation;
    uint8_t security_level;
    uint8_t algo_flow;
    uint8_t gpio_auth_toggling; /* RSID_GPIO_WIRE_ENABLED = enabled, 0x00 = disabled (not a simple bool) */
    uint8_t dump_mode;
    uint8_t frontal_face_policy;
    uint8_t person_motion_mode;
    uint8_t max_spoofs;
    uint16_t match_thresh;
    uint8_t face_selection_policy;
    uint16_t manual_exposure_time_us;
    uint16_t manual_gain;
    uint8_t rect_enable;
    uint8_t landmarks_enable;
    rsid_config_roi_entry_t detection_rois[5];
    uint8_t distance_limit_cm;
    uint8_t distance_enabled;
    uint8_t num_rois;
} rsid_auth_config_payload_t;
#pragma pack(pop)

static void config_to_payload(const rsid_device_config_t* config, rsid_auth_config_payload_t* p)
{
    unsigned int i;
    memset(p, 0, sizeof(*p));
    p->camera_rotation = (uint8_t)config->camera_rotation;
    p->security_level = (uint8_t)config->security_level;
    p->algo_flow = (uint8_t)config->algo_mode;
    p->gpio_auth_toggling = (config->gpio_auth_toggling == 1) ? RSID_GPIO_WIRE_ENABLED : 0x00;
    p->dump_mode = (uint8_t)config->dump_mode;
    p->frontal_face_policy = (uint8_t)config->frontal_face_policy;
    p->person_motion_mode = (uint8_t)config->person_motion_mode;
    p->max_spoofs = config->max_spoofs;
    p->match_thresh = config->match_thresh;
    p->face_selection_policy = (uint8_t)config->face_selection_policy;
    p->manual_exposure_time_us = config->manual_exposure_time_us;
    p->manual_gain = config->manual_gain;
    p->rect_enable = config->rect_enable;
    p->landmarks_enable = config->landmarks_enable;
    for (i = 0; i < RSID_MAX_ROIS && i < config->num_rois; i++)
    {
        p->detection_rois[i].x = config->rois[i].x;
        p->detection_rois[i].y = config->rois[i].y;
        p->detection_rois[i].w = config->rois[i].width;
        p->detection_rois[i].h = config->rois[i].height;
    }
    p->distance_limit_cm = config->distance_limit_cm;
    p->distance_enabled = config->distance_enabled ? 1 : 0;
    p->num_rois = config->num_rois;
}

static void payload_to_config(const rsid_auth_config_payload_t* p, rsid_device_config_t* config)
{
    unsigned int i;
    config->camera_rotation = (rsid_camera_rotation_type)p->camera_rotation;
    config->security_level = (rsid_security_level_type)p->security_level;
    config->algo_mode = (rsid_algo_mode_type)p->algo_flow;
    config->gpio_auth_toggling = (p->gpio_auth_toggling == RSID_GPIO_WIRE_ENABLED) ? 1 : 0;
    config->dump_mode = (rsid_dump_mode)p->dump_mode;
    config->frontal_face_policy = (rsid_frontal_face_policy_type)p->frontal_face_policy;
    config->person_motion_mode = (rsid_person_motion_mode_type)p->person_motion_mode;
    config->max_spoofs = p->max_spoofs;
    config->match_thresh = p->match_thresh;
    config->face_selection_policy = (rsid_face_selection_policy)p->face_selection_policy;
    config->manual_exposure_time_us = p->manual_exposure_time_us;
    config->manual_gain = p->manual_gain;
    config->rect_enable = p->rect_enable;
    config->landmarks_enable = p->landmarks_enable;
    for (i = 0; i < RSID_MAX_ROIS; i++)
    {
        config->rois[i].x = p->detection_rois[i].x;
        config->rois[i].y = p->detection_rois[i].y;
        config->rois[i].width = p->detection_rois[i].w;
        config->rois[i].height = p->detection_rois[i].h;
    }
    config->distance_limit_cm = p->distance_limit_cm;
    config->distance_enabled = (p->distance_enabled == 1);
    config->num_rois = p->num_rois;
    if (config->num_rois == 0 || config->num_rois > RSID_MAX_ROIS)
        config->num_rois = 1;
}

rsid_status rsid_set_device_config(rsid_ctx_t* ctx, const rsid_device_config_t* config)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    rsid_auth_config_payload_t payload;
    rsid_auth_config_payload_t reply_payload;

    if (ctx == NULL || config == NULL)
    {
        RSID_DBG(ctx ? &ctx->platform : NULL, "set-config: invalid args");
        return RSID_Error;
    }
    pkt = ctx_packet(ctx);

    RSID_DBG(&ctx->platform, "set-config");
    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    config_to_payload(config, &payload);
    rsid_packet_init_data(pkt, RSID_MSGID_SET_CONFIG, (const char*)&payload, sizeof(payload));
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    ss = recv_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    /* Device returns Reply on error */
    if (pkt->header.msg_id == RSID_MSGID_REPLY)
        return fa_to_status(&ctx->platform, rsid_packet_get_status_code(pkt));

    if (pkt->header.msg_id != RSID_MSGID_SET_CONFIG)
    {
        RSID_DBG(&ctx->platform, "set-config: unexpected reply");
        return RSID_Error;
    }

    if (pkt->header.payload_size < sizeof(pkt->payload.sequence_number) + sizeof(rsid_auth_config_payload_t))
    {
        RSID_DBG(&ctx->platform, "set-config: payload too small");
        return RSID_Error;
    }

    /* Device echoes the config payload back — verify it matches what we sent */
    memcpy(&reply_payload, pkt->payload.message.data_msg.data, sizeof(reply_payload));
    if (memcmp(&payload, &reply_payload, sizeof(payload)) != 0)
    {
        RSID_DBG(&ctx->platform, "set-config: echo mismatch");
        return RSID_Error;
    }

    return RSID_Ok;
}

rsid_status rsid_query_device_config(rsid_ctx_t* ctx, rsid_device_config_t* config)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;
    rsid_auth_config_payload_t payload;

    if (ctx == NULL || config == NULL)
    {
        RSID_DBG(ctx ? &ctx->platform : NULL, "query-config: invalid args");
        return RSID_Error;
    }
    pkt = ctx_packet(ctx);

    RSID_DBG(&ctx->platform, "query-config");
    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    rsid_packet_init_data(pkt, RSID_MSGID_QUERY_CONFIG, NULL, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    ss = recv_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    /* Device returns Reply on error */
    if (pkt->header.msg_id == RSID_MSGID_REPLY)
        return fa_to_status(&ctx->platform, rsid_packet_get_status_code(pkt));

    if (pkt->header.msg_id != RSID_MSGID_QUERY_CONFIG)
    {
        RSID_DBG(&ctx->platform, "query-config: unexpected reply");
        return RSID_Error;
    }

    if (pkt->header.payload_size < sizeof(pkt->payload.sequence_number) + sizeof(rsid_auth_config_payload_t))
    {
        RSID_DBG(&ctx->platform, "query-config: payload too small");
        return RSID_Error;
    }

    memcpy(&payload, pkt->payload.message.data_msg.data, sizeof(payload));
    payload_to_config(&payload, config);
    return RSID_Ok;
}

rsid_status rsid_standby(rsid_ctx_t* ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;

    if (!validate_ctx(ctx))
        return RSID_Error;

    pkt = ctx_packet(ctx);

    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    rsid_packet_init_data(pkt, RSID_MSGID_STANDBY, NULL, 0);
    ss = send_packet(ctx);
    /* Fire-and-forget — device goes to standby immediately */
    return serial_to_status(ss);
}

rsid_status rsid_unlock(rsid_ctx_t* ctx)
{
    rsid_serial_status ss;
    rsid_serial_packet_t* pkt;

    if (!validate_ctx(ctx))
        return RSID_Error;

    pkt = ctx_packet(ctx);

    ss = start_session(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    rsid_packet_init_fa(pkt, RSID_MSGID_UNLOCK, NULL, 0);
    ss = send_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    ss = recv_packet(ctx);
    if (ss != RSID_SERIAL_OK)
        return serial_to_status(ss);

    if (pkt->header.msg_id != RSID_MSGID_REPLY)
    {
        RSID_DBG(&ctx->platform, "unlock: unexpected reply");
        return RSID_Error;
    }

    return fa_to_status(&ctx->platform, rsid_packet_get_status_code(pkt));
}
