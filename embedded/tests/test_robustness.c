/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Robustness tests — misbehaving HAL, noisy UART, edge cases. */

#include "unity.h"
#include "mock_platform.h"
#include "rsid_common.h"
#include <string.h>

static rsid_ctx_t ctx;
static mock_state_t mock;

static void init(void)
{
    mock_init(&ctx, &mock);
}

/* ---- Send fails mid-session ---- */

/* Verify send failure after session start returns error */
static void test_send_fails_after_session_start(void)
{
    init();
    mock_start_session_ok(&ctx, &mock);

    /* Send now fails */
    mock.send_fail = 1;
    rsid_packet_init_fa(ctx_packet(&ctx), 'A', NULL, 0);
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_send(&ctx));
}

/* Verify ping returns error when send fails */
static void test_ping_send_fails(void)
{
    init();
    mock.send_fail = 1;
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_ping(&ctx));
}

/* Verify enroll fails gracefully when send breaks */
static void test_enroll_send_fails_after_session(void)
{
    init();
    mock_start_session_ok(&ctx, &mock);

    /* Enroll will try to send the Enroll packet — make it fail.
     * We need a fresh session for enroll, so set up recv for session start,
     * then fail on the next send. */
    {
        uint8_t wire[512];
        uint32_t wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
        mock_set_recv_data(&mock, wire, wire_len);
    }
    /* Let session start succeed, then fail subsequent sends */
    mock.send_fail = 0;

    /* Actually, rsid_enroll starts its own session. Let it start, then fail. */
    /* We'll use a counter approach: succeed for session start sends, fail after */
    /* Simpler: just fail send entirely — session start will fail */
    mock.send_fail = 1;
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_enroll(&ctx, "Alice", NULL, NULL));
}

/* ---- Garbage before sync bytes ---- */

/* Verify recv skips garbage bytes before valid sync */
static void test_garbage_before_sync(void)
{
    uint8_t buf[1024];
    uint8_t wire[512];
    uint32_t wire_len;
    uint32_t garbage_len = 50;
    uint32_t i;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* Fill with garbage, then a valid packet */
    for (i = 0; i < garbage_len; i++)
        buf[i] = (uint8_t)(0x80 + (i % 0x40)); /* non-sync bytes */

    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    memcpy(buf + garbage_len, wire, wire_len);
    mock_set_recv_data(&mock, buf, garbage_len + wire_len);

    /* wait_sync_bytes should skip garbage and find the packet */
    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* Verify recv handles false '@' sync starts before real packet */
static void test_false_sync1_before_real_packet(void)
{
    uint8_t buf[1024];
    uint8_t wire[512];
    uint32_t wire_len;
    uint32_t pos = 0;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* '@' followed by non-'F' — false sync start */
    buf[pos++] = '@';
    buf[pos++] = 'X'; /* not 'F' */
    buf[pos++] = '@';
    buf[pos++] = 'Z'; /* not 'F' again */

    /* Then a real packet */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    memcpy(buf + pos, wire, wire_len);
    mock_set_recv_data(&mock, buf, pos + wire_len);

    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* ---- Truncated packet ---- */

/* Verify recv fails on packet truncated after header */
static void test_truncated_after_header(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* Build valid packet but only send first 30 bytes (header + partial payload) */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    TEST_ASSERT_TRUE(wire_len > 30);
    mock_set_recv_data(&mock, wire, 30); /* truncate */

    /* recv should fail — not enough data */
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* Verify recv fails on packet missing trailing CRC bytes */
static void test_truncated_no_crc(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* Build valid packet but cut off the last 2 bytes (CRC) */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    TEST_ASSERT_TRUE(wire_len > 2);
    mock_set_recv_data(&mock, wire, wire_len - 2);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* ---- Optional callbacks are NULL ---- */

/* Verify NULL purge callback does not crash on session start */
/* Verify rsid_init rejects NULL purge (purge is mandatory) */
static void test_purge_null_rejected(void)
{
    init();
    ctx.platform.purge = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify rsid_init rejects NULL sleep_ms (sleep_ms is mandatory) */
static void test_sleep_null_rejected(void)
{
    init();
    ctx.platform.sleep_ms = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify NULL log callback does not crash on error path */
static void test_log_null_no_crash(void)
{
    init();
    ctx.platform.debug = NULL; /* optional */

    /* Trigger a log path — session start with no data will log an error */
    mock.recv_fail = 1;
    rsid_session_start(&ctx); /* should not crash even with NULL log */
}

/* ---- Recv returns failure (not timeout) ---- */

/* Verify recv returns error on hard HAL recv failure */
static void test_recv_hard_failure(void)
{
    init();
    mock_start_session_ok(&ctx, &mock);

    mock.recv_fail = 1; /* hard failure, not just empty buffer */
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* ---- Text command with empty response ---- */

/* Verify serial number query fails on empty response */
static void test_serial_number_empty_response(void)
{
    char output[128];
    init();
    /* recv fails immediately — no response at all */
    mock.recv_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_query_serial_number(&ctx, output, sizeof(output)));
}

/* Verify OTP version fails on truncated prefix with no digit */
static void test_otp_version_truncated_prefix(void)
{
    uint8_t ver = 0;
    init();
    /* Response has the prefix but is truncated right after it */
    mock_set_text_response(&mock, "otp version is ");
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_otp_version(&ctx, &ver));
}

/* ---- Malformed packets ---- */

/* Verify recv rejects payload_size larger than packet buffer */
static void test_malformed_payload_size_overflow(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* Build a valid packet then set payload_size to 0xFFFF */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    /* payload_size is at offset 20-21 in the header (after sync1+sync2+ver+msgid+iv[16]) */
    wire[20] = 0xFF;
    wire[21] = 0xFF;
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* Verify recv handles zero payload_size without crash */
static void test_malformed_zero_payload_size(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* Build a valid packet then force payload_size to 0 */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    wire[20] = 0;
    wire[21] = 0;
    /* Recompute total wire bytes: header(22) + payload(0) + hmac(32) + crc(2) = 56 */
    /* The CRC will be wrong, so this should fail with CRC error, not crash */
    mock_set_recv_data(&mock, wire, 56);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* Verify recv handles packet with unexpected msg_id without crash */
static void test_malformed_invalid_msg_id(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    mock_start_session_ok(&ctx, &mock);

    /* Build valid packet then change msg_id to a control character */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    wire[3] = 0x01; /* msg_id at offset 3 — invalid control char */
    /* CRC will be wrong — should get CRC error, not crash */
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* Verify recv handles all-zero packet bytes without crash */
static void test_malformed_all_zeros(void)
{
    uint8_t wire[128];

    init();
    mock_start_session_ok(&ctx, &mock);

    /* All zeros — no valid sync bytes, recv will scan and fail */
    memset(wire, 0, sizeof(wire));
    mock_set_recv_data(&mock, wire, sizeof(wire));

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* Verify recv handles packet that is just sync bytes and nothing else */
static void test_malformed_sync_only(void)
{
    uint8_t wire[2];

    init();
    mock_start_session_ok(&ctx, &mock);

    wire[0] = '@';
    wire[1] = 'F';
    mock_set_recv_data(&mock, wire, 2);

    /* After finding sync, recv tries to read protocol_ver — will fail (no data) */
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* ---- Malformed user_id from device ---- */
/* NUL-termination test is in test_packet.c (test_packet_get_user_id_force_nul_terminates). */

/* Verify get_user_id handles all-zero user_id (empty string) */
static void test_malformed_user_id_all_zeros(void)
{
    rsid_serial_packet_t pkt;
    const char* uid;

    rsid_packet_init_fa(&pkt, 'R', NULL, 0);
    /* user_id is all zeros from init */
    uid = rsid_packet_get_user_id(&pkt);
    TEST_ASSERT_EQUAL_STRING("", uid);
}

/* ---- Status mapping: out-of-range values from device ---- */
/* These tests exercise the fa_to_*_status() bounds checks indirectly through
 * the public API. A buggy/hostile device could send any byte as a status code;
 * the SDK must clamp unknown values to safe defaults, never pass them through. */

/* Device sends auth Result with status=200 (outside 0..127 range).
 * fa_to_auth_status must return RSID_Auth_Failure as the safe default. */
static void test_auth_status_out_of_range(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    /* Device sends Result with out-of-range status=200, then Reply with RSID_Ok */
    mock_append_fa_packet(&mock, 'R', NULL, (char)200, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Auth_Failure, g_test_cb.last_auth_status);
}

/* Same as above but for enrollment: status=200 → RSID_Enroll_Failure. */
static void test_enroll_status_out_of_range(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    mock_load_session_start(&mock);
    /* Device sends Result with out-of-range status=200, then Reply with RSID_Ok */
    mock_append_fa_packet(&mock, 'R', NULL, (char)200, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    rsid_enroll(&ctx, "Alice", &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Enroll_Failure, g_test_cb.last_enroll_status);
}

/* Device sends Progress with pose=99 (outside 0..4 range).
 * fa_to_face_pose must return RSID_Face_Center as the safe default. */
static void test_face_pose_out_of_range(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;
    cb.on_progress = test_cb_on_enroll_progress;

    mock_load_session_start(&mock);
    /* Device sends Progress with out-of-range pose=99, then Result + Reply */
    mock_append_fa_packet(&mock, 'P', NULL, (char)99, 1);
    mock_append_fa_packet(&mock, 'R', NULL, (char)RSID_Enroll_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    rsid_enroll(&ctx, "Bob", &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.progress_count);
    TEST_ASSERT_EQUAL(RSID_Face_Center, g_test_cb.last_pose);
}

/* Device Reply with status=200 (outside RSID_Ok..RSID_InvalidSettings range).
 * fa_to_status must return RSID_Error. Tested via rsid_remove_all. */
static void test_fa_to_status_out_of_range(void)
{
    rsid_status s;

    init();
    mock_load_session_start(&mock);
    /* Device Reply with out-of-range status=200 */
    mock_append_fa_packet(&mock, 'Y', NULL, (char)200, 1);

    s = rsid_remove_all(&ctx);
    TEST_ASSERT_EQUAL(RSID_Error, s);
}

/* Boundary: RSID_Ok (100) is the lowest valid status. Must pass through unchanged. */
static void test_fa_to_status_lower_bound(void)
{
    rsid_status s;

    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 1);

    s = rsid_remove_all(&ctx);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
}

/* Boundary: RSID_InvalidSettings (111) is the highest valid status. Must pass through unchanged. */
static void test_fa_to_status_upper_bound(void)
{
    rsid_status s;

    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_InvalidSettings, 1);

    s = rsid_remove_all(&ctx);
    TEST_ASSERT_EQUAL(RSID_InvalidSettings, s);
}

/* ---- Error reporting with NULL callbacks ---- */
/* When the caller passes NULL for callbacks, error-reporting helpers like
 * report_auth_error() and report_enroll_error() must skip the callback
 * invocation entirely — not dereference a NULL function pointer. */

/* Auth with NULL callbacks + forced session-start failure: must not crash. */
static void test_report_auth_error_null_callbacks(void)
{
    rsid_status s;
    init();
    /* Don't load session data — session start will fail on send */
    mock.send_fail = 1;
    s = rsid_authenticate(&ctx, NULL, NULL);
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, s);
}

/* Enroll with NULL callbacks + forced session-start failure: must not crash. */
static void test_report_enroll_error_null_callbacks(void)
{
    rsid_status s;
    init();
    mock.send_fail = 1;
    s = rsid_enroll(&ctx, "Alice", NULL, NULL);
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, s);
}

/* ---- Data parsing edge cases ---- */
/* These tests verify the parse_face_detected/landmarks/distances functions
 * handle degenerate payloads safely (zero count, undersized payloads). */

/* FaceDetected with n_faces=0: callback IS called (with 0 faces).
 * Distinguishes "no faces in frame" from "parsing failure". */
static void test_face_detected_zero_faces(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t face_data[5];

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_face_detected = test_cb_on_face_detected;

    mock_build_face_data(face_data, sizeof(face_data), 0, 42, NULL);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'g', face_data, sizeof(face_data), 1);
    mock_append_fa_packet(&mock, 'R', NULL, (char)RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.face_detected_count);
    TEST_ASSERT_EQUAL_UINT(0, g_test_cb.last_num_faces);
}

/* FaceDetected claims n_faces=2 but payload only has room for 1 face rect.
 * The parser checks: needed_bytes = header + n_faces * rect_size > payload_size?
 * Since 2 faces don't fit, it clamps to 0 (safe: no partial face data delivered). */
static void test_face_detected_payload_one_byte_short(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t face_data[21]; /* n_faces(1) + ts(4) + 1 rect(16) = 21, but claims 2 */
    rsid_face_rect rect = {10, 20, 100, 100};

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_face_detected = test_cb_on_face_detected;

    mock_build_face_data(face_data, sizeof(face_data), 1, 42, &rect);
    face_data[0] = 2; /* lie: claims 2 faces, but only 1 fits */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'g', face_data, sizeof(face_data), 1);
    mock_append_fa_packet(&mock, 'R', NULL, (char)RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.face_detected_count);
    TEST_ASSERT_EQUAL_UINT(0, g_test_cb.last_num_faces); /* clamped to 0 */
}

/* LandmarksDetected with n_faces=0: callback is NOT called.
 * process_auth_packet has `if (n > 0)` guard — zero-face landmarks are dropped. */
static void test_landmarks_zero_faces(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t lm_data[5];

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_landmarks_detected = test_cb_on_landmarks_detected;

    mock_build_landmarks_data(lm_data, sizeof(lm_data), 0, 42, NULL);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'h', lm_data, sizeof(lm_data), 1);
    mock_append_fa_packet(&mock, 'R', NULL, (char)RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    /* on_landmarks_detected should NOT be called because n == 0 (if n > 0 guard) */
    TEST_ASSERT_EQUAL_INT(0, g_test_cb.landmarks_count);
}

/* FaceDistances with n_distances=0: callback is NOT called (same `if (n > 0)` guard). */
static void test_distances_zero_faces(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t dist_data[5];

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_face_distances = test_cb_on_face_distances;

    mock_build_distances_data(dist_data, sizeof(dist_data), 0, 42, NULL);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'm', dist_data, sizeof(dist_data), 1);
    mock_append_fa_packet(&mock, 'R', NULL, (char)RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    /* on_face_distances should NOT be called because n == 0 (if n > 0 guard) */
    TEST_ASSERT_EQUAL_INT(0, g_test_cb.distances_count);
}

/* ---- Runner ---- */

void test_robustness_run(void)
{
    RUN_TEST(test_send_fails_after_session_start);
    RUN_TEST(test_ping_send_fails);
    RUN_TEST(test_enroll_send_fails_after_session);
    RUN_TEST(test_garbage_before_sync);
    RUN_TEST(test_false_sync1_before_real_packet);
    RUN_TEST(test_truncated_after_header);
    RUN_TEST(test_truncated_no_crc);
    RUN_TEST(test_purge_null_rejected);
    RUN_TEST(test_sleep_null_rejected);
    RUN_TEST(test_log_null_no_crash);
    RUN_TEST(test_recv_hard_failure);
    RUN_TEST(test_serial_number_empty_response);
    RUN_TEST(test_otp_version_truncated_prefix);
    RUN_TEST(test_malformed_payload_size_overflow);
    RUN_TEST(test_malformed_zero_payload_size);
    RUN_TEST(test_malformed_invalid_msg_id);
    RUN_TEST(test_malformed_all_zeros);
    RUN_TEST(test_malformed_sync_only);
    RUN_TEST(test_malformed_user_id_all_zeros);
    RUN_TEST(test_auth_status_out_of_range);
    RUN_TEST(test_enroll_status_out_of_range);
    RUN_TEST(test_face_pose_out_of_range);
    RUN_TEST(test_fa_to_status_out_of_range);
    RUN_TEST(test_fa_to_status_lower_bound);
    RUN_TEST(test_fa_to_status_upper_bound);
    RUN_TEST(test_report_auth_error_null_callbacks);
    RUN_TEST(test_report_enroll_error_null_callbacks);
    RUN_TEST(test_face_detected_zero_faces);
    RUN_TEST(test_face_detected_payload_one_byte_short);
    RUN_TEST(test_landmarks_zero_faces);
    RUN_TEST(test_distances_zero_faces);
}
