/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Mock platform implementation for unit testing. */

#include "mock_platform.h"
#include "rsid_common.h"
#include <string.h>

/* Captures sent bytes into send_buf; fails if send_fail is set */
static int mock_send(const uint8_t* data, uint32_t len, void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    s->send_calls++;
    if (s->send_fail)
        return -1;
    if (s->send_len + len > MOCK_BUF_SIZE)
        return -1;
    memcpy(s->send_buf + s->send_len, data, len);
    s->send_len += len;
    return 0;
}

/* Replays bytes from recv_buf; fails if recv_fail is set or buffer exhausted */
static int mock_recv(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    (void)timeout_ms;
    s->recv_calls++;
    if (s->recv_fail)
        return -1;
    if (s->recv_pos + len > s->recv_len)
        return -1; /* no more data — simulates timeout */
    memcpy(data, s->recv_buf + s->recv_pos, len);
    s->recv_pos += len;
    return 0;
}

/* Returns time_ms from state (frozen unless advanced by mock_sleep_ms) */
static uint32_t mock_get_time_ms(void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    return s->time_ms;
}

/* Advances time_ms by ms; counts sleep calls */
static void mock_sleep_ms(uint32_t ms, void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    s->sleep_calls++;
    s->time_ms += ms;
}

/* Counts purge calls; no-op otherwise */
static void mock_purge(void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    s->purge_calls++;
}

/* Stores last message in last_log; counts log calls */
static void mock_log(const char* msg, void* app_ctx)
{
    mock_state_t* s = (mock_state_t*)app_ctx;
    s->log_calls++;
    if (msg)
    {
        size_t len = strlen(msg);
        if (len >= sizeof(s->last_log))
            len = sizeof(s->last_log) - 1;
        memcpy(s->last_log, msg, len);
        s->last_log[len] = '\0';
    }
}

void mock_reset(mock_state_t* state)
{
    memset(state, 0, sizeof(*state));
}

void mock_init(rsid_ctx_t* ctx, mock_state_t* state)
{
    mock_reset(state);
    memset(ctx, 0, sizeof(*ctx));
    ctx->platform.send = mock_send;
    ctx->platform.recv = mock_recv;
    ctx->platform.get_time_ms = mock_get_time_ms;
    ctx->platform.purge = mock_purge;
    ctx->platform.sleep_ms = mock_sleep_ms;
    ctx->platform.debug = mock_log;
    ctx->platform.app_ctx = state;
    rsid_init(ctx);
}

void mock_set_recv_data(mock_state_t* state, const uint8_t* data, uint32_t len)
{
    if (len > MOCK_BUF_SIZE)
        len = MOCK_BUF_SIZE;
    memcpy(state->recv_buf, data, len);
    state->recv_len = len;
    state->recv_pos = 0;
}

uint32_t mock_append_wire_packet(mock_state_t* state, char msg_id, const void* data, uint32_t data_size, uint32_t seq_number)
{
    uint32_t remaining = MOCK_BUF_SIZE - state->recv_len;
    uint32_t n = mock_build_wire_packet(state->recv_buf + state->recv_len, remaining, msg_id, data, data_size, seq_number);
    state->recv_len += n;
    return n;
}

uint32_t mock_append_fa_packet(mock_state_t* state, char msg_id, const char* user_id, char status, uint32_t seq_number)
{
    rsid_serial_packet_t pkt;
    uint32_t header_payload_size;
    uint32_t total;
    uint32_t remaining = MOCK_BUF_SIZE - state->recv_len;
    uint8_t* dest = state->recv_buf + state->recv_len;

    rsid_packet_init_fa(&pkt, msg_id, user_id, status);
    pkt.payload.sequence_number = seq_number;
    pkt.crc = rsid_packet_calc_crc(&pkt);

    header_payload_size = (uint32_t)(sizeof(pkt.header) + pkt.header.payload_size);
    total = header_payload_size + RSID_HMAC_SIZE + 2;

    if (total > remaining)
        return 0;

    memcpy(dest, &pkt, header_payload_size);
    memcpy(dest + header_payload_size, pkt.hmac, RSID_HMAC_SIZE);
    memcpy(dest + header_payload_size + RSID_HMAC_SIZE, &pkt.crc, 2);

    state->recv_len += total;
    return total;
}

uint32_t mock_build_wire_packet(uint8_t* buf, uint32_t buf_size, char msg_id, const void* data, uint32_t data_size, uint32_t seq_number)
{
    rsid_serial_packet_t pkt;
    uint32_t header_payload_size;
    uint32_t total;

    /* Build packet using the SDK's own init functions */
    if (msg_id >= 'A' && msg_id <= 'Z')
        rsid_packet_init_fa(&pkt, msg_id, (const char*)data, (char)(data_size & 0xFF));
    else
        rsid_packet_init_data(&pkt, msg_id, (const char*)data, data_size);

    pkt.payload.sequence_number = seq_number;
    pkt.crc = rsid_packet_calc_crc(&pkt);

    /* Serialize to wire format: header+payload, then hmac, then crc */
    header_payload_size = (uint32_t)(sizeof(pkt.header) + pkt.header.payload_size);
    total = header_payload_size + RSID_HMAC_SIZE + 2;

    if (total > buf_size)
        return 0;

    memcpy(buf, &pkt, header_payload_size);
    memcpy(buf + header_payload_size, pkt.hmac, RSID_HMAC_SIZE);
    memcpy(buf + header_payload_size + RSID_HMAC_SIZE, &pkt.crc, 2);

    return total;
}

/* ---- Session helpers ---- */

void mock_start_session_ok(rsid_ctx_t* ctx, mock_state_t* state)
{
    uint8_t wire[512];
    uint32_t wire_len;
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(state, wire, wire_len);
    rsid_session_start(ctx);
    state->send_len = 0;
}

void mock_load_session_start(mock_state_t* state)
{
    state->recv_len = 0;
    state->recv_pos = 0;
    mock_append_wire_packet(state, 'o', NULL, 0, 0);
}

void mock_set_text_response(mock_state_t* state, const char* text)
{
    mock_set_recv_data(state, (const uint8_t*)text, (uint32_t)strlen(text));
}

int mock_send_buf_contains(const mock_state_t* state, const char* needle)
{
    uint32_t needle_len = (uint32_t)strlen(needle);
    uint32_t i;
    for (i = 0; i + needle_len <= state->send_len; i++)
    {
        if (memcmp(state->send_buf + i, needle, needle_len) == 0)
            return 1;
    }
    return 0;
}

/* ---- Binary payload builders ---- */

uint32_t mock_build_face_data(uint8_t* buf, uint32_t buf_size, unsigned int n_faces, uint32_t timestamp, const rsid_face_rect* rects)
{
    uint32_t needed = 1 + 4 + n_faces * 16;
    unsigned int i;
    uint32_t off;
    if (needed > buf_size)
        return 0;
    buf[0] = (uint8_t)n_faces;
    memcpy(buf + 1, &timestamp, 4);
    for (i = 0; i < n_faces; i++)
    {
        off = 5 + i * 16;
        memcpy(buf + off, &rects[i].x, 4);
        memcpy(buf + off + 4, &rects[i].y, 4);
        memcpy(buf + off + 8, &rects[i].w, 4);
        memcpy(buf + off + 12, &rects[i].h, 4);
    }
    return needed;
}

uint32_t mock_build_landmarks_data(uint8_t* buf, uint32_t buf_size, unsigned int n_faces, uint32_t timestamp,
                                   const rsid_face_landmarks* landmarks)
{
    uint32_t needed = 1 + 4 + n_faces * 40;
    unsigned int i, j;
    uint32_t base;
    if (needed > buf_size)
        return 0;
    buf[0] = (uint8_t)n_faces;
    memcpy(buf + 1, &timestamp, 4);
    for (i = 0; i < n_faces; i++)
    {
        base = 5 + i * 40;
        for (j = 0; j < RSID_NUM_FACE_LANDMARKS; j++)
        {
            memcpy(buf + base + j * 4, &landmarks[i].lm_x[j], 4);
            memcpy(buf + base + 20 + j * 4, &landmarks[i].lm_y[j], 4);
        }
    }
    return needed;
}

uint32_t mock_build_distances_data(uint8_t* buf, uint32_t buf_size, unsigned int n_faces, uint32_t timestamp, const double* distances)
{
    uint32_t needed = 1 + 4 + n_faces * 8;
    unsigned int i;
    if (needed > buf_size)
        return 0;
    buf[0] = (uint8_t)n_faces;
    memcpy(buf + 1, &timestamp, 4);
    for (i = 0; i < n_faces; i++)
        memcpy(buf + 5 + i * 8, &distances[i], 8);
    return needed;
}

/* ---- Shared callback tracking ---- */

test_cb_state_t g_test_cb;

void test_cb_reset(void)
{
    memset(&g_test_cb, 0, sizeof(g_test_cb));
}

void test_cb_on_auth_result(rsid_auth_status status, const char* user_id, short score, void* ctx)
{
    (void)ctx;
    g_test_cb.result_count++;
    g_test_cb.last_auth_status = status;
    g_test_cb.last_score = score;
    if (user_id)
    {
        strncpy(g_test_cb.last_user_id, user_id, RSID_MAX_USER_ID);
        g_test_cb.last_user_id[RSID_MAX_USER_ID] = '\0';
    }
}

void test_cb_on_auth_hint(rsid_auth_status hint, float frame_score, void* ctx)
{
    (void)ctx;
    g_test_cb.hint_count++;
    g_test_cb.last_auth_status = hint;
    g_test_cb.last_frame_score = frame_score;
}

void test_cb_on_face_detected(const rsid_face_rect* faces, unsigned int num_faces, unsigned int ts, void* ctx)
{
    unsigned int i;
    (void)ctx;
    g_test_cb.face_detected_count++;
    g_test_cb.last_num_faces = num_faces;
    g_test_cb.last_face_ts = ts;
    for (i = 0; i < num_faces && i < RSID_MAX_ROIS; i++)
        g_test_cb.last_faces[i] = faces[i];
}

void test_cb_on_landmarks_detected(const rsid_face_landmarks* landmarks, unsigned int num_faces, unsigned int ts, void* ctx)
{
    unsigned int i;
    (void)ctx;
    g_test_cb.landmarks_count++;
    g_test_cb.last_num_faces = num_faces;
    g_test_cb.last_face_ts = ts;
    for (i = 0; i < num_faces && i < RSID_MAX_ROIS; i++)
        g_test_cb.last_landmarks[i] = landmarks[i];
}

void test_cb_on_face_distances(const double* distances, unsigned int num_faces, unsigned int ts, void* ctx)
{
    unsigned int i;
    (void)ctx;
    g_test_cb.distances_count++;
    g_test_cb.last_num_faces = num_faces;
    g_test_cb.last_face_ts = ts;
    for (i = 0; i < num_faces && i < RSID_MAX_ROIS; i++)
        g_test_cb.last_distances[i] = distances[i];
}

void test_cb_on_enroll_result(rsid_enroll_status status, void* ctx)
{
    (void)ctx;
    g_test_cb.result_count++;
    g_test_cb.last_enroll_status = status;
}

void test_cb_on_enroll_progress(rsid_face_pose pose, void* ctx)
{
    (void)ctx;
    g_test_cb.progress_count++;
    g_test_cb.last_pose = pose;
}

void test_cb_on_enroll_hint(rsid_enroll_status hint, float frame_score, void* ctx)
{
    (void)ctx;
    g_test_cb.hint_count++;
    g_test_cb.last_enroll_status = hint;
    g_test_cb.last_frame_score = frame_score;
}
