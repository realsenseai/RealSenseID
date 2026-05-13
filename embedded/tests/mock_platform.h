/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Mock platform for unit testing the embedded SDK. */

#ifndef MOCK_PLATFORM_H
#define MOCK_PLATFORM_H

#include "rsid.h"
#include "rsid_packet.h"
#include <stdint.h>

#define MOCK_BUF_SIZE 16384

typedef struct
{
    /* Canned recv data: mock_recv returns bytes from this buffer sequentially */
    uint8_t recv_buf[MOCK_BUF_SIZE];
    uint32_t recv_len;
    uint32_t recv_pos;

    /* Recorded send data: mock_send appends all sent bytes here */
    uint8_t send_buf[MOCK_BUF_SIZE];
    uint32_t send_len;

    /* Controllable timer */
    uint32_t time_ms;

    /* Failure injection */
    int send_fail; /* if non-zero, send returns -1 */
    int recv_fail; /* if non-zero, recv returns -1 (before checking buffer) */

    /* Call counters */
    uint32_t send_calls;
    uint32_t recv_calls;
    uint32_t purge_calls;
    uint32_t sleep_calls;
    uint32_t log_calls;

    /* Last log message */
    char last_log[256];
} mock_state_t;

/* Initialize ctx with mock platform callbacks. Resets mock state. */
void mock_init(rsid_ctx_t* ctx, mock_state_t* state);

/* Reset mock state (clears buffers, counters, time) without re-wiring callbacks */
void mock_reset(mock_state_t* state);

/* Load canned bytes into recv buffer (replaces existing data) */
void mock_set_recv_data(mock_state_t* state, const uint8_t* data, uint32_t len);

/* Append a wire-format packet to the recv buffer. Returns bytes appended. */
uint32_t mock_append_wire_packet(mock_state_t* state, char msg_id, const void* data, uint32_t data_size, uint32_t seq_number);

/* Append an FA wire-format packet with explicit user_id and status. */
uint32_t mock_append_fa_packet(mock_state_t* state, char msg_id, const char* user_id, char status, uint32_t seq_number);

/* Build a valid wire-format packet into buf. Returns total wire bytes written.
 * For FA packets (msg_id 'A'-'Z'): user_id + status via rsid_packet_init_fa.
 * For data packets (msg_id 'a'-'z'): raw data via rsid_packet_init_data.
 * The packet includes sync bytes, header, payload, zeroed hmac, and correct CRC. */
uint32_t mock_build_wire_packet(uint8_t* buf, uint32_t buf_size, char msg_id, const void* data, uint32_t data_size, uint32_t seq_number);

/* ---- Session helpers ---- */

/* Establish a session: queue StartSession('o'), consume it, clear send buffer.
 * Call this when a test needs an active session before exercising send/recv. */
void mock_start_session_ok(rsid_ctx_t* ctx, mock_state_t* state);

/* Reset recv buffer and queue a StartSession('o') response.
 * Call mock_append_*() after this to queue the device response packets. */
void mock_load_session_start(mock_state_t* state);

/* Load a text response into the mock recv buffer (convenience for text-mode tests). */
void mock_set_text_response(mock_state_t* state, const char* text);

/* Return 1 if needle appears anywhere in the send buffer, 0 otherwise. */
int mock_send_buf_contains(const mock_state_t* state, const char* needle);

/* ---- Binary payload builders ---- */

/* Build face-detected binary payload. Returns total bytes written.
 * Wire layout: [n_faces:1][timestamp:4][n_faces * {x,y,w,h}:16 each] */
uint32_t mock_build_face_data(uint8_t* buf, uint32_t buf_size, unsigned int n_faces, uint32_t timestamp, const rsid_face_rect* rects);

/* Build landmarks-detected binary payload. Returns total bytes written.
 * Wire layout: [n_faces:1][timestamp:4][n_faces * {lm_x[5],lm_y[5]}:40 each] */
uint32_t mock_build_landmarks_data(uint8_t* buf, uint32_t buf_size, unsigned int n_faces, uint32_t timestamp,
                                   const rsid_face_landmarks* landmarks);

/* Build face-distances binary payload. Returns total bytes written.
 * Wire layout: [n_faces:1][timestamp:4][n_faces * double:8 each] */
uint32_t mock_build_distances_data(uint8_t* buf, uint32_t buf_size, unsigned int n_faces, uint32_t timestamp, const double* distances);

/* ---- Shared callback tracking ---- */

/* Callback state used across test files to record API callback invocations. */
typedef struct
{
    int result_count;
    int hint_count;
    int progress_count;
    int face_detected_count;
    int landmarks_count;
    int distances_count;
    unsigned int last_num_faces;
    unsigned int last_face_ts;
    rsid_face_rect last_faces[RSID_MAX_ROIS];
    rsid_face_landmarks last_landmarks[RSID_MAX_ROIS];
    double last_distances[RSID_MAX_ROIS];
    rsid_auth_status last_auth_status;
    rsid_enroll_status last_enroll_status;
    char last_user_id[RSID_MAX_USER_ID + 1];
    short last_score;
    rsid_face_pose last_pose;
    float last_frame_score;
} test_cb_state_t;

extern test_cb_state_t g_test_cb;

void test_cb_reset(void);

/* Auth callbacks — record into g_test_cb */
void test_cb_on_auth_result(rsid_auth_status status, const char* user_id, short score, void* ctx);
void test_cb_on_auth_hint(rsid_auth_status hint, float frame_score, void* ctx);
void test_cb_on_face_detected(const rsid_face_rect* faces, unsigned int num_faces, unsigned int ts, void* ctx);
void test_cb_on_landmarks_detected(const rsid_face_landmarks* landmarks, unsigned int num_faces, unsigned int ts, void* ctx);
void test_cb_on_face_distances(const double* distances, unsigned int num_faces, unsigned int ts, void* ctx);

/* Enroll callbacks — record into g_test_cb */
void test_cb_on_enroll_result(rsid_enroll_status status, void* ctx);
void test_cb_on_enroll_progress(rsid_face_pose pose, void* ctx);
void test_cb_on_enroll_hint(rsid_enroll_status hint, float frame_score, void* ctx);

#endif /* MOCK_PLATFORM_H */
