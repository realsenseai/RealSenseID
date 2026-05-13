/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Unit tests for rsid_session.c — session handshake, seq validation, cancel. */

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

/* ---- Session start ---- */

/* Verify session start succeeds with valid StartSession('o') response */
static void test_session_start_success(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Device responds with StartSession('o') */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
    TEST_ASSERT_TRUE(mock.send_len > 0);           /* something was sent */
    TEST_ASSERT_EQUAL_UINT32(0, mock.purge_calls); /* no purge on first successful attempt */
}

/* Verify session start fails when recv times out */
static void test_session_start_timeout(void)
{
    init();
    /* No recv data — recv will fail, simulating timeout */
    mock.recv_fail = 1;
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
}

/* Verify session start fails on device rejection (Reply 'Y') */
static void test_session_start_rejected(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Device sends Reply('Y') — rejection */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
}

/* Verify session start fails on unexpected packet */
static void test_session_start_unexpected_packet(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Device sends a junk packet instead of StartSession response */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'z', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
}

/* Verify session start retries and succeeds after initial failure */
static void test_session_start_retry_then_success(void)
{
    uint8_t wire[512];
    uint32_t total = 0;

    init();
    /* First attempt: junk. After cancel+retry, device responds with StartSession. */
    total += mock_build_wire_packet(wire + total, sizeof(wire) - total, 'z', NULL, 0, 0);
    total += mock_build_wire_packet(wire + total, sizeof(wire) - total, 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, total);

    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
}

/* ---- Sequence numbers ---- */

/* Verify each send increments the sequence number */
static void test_session_send_increments_seq(void)
{
    uint8_t wire[512];
    uint32_t wire_len;
    rsid_serial_packet_t* pkt;

    init();
    /* Start session first */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Clear send buffer to isolate the send */
    mock.send_len = 0;

    /* First send — seq should be 1 */
    pkt = ctx_packet(&ctx);
    rsid_packet_init_fa(pkt, 'A', NULL, 0);
    rsid_session_send(&ctx);
    TEST_ASSERT_EQUAL_UINT32(1, ctx._internal.last_sent_seq);

    /* Second send — seq should be 2 */
    mock.send_len = 0;
    rsid_packet_init_fa(pkt, 'A', NULL, 0);
    rsid_session_send(&ctx);
    TEST_ASSERT_EQUAL_UINT32(2, ctx._internal.last_sent_seq);
}

/* Verify recv accepts a packet with the next expected seq */
static void test_session_recv_accepts_valid_seq(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Start session */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Recv packet with seq=1 (first after session start where last_recv=0) */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
    TEST_ASSERT_EQUAL_UINT32(1, ctx._internal.last_recv_seq);
}

/* Verify recv rejects a replayed (duplicate) sequence number */
static void test_session_recv_rejects_replay(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Start session */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Recv seq=1 — OK */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));

    /* Recv seq=1 again — replay, should fail */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_SECURITY_ERROR, rsid_session_recv(&ctx, 5000));
}

/* Verify recv rejects seq delta exceeding RSID_MAX_SEQ_DELTA */
static void test_session_recv_rejects_seq_too_far(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Start session */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Recv seq=1 — OK */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_recv(&ctx, 5000);

    /* Recv seq=22 — delta 21, exceeds RSID_MAX_SEQ_DELTA (20) */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 22);
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_SECURITY_ERROR, rsid_session_recv(&ctx, 5000));
}

/* Verify recv accepts seq at exactly RSID_MAX_SEQ_DELTA */
static void test_session_recv_accepts_max_delta(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Start session */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Recv seq=1 — OK */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_recv(&ctx, 5000);

    /* Recv seq=21 — delta exactly 20, should be OK */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 21);
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 5000));
}

/* ---- CRC / version errors ---- */

/* Verify recv detects corrupted CRC and returns CRC_ERROR */
static void test_session_recv_crc_error(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Start session */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Build a valid packet then corrupt the CRC */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    wire[wire_len - 1] ^= 0xFF; /* flip CRC byte */
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_CRC_ERROR, rsid_session_recv(&ctx, 5000));
}

/* Verify recv rejects packet with wrong protocol version */
static void test_session_recv_version_mismatch(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    /* Start session */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);

    /* Build packet then change protocol version from 3 to 2 */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    wire[2] = 2; /* protocol_ver is at offset 2 (after sync1, sync2) */
    mock_set_recv_data(&mock, wire, wire_len);
    TEST_ASSERT_EQUAL(RSID_SERIAL_VERSION_MISMATCH, rsid_session_recv(&ctx, 5000));
}

/* ---- Cancel ---- */

/* Verify cancel flag triggers __FACE_CANCEL__ and clears */
static void test_session_cancel_flag(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();

    /* rsid_cancel sets flag and sends __FACE_CANCEL__ immediately */
    mock.send_len = 0;
    rsid_cancel(&ctx);
    TEST_ASSERT_EQUAL_UINT8(1, ctx._internal.cancel_requested);
    TEST_ASSERT_TRUE(mock.send_len > 0); /* __FACE_CANCEL__ was sent */

    /* Flag is cleared at next session start */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);
    rsid_session_start(&ctx);
    TEST_ASSERT_EQUAL_UINT8(0, ctx._internal.cancel_requested);
}

/* Verify send_cancel emits __FACE_CANCEL__ on the wire */
static void test_session_send_cancel(void)
{
    init();
    mock.send_len = 0;
    rsid_session_send_cancel(&ctx);
    TEST_ASSERT_TRUE(mock_send_buf_contains(&mock, "__FACE_CANCEL__"));
}

/* ---- Timeouts ---- */

/* mock get_time_ms that auto-advances to simulate time passing */
static uint32_t timeout_time_ms;
static uint32_t timeout_get_time_ms(void* app_ctx)
{
    (void)app_ctx;
    timeout_time_ms += 100; /* advance 100ms per call */
    return timeout_time_ms;
}

/* Verify recv times out when no data arrives */
static void test_recv_timeout_no_data(void)
{
    init();
    mock_start_session_ok(&ctx, &mock);

    /* No recv data, mock timer advances — remaining_ms will expire */
    timeout_time_ms = 0;
    ctx.platform.get_time_ms = timeout_get_time_ms;

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 500));
}

/* Verify recv fails when no data is available, even with a short timeout.
 * Empty recv buffer → mock_recv returns -1 immediately → recv fails. */
static void test_recv_timeout_with_short_deadline(void)
{
    init();
    mock_start_session_ok(&ctx, &mock);

    mock.time_ms = 10000;
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_recv(&ctx, 100));
}

/* Verify recv_raw times out on truncated packet data */
static void test_recv_raw_timeout_mid_packet(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();

    /* Build a valid packet but only provide the first 10 bytes.
     * With auto-advancing timer, remaining_ms will expire mid-read. */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    mock_set_recv_data(&mock, wire, 10); /* truncated */

    timeout_time_ms = 0;
    ctx.platform.get_time_ms = timeout_get_time_ms;

    /* recv_packet_raw should timeout — not enough data within deadline */
    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_recv_packet(&ctx.platform, ctx_packet(&ctx), 500));
}

/* ---- Retry / purge / sleep ---- */

/* Verify the UART buffer is purged between retry attempts.
 * Scenario: first attempt receives junk ('z'), retry receives valid response ('o').
 * purge() must be called before the retry to clear stale UART data. */
static void test_session_start_purge_called(void)
{
    uint8_t wire[512];
    uint32_t total = 0;

    init();
    total += mock_build_wire_packet(wire + total, sizeof(wire) - total, 'z', NULL, 0, 0);
    total += mock_build_wire_packet(wire + total, sizeof(wire) - total, 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, total);

    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
    TEST_ASSERT_TRUE(mock.purge_calls > 0); /* purge was called between attempts */
}

/* Verify session start gives up after 3 failed attempts (RSID_SESSION_MAX_RETRIES).
 * With recv always failing, all 3 attempts fail. The 2 gaps between 3 attempts
 * require 2 sleep calls (RSID_SESSION_RETRY_DELAY_MS each). */
static void test_session_start_max_retries_exhausted(void)
{
    init();
    mock.recv_fail = 1; /* every attempt fails */

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
    TEST_ASSERT_TRUE(mock.sleep_calls >= 2); /* slept between each of 3 attempts */
}

/* ---- send_binary / __FACE_API__ prefix ---- */

/* Verify rsid_send_packet prepends "\r\n__FACE_API__\r\n" before the packet.
 * This prefix is how the device's UART console distinguishes binary traffic from text. */
static void test_session_send_binary_includes_face_api_prefix(void)
{
    rsid_serial_packet_t pkt;

    init();
    mock.send_len = 0;
    rsid_packet_init_data(&pkt, 'p', NULL, 0);
    rsid_send_packet(&ctx.platform, &pkt);

    TEST_ASSERT_TRUE(mock_send_buf_contains(&mock, "__FACE_API__"));
}

/* ---- recv edge cases ---- */

/* Verify recv rejects (not crashes on) a packet whose payload_size claims 64 KB.
 * This tests the bounds check: payload_size > sizeof(pkt.payload) must be rejected. */
static void test_session_recv_payload_size_exceeds_buffer(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    mock_start_session_ok(&ctx, &mock);

    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'Y', NULL, 0, 1);
    wire[20] = 0xFE; /* payload_size = 0xFFFE (little-endian at offset 20-21) */
    wire[21] = 0xFF;
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_NOT_EQUAL(RSID_SERIAL_OK, rsid_recv_packet(&ctx.platform, ctx_packet(&ctx), 5000));
}

/* Verify recv_raw handles a data packet with zero user data (only seq_number).
 * payload_size will be 32 (seq_number aligned up). This is a valid degenerate case. */
static void test_session_recv_raw_zero_payload(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'p', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_recv_packet(&ctx.platform, ctx_packet(&ctx), 5000));
}

/* ---- Cancel cleared on session start ---- */

/* Verify rsid_session_start clears a stale cancel_requested flag.
 * If cancel_requested was set in a previous operation, it must not
 * leak into the next session. */
static void test_session_start_cancel_cleared(void)
{
    uint8_t wire[512];
    uint32_t wire_len;

    init();
    ctx._internal.cancel_requested = 1; /* stale flag from previous operation */

    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'o', NULL, 0, 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_EQUAL(RSID_SERIAL_OK, rsid_session_start(&ctx));
    TEST_ASSERT_EQUAL_UINT8(0, ctx._internal.cancel_requested); /* cleared */
}

/* ---- Runner ---- */

void test_session_run(void)
{
    RUN_TEST(test_session_start_success);
    RUN_TEST(test_session_start_timeout);
    RUN_TEST(test_session_start_rejected);
    RUN_TEST(test_session_start_unexpected_packet);
    RUN_TEST(test_session_start_retry_then_success);
    RUN_TEST(test_session_send_increments_seq);
    RUN_TEST(test_session_recv_accepts_valid_seq);
    RUN_TEST(test_session_recv_rejects_replay);
    RUN_TEST(test_session_recv_rejects_seq_too_far);
    RUN_TEST(test_session_recv_accepts_max_delta);
    RUN_TEST(test_session_recv_crc_error);
    RUN_TEST(test_session_recv_version_mismatch);
    RUN_TEST(test_session_cancel_flag);
    RUN_TEST(test_session_send_cancel);
    RUN_TEST(test_recv_timeout_no_data);
    RUN_TEST(test_recv_timeout_with_short_deadline);
    RUN_TEST(test_recv_raw_timeout_mid_packet);
    RUN_TEST(test_session_start_purge_called);
    RUN_TEST(test_session_start_max_retries_exhausted);
    RUN_TEST(test_session_send_binary_includes_face_api_prefix);
    RUN_TEST(test_session_recv_payload_size_exceeds_buffer);
    RUN_TEST(test_session_recv_raw_zero_payload);
    RUN_TEST(test_session_start_cancel_cleared);
}
