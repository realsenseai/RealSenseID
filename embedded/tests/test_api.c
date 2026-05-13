/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* End-to-end API tests — enroll, authenticate, user mgmt, config, device control. */

#include "unity.h"
#include "mock_platform.h"
#include <string.h>

static rsid_ctx_t ctx;
static mock_state_t mock;

static void init(void)
{
    mock_init(&ctx, &mock);
}


/* ---- Enroll ---- */

/* Verify successful enroll flow with progress and result callbacks */
static void test_enroll_success(void)
{
    rsid_enroll_callbacks_t cb;
    rsid_status s;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;
    cb.on_progress = test_cb_on_enroll_progress;
    cb.on_hint = test_cb_on_enroll_hint;

    mock_load_session_start(&mock);
    /* Device sends: Progress (look center), Result (success), Reply (ok) */
    mock_append_fa_packet(&mock, 'P', NULL, RSID_Face_Center, 1);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_enroll(&ctx, "Alice", &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.progress_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Enroll_Success, g_test_cb.last_enroll_status);
    TEST_ASSERT_EQUAL(RSID_Face_Center, g_test_cb.last_pose);
}

/* Verify enroll failure reported via callback */
static void test_enroll_failure(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_Failure, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Error, 2);

    rsid_enroll(&ctx, "Bob", &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Enroll_Failure, g_test_cb.last_enroll_status);
}

/* ---- Authenticate ---- */

/* Verify successful authenticate with result callback */
static void test_authenticate_success(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_hint = test_cb_on_auth_hint;

    mock_load_session_start(&mock);
    /* Device sends: Hint, Result (success, "Alice", score encoded in reserved), Reply */
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 1);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.hint_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Auth_Success, g_test_cb.last_auth_status);
    TEST_ASSERT_EQUAL_STRING("Alice", g_test_cb.last_user_id);
}

/* Verify authenticate with no face detected */
static void test_authenticate_no_face(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Auth_NoFaceDetected, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL(RSID_Auth_NoFaceDetected, g_test_cb.last_auth_status);
}

/* Verify auth correctly handles FaceDetected (data packet) interleaved with FA packets.
 * Regression: rsid_packet_get_status_code was called on 'g' data packets, reading
 * garbage from the fa_msg union and logging "unknown auth status". */
static void test_authenticate_face_detected_interleaved(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t face_data[21];
    rsid_face_rect rect = {10, 20, 100, 100};

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_hint = test_cb_on_auth_hint;
    cb.on_face_detected = test_cb_on_face_detected;

    mock_build_face_data(face_data, sizeof(face_data), 1, 42, &rect);

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 1);
    mock_append_wire_packet(&mock, 'g', face_data, sizeof(face_data), 2);
    mock_append_fa_packet(&mock, 'R', "Bob", RSID_Auth_Success, 3);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 4);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.face_detected_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.hint_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Auth_Success, g_test_cb.last_auth_status);
    TEST_ASSERT_EQUAL_STRING("Bob", g_test_cb.last_user_id);
}

/* Verify 5 face rects are parsed and delivered correctly */
static void test_authenticate_face_detected_5_faces(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t face_data[85];
    rsid_face_rect rects[5];
    unsigned int i;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_face_detected = test_cb_on_face_detected;

    /* Build 5 faces: face[i] at (i*10, i*20, 50+i, 60+i), ts=99 */
    for (i = 0; i < 5; i++)
    {
        rects[i].x = i * 10;
        rects[i].y = i * 20;
        rects[i].w = 50 + i;
        rects[i].h = 60 + i;
    }
    mock_build_face_data(face_data, sizeof(face_data), 5, 99, rects);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'g', face_data, sizeof(face_data), 1);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.face_detected_count);
    TEST_ASSERT_EQUAL_UINT(5, g_test_cb.last_num_faces);
    TEST_ASSERT_EQUAL_UINT(99, g_test_cb.last_face_ts);

    for (i = 0; i < 5; i++)
    {
        TEST_ASSERT_EQUAL_UINT32(i * 10, g_test_cb.last_faces[i].x);
        TEST_ASSERT_EQUAL_UINT32(i * 20, g_test_cb.last_faces[i].y);
        TEST_ASSERT_EQUAL_UINT32(50 + i, g_test_cb.last_faces[i].w);
        TEST_ASSERT_EQUAL_UINT32(60 + i, g_test_cb.last_faces[i].h);
    }
}

/* Verify landmarks are parsed and delivered correctly (3 faces) */
static void test_authenticate_landmarks_detected(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t lm_data[125];
    rsid_face_landmarks lms[3];
    unsigned int i, j;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_landmarks_detected = test_cb_on_landmarks_detected;

    /* Build 3 faces of landmarks */
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < RSID_NUM_FACE_LANDMARKS; j++)
        {
            lms[i].lm_x[j] = i * 100 + j;
            lms[i].lm_y[j] = i * 100 + j + 50;
        }
    }
    mock_build_landmarks_data(lm_data, sizeof(lm_data), 3, 77, lms);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'h', lm_data, sizeof(lm_data), 1);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.landmarks_count);
    TEST_ASSERT_EQUAL_UINT(3, g_test_cb.last_num_faces);
    TEST_ASSERT_EQUAL_UINT(77, g_test_cb.last_face_ts);
    for (i = 0; i < 3; i++)
    {
        for (j = 0; j < RSID_NUM_FACE_LANDMARKS; j++)
        {
            TEST_ASSERT_EQUAL_UINT32(i * 100 + j, g_test_cb.last_landmarks[i].lm_x[j]);
            TEST_ASSERT_EQUAL_UINT32(i * 100 + j + 50, g_test_cb.last_landmarks[i].lm_y[j]);
        }
    }
}

/* Verify face distances are parsed and delivered correctly */
static void test_authenticate_face_distances(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;
    uint8_t dist_data[29];
    const double dists[3] = {42.5, 100.0, 75.25};
    double d;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_face_distances = test_cb_on_face_distances;

    mock_build_distances_data(dist_data, sizeof(dist_data), 3, 55, dists);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'm', dist_data, sizeof(dist_data), 1);
    mock_append_fa_packet(&mock, 'R', "Bob", RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    s = rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.distances_count);
    TEST_ASSERT_EQUAL_UINT(3, g_test_cb.last_num_faces);
    TEST_ASSERT_EQUAL_UINT(55, g_test_cb.last_face_ts);
    /* Compare raw bytes — avoids needing Unity double support */
    d = 42.5;
    TEST_ASSERT_EQUAL_MEMORY(&d, &g_test_cb.last_distances[0], sizeof(double));
    d = 100.0;
    TEST_ASSERT_EQUAL_MEMORY(&d, &g_test_cb.last_distances[1], sizeof(double));
    d = 75.25;
    TEST_ASSERT_EQUAL_MEMORY(&d, &g_test_cb.last_distances[2], sizeof(double));
}

/* Verify authenticate with NULL callbacks doesn't crash */
static void test_authenticate_null_callbacks(void)
{
    rsid_status s;

    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Auth_Success, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    s = rsid_authenticate(&ctx, NULL, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
}

/* ---- Authenticate loop ---- */

/* Verify authenticate_loop terminates cleanly on Reply('Y').
 * The loop processes Result, then Reply returns the final status. */
static void test_authenticate_loop_cancel(void)
{
    rsid_auth_callbacks_t cb;
    rsid_status s;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    s = rsid_authenticate_loop(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
}

/* ---- Remove all ---- */

/* Verify remove_all succeeds with Reply(Ok) */
static void test_remove_all_success(void)
{
    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_remove_all(&ctx));
}

/* Verify remove_all returns device error status */
static void test_remove_all_error(void)
{
    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Error, 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_remove_all(&ctx));
}

/* ---- Query number of users ---- */

/* Verify query_number_of_users returns correct count */
static void test_query_number_of_users_success(void)
{
    unsigned int count = 0;
    uint32_t num = 5;

    init();
    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'n', &num, sizeof(num), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_number_of_users(&ctx, &count));
    TEST_ASSERT_EQUAL_UINT(5, count);
}

/* Verify query_number_of_users returns zero */
static void test_query_number_of_users_zero(void)
{
    unsigned int count = 99;
    uint32_t num = 0;

    init();
    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'n', &num, sizeof(num), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_number_of_users(&ctx, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
}

/* ---- Query user IDs ---- */
/* Wire format for GetUserIds response: [arrived:u32][id1\0][id2\0]...
 * Each chunk is a separate session. Chunks of up to 50 users.
 * End of list signaled by arrived=0. */

/* Verify query_user_ids retrieves packed user ID strings */
static void test_query_user_ids_single(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 10;
    uint8_t payload[128];
    uint32_t arrived;
    uint32_t pos;

    init();

    /* First chunk session: returns 2 users */
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */

    arrived = 2;
    pos = 0;
    memset(payload, 0, sizeof(payload));
    memcpy(payload + pos, &arrived, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(payload + pos, "Alice", 6); /* 5 chars + NUL */
    pos += 6;
    memcpy(payload + pos, "Bob", 4); /* 3 chars + NUL */
    pos += 4;
    mock_append_wire_packet(&mock, 'u', payload, pos, 1);

    /* Second chunk session: returns 0 users (end of list) */
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */
    arrived = 0;
    mock_append_wire_packet(&mock, 'u', &arrived, sizeof(arrived), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_user_ids(&ctx, users, &count));
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("Alice", users[0]);
    TEST_ASSERT_EQUAL_STRING("Bob", users[1]);
}

/* Verify query_user_ids returns empty list */
static void test_query_user_ids_empty(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 10;
    uint32_t arrived = 0;

    init();
    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'u', &arrived, sizeof(arrived), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_user_ids(&ctx, users, &count));
    TEST_ASSERT_EQUAL_UINT(0, count);
}

/* ---- Device config ---- */

/* Verify set_device_config returns error when device returns mismatched payload */
static void test_set_device_config_echo_mismatch(void)
{
    rsid_device_config_t config;
    uint8_t bad_payload[60];

    init();
    memset(&config, 0, sizeof(config));
    config.camera_rotation = RSID_Rotation_0_Deg;
    config.security_level = RSID_SecLevel_High;
    config.num_rois = 1;

    /* Device returns different payload — should fail verification */
    memset(bad_payload, 0xFF, sizeof(bad_payload));
    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 's', bad_payload, sizeof(bad_payload), 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_device_config(&ctx, &config));
}

/* Verify set_device_config handles device error Reply */
static void test_set_device_config_device_error(void)
{
    rsid_device_config_t config;

    init();
    memset(&config, 0, sizeof(config));
    config.num_rois = 1;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_InvalidSettings, 1);

    TEST_ASSERT_EQUAL(RSID_InvalidSettings, rsid_set_device_config(&ctx, &config));
}

/* Verify query_device_config parses response */
static void test_query_device_config_success(void)
{
    rsid_device_config_t config;
    uint8_t config_payload[60];

    init();
    memset(&config, 0, sizeof(config));
    memset(config_payload, 0, sizeof(config_payload));

    /* Set some known values in the wire payload */
    config_payload[0] = 1;    /* camera_rotation = 180 */
    config_payload[1] = 2;    /* security_level = Low */
    config_payload[3] = 0x0b; /* gpio_auth_toggling = enabled */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'q', config_payload, sizeof(config_payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_device_config(&ctx, &config));
    TEST_ASSERT_EQUAL(RSID_Rotation_180_Deg, config.camera_rotation);
    TEST_ASSERT_EQUAL(RSID_SecLevel_Low, config.security_level);
    TEST_ASSERT_EQUAL_INT(1, config.gpio_auth_toggling);
}

/* Verify query_device_config handles device error Reply */
static void test_query_device_config_error(void)
{
    rsid_device_config_t config;

    init();
    memset(&config, 0, sizeof(config));

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Error, 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_device_config(&ctx, &config));
}

/* ---- Standby / Unlock ---- */

/* Verify standby is fire-and-forget (no recv expected) */
static void test_standby_success(void)
{
    init();
    mock_load_session_start(&mock);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_standby(&ctx));
}

/* Verify unlock succeeds with Reply(Ok) */
static void test_unlock_success(void)
{
    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_unlock(&ctx));
}

/* ---- Reboot / Hibernate ---- */

/* Verify reboot sends text command without crash */
static void test_reboot_success(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_reboot(&ctx));
    TEST_ASSERT_TRUE(mock.send_len > 0);
}

/* Verify hibernate sends text command without crash */
static void test_hibernate_success(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_hibernate(&ctx));
    TEST_ASSERT_TRUE(mock.send_len > 0);
}

/* Verify reboot fails on send error */
static void test_reboot_send_fails(void)
{
    init();
    mock.send_fail = 1;
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_reboot(&ctx));
}

/* Verify hibernate fails on send error */
static void test_hibernate_send_fails(void)
{
    init();
    mock.send_fail = 1;
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_hibernate(&ctx));
}

/* ---- Remove user ---- */

/* Verify remove_user succeeds with Reply(Ok) */
static void test_remove_user_success(void)
{
    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_remove_user(&ctx, "Alice"));
}

/* Verify remove_user returns error on unexpected msg_id */
static void test_remove_user_unexpected_reply(void)
{
    init();
    mock_load_session_start(&mock);
    /* Device sends Result('R') instead of Reply('Y') */
    mock_append_fa_packet(&mock, 'R', NULL, 0, 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_remove_user(&ctx, "Alice"));
}

/* ---- Cancel ---- */

/* Verify rsid_cancel sets flag AND sends __FACE_CANCEL__ to device */
static void test_cancel_sets_flag(void)
{
    init();
    mock.send_len = 0;
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_cancel(&ctx));
    TEST_ASSERT_EQUAL_UINT8(1, ctx._internal.cancel_requested);
    /* __FACE_CANCEL__ must be sent immediately — not deferred */
    TEST_ASSERT_TRUE(mock.send_len > 0);
    TEST_ASSERT_NOT_NULL(strstr((const char*)mock.send_buf, "__FACE_CANCEL__"));
}

/* ---- BSP version ---- */

/* Verify bsp_version returns raw unprocessed text */
static void test_bsp_version_success(void)
{
    char output[256];
    const char* response = "OPFW : 4.2.0.1\r\nNNFW : 3.1.0.0\r\n";

    init();
    mock_set_recv_data(&mock, (const uint8_t*)response, (uint32_t)strlen(response));

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_bsp_version(&ctx, output, sizeof(output)));
    /* Raw text, not parsed like query_firmware_version */
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW : 4.2.0.1"));
}

/* Verify bsp_version truncates output to buffer size */
static void test_bsp_version_truncated(void)
{
    char output[10];
    const char* response = "OPFW : 4.2.0.1\r\nNNFW : 3.1.0.0\r\n";

    init();
    mock_set_recv_data(&mock, (const uint8_t*)response, (uint32_t)strlen(response));

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_bsp_version(&ctx, output, sizeof(output)));
    TEST_ASSERT_TRUE(strlen(output) < 10);
    TEST_ASSERT_EQUAL_CHAR('\0', output[9]); /* NUL terminated within buffer */
}

/* ---- Color gains set ---- */

/* Verify set_color_gains succeeds with valid values */
static void test_set_color_gains_success(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_color_gains(&ctx, 128, 256));
    TEST_ASSERT_TRUE(mock.send_len > 0);
}

/* Verify set_color_gains rejects out-of-range values */
static void test_set_color_gains_boundary(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_color_gains(&ctx, -1, 256));

    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_color_gains(&ctx, 512, 256));

    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_color_gains(&ctx, 128, -1));

    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_color_gains(&ctx, 128, 512));

    /* Boundary values that should succeed */
    init();
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_color_gains(&ctx, 0, 0));

    init();
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_color_gains(&ctx, 511, 511));
}

/* Verify set_color_gains fails on send error */
static void test_set_color_gains_send_fails(void)
{
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_set_color_gains(&ctx, 100, 100));
}

/* ---- Timeouts ---- */

/* Auto-incrementing timer: advances 7s per call.
 * Session start succeeds (data available → few calls).
 * Enroll/auth recv loop timeout check fires after ~2 iterations. */
static uint32_t auto_inc_ms;
static uint32_t auto_inc_get_time(void* app_ctx)
{
    (void)app_ctx;
    auto_inc_ms += 500;
    return auto_inc_ms;
}

/* Verify enroll times out and calls on_result with Failure */
static void test_enroll_timeout(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();
    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    auto_inc_ms = 0;
    ctx.platform.get_time_ms = auto_inc_get_time;

    mock_load_session_start(&mock);
    /* Multiple Hints so recv loop iterates until timeout fires */
    {
        int i;
        for (i = 1; i <= 10; i++)
            mock_append_fa_packet(&mock, 'H', NULL, RSID_Enroll_FaceDetected, (uint32_t)i);
    }

    rsid_enroll(&ctx, "Alice", &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Enroll_Failure, g_test_cb.last_enroll_status);
}

/* Verify authenticate times out and calls on_result with Forbidden */
static void test_authenticate_timeout(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();
    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    auto_inc_ms = 0;
    ctx.platform.get_time_ms = auto_inc_get_time;

    mock_load_session_start(&mock);
    {
        int i;
        for (i = 1; i <= 10; i++)
            mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, (uint32_t)i);
    }

    rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL(RSID_Auth_Forbidden, g_test_cb.last_auth_status);
}

/* ---- Recv failures ---- */

/* Verify enroll calls on_result on recv error (no device response) */
static void test_enroll_recv_error(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();
    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    mock_load_session_start(&mock);

    rsid_enroll(&ctx, "Alice", &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_enroll_status >= RSID_Enroll_Serial_Ok);
}

/* Verify authenticate calls on_result on recv error */
static void test_authenticate_recv_error(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();
    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);

    rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_auth_status >= RSID_Auth_Serial_Ok);
}

/* Verify authenticate_loop calls on_result on recv error */
static void test_authenticate_loop_recv_error(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();
    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);

    rsid_authenticate_loop(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_auth_status >= RSID_Auth_Serial_Ok);
}

/* ---- Query user IDs malformed ---- */

/* Verify query_user_ids rejects user ID without NUL terminator */
static void test_query_user_ids_missing_nul(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 10;
    /* Build payload: arrived=1, then fill remaining data with 'X' (no NUL) */
    uint8_t payload[20];
    uint32_t arrived = 1;

    init();
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */

    memset(payload, 'X', sizeof(payload));
    memcpy(payload, &arrived, sizeof(uint32_t)); /* first 4 bytes = count */
    /* bytes 4..19 = all 'X', no NUL anywhere */
    mock_append_wire_packet(&mock, 'u', payload, sizeof(payload), 1);

    /* The function should detect id_len == remaining (no NUL) and return error */
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_query_user_ids(&ctx, users, &count));
}

/* Verify query_user_ids rejects user ID longer than RSID_MAX_USER_ID */
static void test_query_user_ids_id_too_long(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 10;
    uint8_t payload[128];
    uint32_t arrived = 1;
    uint32_t pos = 0;

    init();
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0);

    /* Pack arrived=1 then 31 bytes of 'X' + NUL (exceeds RSID_MAX_USER_ID=30) */
    memset(payload, 0, sizeof(payload));
    memcpy(payload, &arrived, sizeof(uint32_t));
    pos = sizeof(uint32_t);
    memset(payload + pos, 'X', RSID_MAX_USER_ID + 1);
    pos += RSID_MAX_USER_ID + 1;
    payload[pos] = '\0';
    pos++;
    mock_append_wire_packet(&mock, 'u', payload, pos, 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_user_ids(&ctx, users, &count));
}

/* ---- Config round-trip ---- */
/* The device config wire format is a 60-byte packed struct (rsid_auth_config_payload_t).
 * set_device_config sends it and verifies the device returns the exact same bytes.
 *
 * Wire payload byte offsets (must match C++ AuthConfigPayload):
 *   0:  camera_rotation    1:  security_level     2:  algo_flow
 *   3:  gpio_auth_toggling (0x0b=on, 0x00=off)    4:  dump_mode
 *   5:  frontal_face_policy  6: person_motion_mode  7: max_spoofs
 *   8:  match_thresh (u16)  10: face_selection_policy
 *  11:  manual_exposure (u16)  13: manual_gain (u16)
 *  15:  rect_enable  16: landmarks_enable
 *  17-56: detection_rois[5] (5 * {x,y,w,h} as u16 = 8 bytes each = 40 bytes)
 *  57:  distance_limit_cm (u8, 0 = unlimited, 1..150 = cm)
 *  58:  distance_enabled  59: num_rois
 */

/* Verify set_device_config succeeds when device returns the matching payload.
 * Default config (all zero + num_rois=1) produces a wire payload with byte[59]=1. */
static void test_set_device_config_success(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];

    init();
    memset(&config, 0, sizeof(config));
    config.camera_rotation = RSID_Rotation_0_Deg;
    config.security_level = RSID_SecLevel_High;
    config.num_rois = 1;

    /* Build matching 60-byte wire payload: all zero except num_rois at offset 59 = 1 */
    memset(payload, 0, sizeof(payload));
    payload[59] = 1; /* num_rois */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 's', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_device_config(&ctx, &config));
}

/* Verify every config field survives the encode→send→verify round-trip.
 * Sets all fields to non-default values and builds the matching 60-byte wire payload. */
static void test_set_device_config_all_fields(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];
    uint16_t val16;

    init();
    memset(&config, 0, sizeof(config));
    config.camera_rotation = RSID_Rotation_180_Deg;
    config.security_level = RSID_SecLevel_Low;
    config.algo_mode = RSID_AlgoMode_SpoofOnly;
    config.gpio_auth_toggling = 1;
    config.dump_mode = RSID_DumpCroppedFace;
    config.frontal_face_policy = RSID_FacePolicy_Strict;
    config.person_motion_mode = RSID_PersonMotionMode_Walkthrough;
    config.max_spoofs = 3;
    config.match_thresh = 500;
    config.face_selection_policy = RSID_FaceSelection_All;
    config.manual_exposure_time_us = 1000;
    config.manual_gain = 200;
    config.rect_enable = 1;
    config.landmarks_enable = 1;
    config.distance_limit_cm = 100;
    config.distance_enabled = 1;
    config.num_rois = 1;

    /* Build matching wire payload */
    memset(payload, 0, sizeof(payload));
    payload[0] = 1;    /* camera_rotation = 180 */
    payload[1] = 2;    /* security_level = Low */
    payload[2] = 1;    /* algo_flow = SpoofOnly */
    payload[3] = 0x0b; /* gpio = enabled */
    payload[4] = 1;    /* dump_mode = CroppedFace */
    payload[5] = 2;    /* frontal = Strict */
    payload[6] = 1;    /* person_motion = Walkthrough */
    payload[7] = 3;    /* max_spoofs */
    val16 = 500;
    memcpy(payload + 8, &val16, 2); /* match_thresh */
    payload[10] = 1;                /* face_selection = All */
    val16 = 1000;
    memcpy(payload + 11, &val16, 2); /* manual_exposure */
    val16 = 200;
    memcpy(payload + 13, &val16, 2); /* manual_gain */
    payload[15] = 1;                 /* rect_enable */
    payload[16] = 1;                 /* landmarks_enable */
    /* rois[17..56] all zero */
    payload[57] = 100; /* distance_limit_cm = 100 cm */
    payload[58] = 1;   /* distance_enabled */
    payload[59] = 1;   /* num_rois */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 's', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_device_config(&ctx, &config));
}

/* Verify the gpio_auth_toggling special encoding: host value 1 → wire byte 0x0b,
 * and wire byte 0x0b → host value 1. Not a simple bool — matches C++ AuthConfigPayload. */
static void test_config_roundtrip_gpio_encoding(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];

    /* Part 1: Set config with gpio=1, verify it succeeds */
    init();
    memset(&config, 0, sizeof(config));
    config.gpio_auth_toggling = 1;
    config.num_rois = 1;

    memset(payload, 0, sizeof(payload));
    payload[3] = 0x0b; /* gpio wire encoding */
    payload[59] = 1;   /* num_rois */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 's', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_device_config(&ctx, &config));

    /* Part 2: Query config with gpio=0x0b, verify decoded as 1 */
    init();
    memset(&config, 0, sizeof(config));
    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'q', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_device_config(&ctx, &config));
    TEST_ASSERT_EQUAL_INT(1, config.gpio_auth_toggling);
}

/* Verify config round-trip with 3 ROIs */
static void test_config_roundtrip_rois(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];
    uint16_t val16;

    init();
    memset(&config, 0, sizeof(config));
    config.num_rois = 3;
    config.rois[0].x = 10;
    config.rois[0].y = 20;
    config.rois[0].width = 100;
    config.rois[0].height = 200;
    config.rois[1].x = 30;
    config.rois[1].y = 40;
    config.rois[1].width = 150;
    config.rois[1].height = 250;
    config.rois[2].x = 50;
    config.rois[2].y = 60;
    config.rois[2].width = 170;
    config.rois[2].height = 280;

    /* Build matching wire payload with ROIs at offset 17 */
    memset(payload, 0, sizeof(payload));
    /* ROI 0 */
    val16 = 10;
    memcpy(payload + 17, &val16, 2);
    val16 = 20;
    memcpy(payload + 19, &val16, 2);
    val16 = 100;
    memcpy(payload + 21, &val16, 2);
    val16 = 200;
    memcpy(payload + 23, &val16, 2);
    /* ROI 1 */
    val16 = 30;
    memcpy(payload + 25, &val16, 2);
    val16 = 40;
    memcpy(payload + 27, &val16, 2);
    val16 = 150;
    memcpy(payload + 29, &val16, 2);
    val16 = 250;
    memcpy(payload + 31, &val16, 2);
    /* ROI 2 */
    val16 = 50;
    memcpy(payload + 33, &val16, 2);
    val16 = 60;
    memcpy(payload + 35, &val16, 2);
    val16 = 170;
    memcpy(payload + 37, &val16, 2);
    val16 = 280;
    memcpy(payload + 39, &val16, 2);

    payload[59] = 3; /* num_rois */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 's', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_set_device_config(&ctx, &config));
}

/* Verify payload_to_config clamps num_rois=0 to 1 (at least 1 ROI required). */
static void test_config_num_rois_zero_sanitized(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];

    init();
    memset(&config, 0, sizeof(config));
    memset(payload, 0, sizeof(payload));
    payload[59] = 0; /* num_rois = 0 */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'q', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_device_config(&ctx, &config));
    TEST_ASSERT_EQUAL_UINT(1, config.num_rois);
}

/* Verify payload_to_config clamps num_rois=6 to 1 (max is RSID_MAX_ROIS=5). */
static void test_config_num_rois_overflow_sanitized(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];

    init();
    memset(&config, 0, sizeof(config));
    memset(payload, 0, sizeof(payload));
    payload[59] = 6; /* num_rois = 6, exceeds RSID_MAX_ROIS */

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'q', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_device_config(&ctx, &config));
    TEST_ASSERT_EQUAL_UINT(1, config.num_rois);
}

/* Verify set_device_config fails when returned payload is too small */
static void test_set_device_config_payload_too_small(void)
{
    rsid_device_config_t config;
    uint8_t small_payload[10];

    init();
    memset(&config, 0, sizeof(config));
    config.num_rois = 1;

    memset(small_payload, 0, sizeof(small_payload));

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 's', small_payload, sizeof(small_payload), 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_device_config(&ctx, &config));
}

/* Verify set_device_config fails on unexpected msg_id */
static void test_set_device_config_unexpected_msg_id(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];

    init();
    memset(&config, 0, sizeof(config));
    config.num_rois = 1;

    memset(payload, 0, sizeof(payload));
    payload[59] = 1;

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'z', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_device_config(&ctx, &config));
}

/* Verify set_device_config with NULL config returns error */
static void test_set_device_config_null_config(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_device_config(&ctx, NULL));
}

/* Verify query_device_config fails on unexpected msg_id */
static void test_query_device_config_unexpected_msg_id(void)
{
    rsid_device_config_t config;
    uint8_t payload[60];

    init();
    memset(&config, 0, sizeof(config));
    memset(payload, 0, sizeof(payload));

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'z', payload, sizeof(payload), 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_device_config(&ctx, &config));
}

/* Verify query_device_config fails when payload is too small */
static void test_query_device_config_payload_too_small(void)
{
    rsid_device_config_t config;
    uint8_t small_payload[10];

    init();
    memset(&config, 0, sizeof(config));
    memset(small_payload, 0, sizeof(small_payload));

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'q', small_payload, sizeof(small_payload), 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_device_config(&ctx, &config));
}

/* ---- Enrollment depth ---- */

/* Verify progress callback fires for each pose value 0-4 */
static void test_enroll_progress_callback(void)
{
    rsid_enroll_callbacks_t cb;
    int i;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;
    cb.on_progress = test_cb_on_enroll_progress;

    mock_load_session_start(&mock);
    for (i = 0; i < 5; i++)
        mock_append_fa_packet(&mock, 'P', NULL, (char)i, (uint32_t)(i + 1));
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_Success, 6);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 7);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_enroll(&ctx, "Alice", &cb, NULL));
    TEST_ASSERT_EQUAL_INT(5, g_test_cb.progress_count);
    TEST_ASSERT_EQUAL(RSID_Face_Right, g_test_cb.last_pose); /* last pose = 4 = Right */
}

/* Verify hint callback fires during enroll. Note: on_result fires last and
 * overwrites last_enroll_status, so we only assert on hint_count here. */
static void test_enroll_hint_callback(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;
    cb.on_hint = test_cb_on_enroll_hint;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Enroll_FaceDetected, 1);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Enroll_FaceDetected, 2);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_Success, 3);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 4);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_enroll(&ctx, "Bob", &cb, NULL));
    TEST_ASSERT_EQUAL_INT(2, g_test_cb.hint_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
}

/* Verify FaceDetected('g'), LandmarksDetected('h'), and FaceDistances('m') are
 * silently skipped during enrollment — enroll only processes FA packets. */
static void test_enroll_skips_data_packets(void)
{
    rsid_enroll_callbacks_t cb;
    uint8_t face_data[21];
    uint8_t lm_data[45];
    uint8_t dist_data[13];
    rsid_face_rect rect = {0, 0, 0, 0};
    rsid_face_landmarks lm;
    const double d = 50.0;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    memset(&lm, 0, sizeof(lm));
    mock_build_face_data(face_data, sizeof(face_data), 1, 1, &rect);
    mock_build_landmarks_data(lm_data, sizeof(lm_data), 1, 1, &lm);
    mock_build_distances_data(dist_data, sizeof(dist_data), 1, 1, &d);

    mock_load_session_start(&mock);
    mock_append_wire_packet(&mock, 'g', face_data, sizeof(face_data), 1);
    mock_append_wire_packet(&mock, 'h', lm_data, sizeof(lm_data), 2);
    mock_append_wire_packet(&mock, 'm', dist_data, sizeof(dist_data), 3);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_Success, 4);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 5);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_enroll(&ctx, "Charlie", &cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL_INT(0, g_test_cb.face_detected_count);
    TEST_ASSERT_EQUAL_INT(0, g_test_cb.landmarks_count);
    TEST_ASSERT_EQUAL_INT(0, g_test_cb.distances_count);
}

/* Verify enroll returns DatabaseFull status */
static void test_enroll_database_full(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_DatabaseFull, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_DatabaseFull, 2);

    TEST_ASSERT_EQUAL(RSID_DatabaseFull, rsid_enroll(&ctx, "Alice", &cb, NULL));
    TEST_ASSERT_EQUAL(RSID_Enroll_DatabaseFull, g_test_cb.last_enroll_status);
}

/* Verify enroll returns DuplicateUserId status */
static void test_enroll_duplicate_user_id(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_DuplicateUserId, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_DuplicateUserId, 2);

    TEST_ASSERT_EQUAL(RSID_DuplicateUserId, rsid_enroll(&ctx, "Alice", &cb, NULL));
    TEST_ASSERT_EQUAL(RSID_Enroll_DuplicateUserId, g_test_cb.last_enroll_status);
}

/* Verify enroll calls on_result with serial error when session start fails */
static void test_enroll_session_start_fails(void)
{
    rsid_enroll_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_enroll_result;

    mock.recv_fail = 1;

    rsid_enroll(&ctx, "Alice", &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_enroll_status >= RSID_Enroll_Serial_Ok);
}

/* Verify enroll with NULL callbacks succeeds without crash */
static void test_enroll_null_callbacks(void)
{
    rsid_status s;

    init();

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, RSID_Enroll_Success, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    s = rsid_enroll(&ctx, "Alice", NULL, NULL);
    TEST_ASSERT_EQUAL(RSID_Ok, s);
}

/* ---- Auth depth ---- */

/* Verify hint callback fires and increments hint_count during auth.
 * Note: on_result fires after and overwrites last_auth_status. */
static void test_authenticate_hint_with_score(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_hint = test_cb_on_auth_hint;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 1);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 2);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 3);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_authenticate(&ctx, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.hint_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Auth_Success, g_test_cb.last_auth_status);
}

/* Verify result callback with user_id and score */
static void test_authenticate_result_with_score(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_authenticate(&ctx, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL_STRING("Alice", g_test_cb.last_user_id);
    /* Score is decoded from reserved field, which mock fills with '0' chars */
    TEST_ASSERT_EQUAL(RSID_Auth_Success, g_test_cb.last_auth_status);
}

/* Verify an unknown FA msg_id ('Z') is silently skipped during auth.
 * Future protocol extensions may add new msg_ids; the SDK must not crash. */
static void test_authenticate_unknown_msg_id_skipped(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_hint = test_cb_on_auth_hint;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 1);
    /* Unknown FA msg_id 'Z' — should be silently skipped */
    mock_append_fa_packet(&mock, 'Z', NULL, 0, 2);
    mock_append_fa_packet(&mock, 'R', "Bob", RSID_Auth_Success, 3);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 4);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_authenticate(&ctx, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.hint_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL_STRING("Bob", g_test_cb.last_user_id);
}

/* Verify auth calls on_result with serial error when session start fails */
static void test_authenticate_session_start_fails(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock.recv_fail = 1;

    rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_auth_status >= RSID_Auth_Serial_Ok);
}

/* Verify auth fails when send fails after session start */
static void test_authenticate_send_fails(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    mock.send_fail = 1;

    rsid_authenticate(&ctx, &cb, NULL);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_auth_status >= RSID_Auth_Serial_Ok);
}

/* Verify auth handles multiple hints before result */
static void test_authenticate_multiple_hints_then_result(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_hint = test_cb_on_auth_hint;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 1);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceIsTooFarToTheTop, 2);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceIsNotFrontal, 3);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 4);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 5);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_authenticate(&ctx, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(3, g_test_cb.hint_count);
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
}

/* ---- Auth loop ---- */

/* Recv-driven timer for keep-alive testing.
 * Returns recv_calls * 500ms — time naturally advances as recv activity happens.
 * This avoids the problem of a phase-jump timer causing mid-recv timeouts.
 *
 * Timing budget:
 *   Session start recv (7 recv calls):  0→3500ms  < SESSION_START_TIMEOUT (4s)  ✓
 *   Each loop recv    (7 recv calls):   +3500ms   < DEFAULT_RECV_TIMEOUT  (5s)  ✓
 *   keep-alive fires after ~2 loop iterations: elapsed ≈ 7000ms > 4000ms        ✓
 */
static uint32_t ka_get_time(void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    return s->recv_calls * 500;
}

/* Verify auth_loop sends a keep-alive Progress('P') packet when the timer
 * exceeds RSID_KEEP_ALIVE_INTERVAL_MS (4 seconds). The fast-advancing timer
 * (5s per call) forces the keep-alive to fire on every loop iteration. */
static void test_authenticate_loop_keep_alive_sent(void)
{
    rsid_auth_callbacks_t cb;
    /* A Progress packet on the wire starts with: sync1='@', sync2='F', ver=3, msg_id='P' */
    const uint8_t ka_sig[] = {'@', 'F', 3, 'P'};
    uint32_t i;
    int found = 0;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;
    cb.on_hint = test_cb_on_auth_hint;

    ctx.platform.get_time_ms = ka_get_time;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 1);
    mock_append_fa_packet(&mock, 'H', NULL, RSID_Auth_FaceDetected, 2);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 3);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 4);

    rsid_authenticate_loop(&ctx, &cb, NULL);

    /* Search for the 4-byte wire signature of a Progress packet in send_buf.
     * Byte 'P' alone is too common in binary data; matching the full header
     * prefix eliminates false positives. */
    for (i = 0; i + sizeof(ka_sig) <= mock.send_len; i++)
    {
        if (memcmp(mock.send_buf + i, ka_sig, sizeof(ka_sig)) == 0)
        {
            found = 1;
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

/* Verify auth_loop processes Result then terminates on Reply.
 * Even though it's a "loop", Reply('Y') is always terminal. */
static void test_authenticate_loop_multiple_results(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', "Alice", RSID_Auth_Success, 1);
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 2);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_authenticate_loop(&ctx, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_EQUAL(RSID_Auth_Success, g_test_cb.last_auth_status);
}

/* Verify auth_loop reports serial error via on_result when recv fails.
 * Note: rsid_session_start clears cancel_requested, so the "cancel during recv"
 * scenario can only be tested with real threading. This test covers the recv-failure path. */
static void test_authenticate_loop_cancel_during_recv_failure(void)
{
    rsid_auth_callbacks_t cb;

    init();
    test_cb_reset();

    memset(&cb, 0, sizeof(cb));
    cb.on_result = test_cb_on_auth_result;

    mock_load_session_start(&mock);
    /* No more data after session start — recv will fail in loop */

    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_authenticate_loop(&ctx, &cb, NULL));
    TEST_ASSERT_EQUAL_INT(1, g_test_cb.result_count);
    TEST_ASSERT_TRUE(g_test_cb.last_auth_status >= RSID_Auth_Serial_Ok);
}

/* ---- NULL input validation ---- */

/* Verify query_number_of_users with NULL count returns error */
static void test_query_number_of_users_null_count(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_number_of_users(&ctx, NULL));
}

/* Verify query_number_of_users with NULL ctx returns error */
static void test_query_number_of_users_null_ctx(void)
{
    unsigned int count = 0;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_number_of_users(NULL, &count));
}

/* Verify query_user_ids with NULL user_ids returns error */
static void test_query_user_ids_null_user_ids(void)
{
    unsigned int count = 10;
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_user_ids(&ctx, NULL, &count));
}

/* Verify query_user_ids with NULL count returns error */
static void test_query_user_ids_null_count(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_user_ids(&ctx, users, NULL));
}

/* Verify query_user_ids with zero count returns error */
static void test_query_user_ids_zero_count(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 0;
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_user_ids(&ctx, users, &count));
}

/* Verify set_device_config with NULL ctx returns error */
static void test_set_device_config_null_ctx(void)
{
    rsid_device_config_t config;
    memset(&config, 0, sizeof(config));
    config.num_rois = 1;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_set_device_config(NULL, &config));
}

/* Verify query_device_config with NULL ctx returns error */
static void test_query_device_config_null_ctx(void)
{
    rsid_device_config_t config;
    memset(&config, 0, sizeof(config));
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_device_config(NULL, &config));
}

/* Verify query_device_config with NULL config returns error */
static void test_query_device_config_null_config(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_device_config(&ctx, NULL));
}

/* Verify standby with NULL ctx returns error */
static void test_standby_null_ctx(void)
{
    TEST_ASSERT_EQUAL(RSID_Error, rsid_standby(NULL));
}

/* Verify unlock with NULL ctx returns error */
static void test_unlock_null_ctx(void)
{
    TEST_ASSERT_EQUAL(RSID_Error, rsid_unlock(NULL));
}

/* ---- Query user IDs advanced ---- */
/* query_user_ids retrieves users in chunks of 50 (RSID_QUERY_CHUNK_SIZE).
 * Each chunk requires a new session because the device closes it after responding. */

/* Verify multi-chunk retrieval: 2 users in chunk 1, 1 in chunk 2, 0 in chunk 3 (end). */
static void test_query_user_ids_multi_chunk(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 10;
    uint8_t payload[128];
    uint32_t arrived;
    uint32_t pos;

    init();

    /* First chunk session: returns 2 users */
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */

    arrived = 2;
    pos = 0;
    memset(payload, 0, sizeof(payload));
    memcpy(payload + pos, &arrived, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(payload + pos, "Alice", 6);
    pos += 6;
    memcpy(payload + pos, "Bob", 4);
    pos += 4;
    mock_append_wire_packet(&mock, 'u', payload, pos, 1);

    /* Second chunk session: returns 1 user */
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */
    arrived = 1;
    pos = 0;
    memset(payload, 0, sizeof(payload));
    memcpy(payload + pos, &arrived, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(payload + pos, "Charlie", 8);
    pos += 8;
    mock_append_wire_packet(&mock, 'u', payload, pos, 1);

    /* Third chunk session: returns 0 users (end) */
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */
    arrived = 0;
    mock_append_wire_packet(&mock, 'u', &arrived, sizeof(arrived), 1);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_user_ids(&ctx, users, &count));
    TEST_ASSERT_EQUAL_UINT(3, count);
    TEST_ASSERT_EQUAL_STRING("Alice", users[0]);
    TEST_ASSERT_EQUAL_STRING("Bob", users[1]);
    TEST_ASSERT_EQUAL_STRING("Charlie", users[2]);
}

/* Verify retrieval stops when the caller's array is full (count=2),
 * even though the device reports 3 users in the chunk. */
static void test_query_user_ids_capacity_reached(void)
{
    char users[2][RSID_MAX_USER_ID + 1];
    unsigned int count = 2;
    uint8_t payload[128];
    uint32_t arrived;
    uint32_t pos;

    init();

    /* First chunk: device says 3 arrived, but we only have capacity for 2 */
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */

    arrived = 3;
    pos = 0;
    memset(payload, 0, sizeof(payload));
    memcpy(payload + pos, &arrived, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(payload + pos, "A", 2); /* 1 char + NUL */
    pos += 2;
    memcpy(payload + pos, "B", 2);
    pos += 2;
    memcpy(payload + pos, "C", 2);
    pos += 2;
    mock_append_wire_packet(&mock, 'u', payload, pos, 1);

    rsid_query_user_ids(&ctx, users, &count);
    TEST_ASSERT_EQUAL_UINT(2, count);
    TEST_ASSERT_EQUAL_STRING("A", users[0]);
    TEST_ASSERT_EQUAL_STRING("B", users[1]);
}

/* Verify error when the second chunk's session start fails.
 * First chunk succeeds (2 users), then no more recv data → session start fails. */
static void test_query_user_ids_session_start_fails_second_chunk(void)
{
    char users[10][RSID_MAX_USER_ID + 1];
    unsigned int count = 10;
    uint8_t payload[128];
    uint32_t arrived;
    uint32_t pos;

    init();

    /* First chunk session: returns 2 users */
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0); /* session start response */

    arrived = 2;
    pos = 0;
    memset(payload, 0, sizeof(payload));
    memcpy(payload + pos, &arrived, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    memcpy(payload + pos, "Alice", 6);
    pos += 6;
    memcpy(payload + pos, "Bob", 4);
    pos += 4;
    mock_append_wire_packet(&mock, 'u', payload, pos, 1);

    /* Second session start will fail — no more data in recv buffer */

    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_query_user_ids(&ctx, users, &count));
}

/* ---- State / cancel ---- */

/* Verify rsid_init zeroes all internal state, including a stale cancel flag. */
static void test_init_clears_cancel_flag(void)
{
    init();
    ctx._internal.cancel_requested = 1; /* stale flag from a previous session */
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_init(&ctx));
    TEST_ASSERT_EQUAL_UINT8(0, ctx._internal.cancel_requested);
}

/* Verify rsid_cancel with NULL ctx returns error */
static void test_cancel_null_ctx(void)
{
    TEST_ASSERT_EQUAL(RSID_Error, rsid_cancel(NULL));
}

/* ---- Additional ---- */

/* Verify remove_all returns error on unexpected msg_id (Result instead of Reply) */
static void test_remove_all_unexpected_reply(void)
{
    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, 0, 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_remove_all(&ctx));
}

/* Verify remove_user returns error when send fails after session start */
static void test_remove_user_send_fails(void)
{
    init();
    mock_load_session_start(&mock);
    mock.send_fail = 1;

    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_remove_user(&ctx, "Alice"));
}

/* Verify unlock returns error on unexpected msg_id (Result instead of Reply) */
static void test_unlock_unexpected_reply(void)
{
    init();
    mock_load_session_start(&mock);
    mock_append_fa_packet(&mock, 'R', NULL, 0, 1);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_unlock(&ctx));
}

/* ---- Runner ---- */

void test_api_run(void)
{
    RUN_TEST(test_enroll_success);
    RUN_TEST(test_enroll_failure);
    RUN_TEST(test_authenticate_success);
    RUN_TEST(test_authenticate_no_face);
    RUN_TEST(test_authenticate_face_detected_interleaved);
    RUN_TEST(test_authenticate_face_detected_5_faces);
    RUN_TEST(test_authenticate_landmarks_detected);
    RUN_TEST(test_authenticate_face_distances);
    RUN_TEST(test_authenticate_null_callbacks);
    RUN_TEST(test_authenticate_loop_cancel);
    RUN_TEST(test_remove_all_success);
    RUN_TEST(test_remove_all_error);
    RUN_TEST(test_query_number_of_users_success);
    RUN_TEST(test_query_number_of_users_zero);
    RUN_TEST(test_query_user_ids_single);
    RUN_TEST(test_query_user_ids_empty);
    RUN_TEST(test_set_device_config_echo_mismatch);
    RUN_TEST(test_set_device_config_device_error);
    RUN_TEST(test_query_device_config_success);
    RUN_TEST(test_query_device_config_error);
    RUN_TEST(test_standby_success);
    RUN_TEST(test_unlock_success);
    RUN_TEST(test_reboot_success);
    RUN_TEST(test_hibernate_success);
    RUN_TEST(test_reboot_send_fails);
    RUN_TEST(test_hibernate_send_fails);
    RUN_TEST(test_remove_user_success);
    RUN_TEST(test_remove_user_unexpected_reply);
    RUN_TEST(test_cancel_sets_flag);
    RUN_TEST(test_bsp_version_success);
    RUN_TEST(test_bsp_version_truncated);
    RUN_TEST(test_set_color_gains_success);
    RUN_TEST(test_set_color_gains_boundary);
    RUN_TEST(test_set_color_gains_send_fails);
    RUN_TEST(test_enroll_timeout);
    RUN_TEST(test_authenticate_timeout);
    RUN_TEST(test_enroll_recv_error);
    RUN_TEST(test_authenticate_recv_error);
    RUN_TEST(test_authenticate_loop_recv_error);
    RUN_TEST(test_query_user_ids_missing_nul);
    RUN_TEST(test_query_user_ids_id_too_long);

    /* Config round-trip */
    RUN_TEST(test_set_device_config_success);
    RUN_TEST(test_set_device_config_all_fields);
    RUN_TEST(test_config_roundtrip_gpio_encoding);
    RUN_TEST(test_config_roundtrip_rois);
    RUN_TEST(test_config_num_rois_zero_sanitized);
    RUN_TEST(test_config_num_rois_overflow_sanitized);
    RUN_TEST(test_set_device_config_payload_too_small);
    RUN_TEST(test_set_device_config_unexpected_msg_id);
    RUN_TEST(test_set_device_config_null_config);
    RUN_TEST(test_query_device_config_unexpected_msg_id);
    RUN_TEST(test_query_device_config_payload_too_small);

    /* Enrollment depth */
    RUN_TEST(test_enroll_progress_callback);
    RUN_TEST(test_enroll_hint_callback);
    RUN_TEST(test_enroll_skips_data_packets);
    RUN_TEST(test_enroll_database_full);
    RUN_TEST(test_enroll_duplicate_user_id);
    RUN_TEST(test_enroll_session_start_fails);
    RUN_TEST(test_enroll_null_callbacks);

    /* Auth depth */
    RUN_TEST(test_authenticate_hint_with_score);
    RUN_TEST(test_authenticate_result_with_score);
    RUN_TEST(test_authenticate_unknown_msg_id_skipped);
    RUN_TEST(test_authenticate_session_start_fails);
    RUN_TEST(test_authenticate_send_fails);
    RUN_TEST(test_authenticate_multiple_hints_then_result);

    /* Auth loop */
    RUN_TEST(test_authenticate_loop_keep_alive_sent);
    RUN_TEST(test_authenticate_loop_multiple_results);
    RUN_TEST(test_authenticate_loop_cancel_during_recv_failure);

    /* NULL input validation */
    RUN_TEST(test_query_number_of_users_null_count);
    RUN_TEST(test_query_number_of_users_null_ctx);
    RUN_TEST(test_query_user_ids_null_user_ids);
    RUN_TEST(test_query_user_ids_null_count);
    RUN_TEST(test_query_user_ids_zero_count);
    RUN_TEST(test_set_device_config_null_ctx);
    RUN_TEST(test_query_device_config_null_ctx);
    RUN_TEST(test_query_device_config_null_config);
    RUN_TEST(test_standby_null_ctx);
    RUN_TEST(test_unlock_null_ctx);

    /* Query user IDs advanced */
    RUN_TEST(test_query_user_ids_multi_chunk);
    RUN_TEST(test_query_user_ids_capacity_reached);
    RUN_TEST(test_query_user_ids_session_start_fails_second_chunk);

    /* State / cancel */
    RUN_TEST(test_init_clears_cancel_flag);
    RUN_TEST(test_cancel_null_ctx);

    /* Additional */
    RUN_TEST(test_remove_all_unexpected_reply);
    RUN_TEST(test_remove_user_send_fails);
    RUN_TEST(test_unlock_unexpected_reply);
}
