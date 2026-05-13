/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/*
 * rsid_session.c — Session-layer protocol implementation.
 *
 * Session start handshake:
 *   Host                          Device
 *   ────                          ──────
 *   "\r\n__FACE_API__\r\n"  -->
 *   StartSession('o') pkt   -->
 *                            <--  StartSession('o') echo  (success)
 *                            <--  Reply('Y')              (rejection)
 *   (retry up to 3x with 250ms delay, sending __FACE_CANCEL__ between retries)
 *
 * Send protocol (rsid_session_send):
 *   1. Increment session sequence number
 *   2. Set pkt->payload.sequence_number
 *   3. Transmit: __FACE_API__ + header+payload + hmac + crc
 *
 * Recv protocol (rsid_session_recv):
 *   1. Check cancel flag (if set, send __FACE_CANCEL__ and return)
 *   2. Scan byte stream for '@' 'F' sync pair
 *   3. Read and validate protocol_ver == 3
 *   4. Read remaining header, payload, hmac, crc
 *   5. Validate CRC-16/AUG-CCITT over header+payload+hmac
 *   6. Validate sequence_number: must be > last_recv and <= last_recv + 20
 *
 * The wire format splits transmission into: __FACE_API__ text, then header+payload
 * bytes, then hmac (32 bytes), then crc (2 bytes). This matches the C++ PacketSender.
 */

#include "rsid_common.h"
#include <string.h>

/* Text commands sent as raw UART bytes — no packet framing.
 * Declared as arrays (not pointers) so sizeof() gives string length + 1. */
static const char FACE_API_CMD[] = "\r\n__FACE_API__\r\n";       /* Switch device to binary mode */
static const char FACE_CANCEL_CMD[] = "\r\n__FACE_CANCEL__\r\n"; /* Abort current operation */

/* Sleep using platform callback */
static void rsid_delay_ms(rsid_platform_t* platform, uint32_t ms)
{
    platform->sleep_ms(ms, platform->app_ctx);
}

/* Send raw bytes via platform */
rsid_serial_status rsid_send_raw(rsid_platform_t* platform, const char* data, uint32_t len)
{
    if (platform->send((const uint8_t*)data, len, platform->app_ctx) != 0)
        return RSID_SERIAL_SEND_FAILED;
    return RSID_SERIAL_OK;
}

/*
 * Send a complete packet over the wire in 4 parts:
 *   1. __FACE_API__ text prefix (tells device to expect binary)
 *   2. header + payload bytes (contiguous, payload_size determines length)
 *   3. hmac (32 bytes, zeroed in non-secure mode)
 *   4. CRC-16 (2 bytes, computed over parts 2+3)
 */
rsid_serial_status rsid_send_packet(rsid_platform_t* platform, rsid_serial_packet_t* pkt)
{
    rsid_serial_status status;
    uint32_t face_api_len = (uint32_t)(sizeof(FACE_API_CMD) - 1);

    /* Send __FACE_API__ text command */
    status = rsid_send_raw(platform, FACE_API_CMD, face_api_len);
    if (status != RSID_SERIAL_OK)
        return status;

    /* Send header + payload */
    {
        const char* pkt_ptr = (const char*)pkt;
        uint32_t pkt_size = (uint32_t)(sizeof(pkt->header) + pkt->header.payload_size);
        status = rsid_send_raw(platform, pkt_ptr, pkt_size);
        if (status != RSID_SERIAL_OK)
            return status;
    }

    /* Send hmac */
    status = rsid_send_raw(platform, pkt->hmac, sizeof(pkt->hmac));
    if (status != RSID_SERIAL_OK)
        return status;

    /* Compute and send CRC */
    {
        uint16_t crc = rsid_packet_calc_crc(pkt);
        status = rsid_send_raw(platform, (const char*)&crc, sizeof(crc));
    }

    return status;
}

/*
 * Scan the byte stream for the '@' 'F' sync pair that marks packet start.
 * Discards any garbage bytes (e.g. text console output) before the sync.
 * Returns RSID_SERIAL_OK when both sync bytes are found, or RECV_TIMEOUT
 * if the deadline expires.
 */
static rsid_serial_status wait_sync_bytes(rsid_platform_t* platform, uint32_t start_ms, uint32_t timeout_ms)
{
    uint8_t byte;
    while (1)
    {
        uint32_t elapsed = platform->get_time_ms(platform->app_ctx) - start_ms;
        if (elapsed >= timeout_ms)
            return RSID_SERIAL_RECV_TIMEOUT;

        if (platform->recv(&byte, 1, timeout_ms - elapsed, platform->app_ctx) != 0)
            return RSID_SERIAL_RECV_TIMEOUT;

        if ((char)byte == RSID_SYNC1)
        {
            elapsed = platform->get_time_ms(platform->app_ctx) - start_ms;
            if (elapsed >= timeout_ms)
                return RSID_SERIAL_RECV_TIMEOUT;

            if (platform->recv(&byte, 1, timeout_ms - elapsed, platform->app_ctx) != 0)
                return RSID_SERIAL_RECV_TIMEOUT;

            if ((char)byte == RSID_SYNC2)
                return RSID_SERIAL_OK;
        }
    }
}


/* Returns remaining ms from deadline, or 0 if expired */
static uint32_t remaining_ms(rsid_platform_t* platform, uint32_t start_ms, uint32_t timeout_ms)
{
    uint32_t elapsed = platform->get_time_ms(platform->app_ctx) - start_ms;
    return (timeout_ms > elapsed) ? (timeout_ms - elapsed) : 0;
}

/*
 * Receive a complete packet from the wire (no sequence number validation).
 * Steps: sync scan -> protocol check -> header -> payload -> hmac -> crc -> CRC verify.
 * Used by both session_recv (which adds seq validation) and session_start (raw).
 */
rsid_serial_status rsid_recv_packet(rsid_platform_t* platform, rsid_serial_packet_t* pkt, uint32_t timeout_ms)
{
    rsid_serial_status status;
    uint32_t start_ms = platform->get_time_ms(platform->app_ctx);
    uint32_t remaining;
    uint16_t expected_crc;

    /* Wait for sync bytes */
    status = wait_sync_bytes(platform, start_ms, timeout_ms);
    if (status != RSID_SERIAL_OK)
        return status;

    pkt->header.sync1 = RSID_SYNC1;
    pkt->header.sync2 = RSID_SYNC2;

    /* Read protocol version */
    remaining = remaining_ms(platform, start_ms, timeout_ms);
    if (remaining == 0)
        return RSID_SERIAL_RECV_TIMEOUT;
    if (platform->recv(&pkt->header.protocol_ver, 1, remaining, platform->app_ctx) != 0)
        return RSID_SERIAL_RECV_FAILED;

    if (pkt->header.protocol_ver != RSID_PROTOCOL_VER)
        return RSID_SERIAL_VERSION_MISMATCH;

    /* Read rest of header (after sync1 + sync2 + protocol_ver = 3 bytes) */
    remaining = remaining_ms(platform, start_ms, timeout_ms);
    if (remaining == 0)
        return RSID_SERIAL_RECV_TIMEOUT;
    {
        char* header_rest = (char*)pkt + 3;
        uint32_t rest_size = (uint32_t)(sizeof(pkt->header) - 3);
        if (platform->recv((uint8_t*)header_rest, rest_size, remaining, platform->app_ctx) != 0)
            return RSID_SERIAL_RECV_FAILED;
    }

    /* Validate payload size */
    if (pkt->header.payload_size > sizeof(pkt->payload))
        return RSID_SERIAL_RECV_FAILED;

    /* Zero payload + trailing 32 bytes that CRC covers (see rsid_packet_calc_crc).
     * CRC is computed over contiguous bytes: header + payload_size + HMAC_SIZE.
     * In the struct, the HMAC field is at a fixed offset past the ~8KB payload union,
     * so the CRC actually reads payload padding — not pkt->hmac. Both sides must agree
     * these bytes are zero. This zeroes only the CRC-relevant region, not the full 8KB. */
    memset(&pkt->payload, 0, pkt->header.payload_size + RSID_HMAC_SIZE);

    /* Read payload */
    if (pkt->header.payload_size > 0)
    {
        remaining = remaining_ms(platform, start_ms, timeout_ms);
        if (remaining == 0)
            return RSID_SERIAL_RECV_TIMEOUT;
        if (platform->recv((uint8_t*)&pkt->payload, pkt->header.payload_size, remaining, platform->app_ctx) != 0)
            return RSID_SERIAL_RECV_FAILED;
    }

    /* Read hmac */
    remaining = remaining_ms(platform, start_ms, timeout_ms);
    if (remaining == 0)
        return RSID_SERIAL_RECV_TIMEOUT;
    if (platform->recv((uint8_t*)pkt->hmac, sizeof(pkt->hmac), remaining, platform->app_ctx) != 0)
        return RSID_SERIAL_RECV_FAILED;

    /* Read CRC */
    remaining = remaining_ms(platform, start_ms, timeout_ms);
    if (remaining == 0)
        return RSID_SERIAL_RECV_TIMEOUT;
    if (platform->recv((uint8_t*)&pkt->crc, sizeof(pkt->crc), remaining, platform->app_ctx) != 0)
        return RSID_SERIAL_RECV_FAILED;

    /* Validate CRC */
    expected_crc = rsid_packet_calc_crc(pkt);
    if (expected_crc != pkt->crc)
        return RSID_SERIAL_CRC_ERROR;

    return RSID_SERIAL_OK;
}

/* Single session start attempt: send StartSession, wait for echo.
 * Matches C++ NonSecureSession::Start logic. */
static rsid_serial_status start_session_impl(rsid_platform_t* platform, rsid_serial_packet_t* pkt)
{
    rsid_serial_status status;

    rsid_packet_init_data(pkt, RSID_MSGID_START_SESSION, NULL, 0);
    status = rsid_send_packet(platform, pkt);
    if (status != RSID_SERIAL_OK)
        return status;

    status = rsid_recv_packet(platform, pkt, RSID_SESSION_START_TIMEOUT_MS);
    if (status != RSID_SERIAL_OK)
        return status;

    return (pkt->header.msg_id == RSID_MSGID_START_SESSION) ? RSID_SERIAL_OK : RSID_SERIAL_UNEXPECTED_PACKET;
}

#define RSID_SESSION_MAX_RETRIES    3
#define RSID_SESSION_RETRY_DELAY_MS 250

rsid_serial_status rsid_session_start(rsid_ctx_t* ctx)
{
    rsid_platform_t* platform = &ctx->platform;
    rsid_serial_packet_t* pkt = ctx_packet(ctx);
    rsid_serial_status status;
    int tries;

    ctx->_internal.cancel_requested = 0;
    ctx->_internal.last_sent_seq = 0;
    ctx->_internal.last_recv_seq = 0;

    for (tries = 0; tries < RSID_SESSION_MAX_RETRIES; tries++)
    {
        status = start_session_impl(platform, pkt);
        if (status == RSID_SERIAL_OK)
        {
            RSID_DBG(platform, "session_start ok");
            return RSID_SERIAL_OK;
        }

        /* Clean up before retry: cancel previous session, wait, purge stale data */
        RSID_DBG(platform, "session_start attempt %d failed, retrying", tries + 1);
        rsid_session_send_cancel(ctx);
        rsid_delay_ms(platform, RSID_SESSION_RETRY_DELAY_MS);
        platform->purge(platform->app_ctx);
    }

    RSID_DBG(platform, "session_start failed");
    return status;
}

rsid_serial_status rsid_session_send(rsid_ctx_t* ctx)
{
    rsid_serial_status status;
    rsid_serial_packet_t* pkt;

    if (!validate_ctx(ctx))
        return RSID_SERIAL_SEND_FAILED;

    pkt = ctx_packet(ctx);
    if (pkt == NULL)
        return RSID_SERIAL_SEND_FAILED;

    /* Increment sequence number */
    ctx->_internal.last_sent_seq++;
    pkt->payload.sequence_number = ctx->_internal.last_sent_seq;

    /* Send __FACE_API__ + packet */
    status = rsid_send_packet(&ctx->platform, pkt);
    if (status == RSID_SERIAL_OK)
        RSID_DBG(&ctx->platform, "session_send seq=%u", ctx->_internal.last_sent_seq);
    return status;
}

rsid_serial_status rsid_session_recv(rsid_ctx_t* ctx, uint32_t timeout_ms)
{
    rsid_serial_status status;
    uint32_t current_seq;

    status = rsid_recv_packet(&ctx->platform, ctx_packet(ctx), timeout_ms);
    if (status != RSID_SERIAL_OK)
    {
        RSID_DBG(&ctx->platform, "session_recv failed (status=%d)", (int)status);
        return status;
    }

    /*
     * Validate sequence number for replay protection:
     * - Must be strictly greater than last received (prevents replay)
     * - Must not jump more than 20 ahead (prevents far-future injection)
     * A delta of 20 allows for some lost/dropped packets without breaking the session.
     */
    current_seq = ctx_packet(ctx)->payload.sequence_number;
    if (current_seq <= ctx->_internal.last_recv_seq || current_seq > ctx->_internal.last_recv_seq + RSID_MAX_SEQ_DELTA)
    {
        RSID_DBG(&ctx->platform, "sequence number out of range");
        return RSID_SERIAL_SECURITY_ERROR;
    }

    ctx->_internal.last_recv_seq = current_seq;
    RSID_DBG(&ctx->platform, "session_recv msg='%c' seq=%u", (char)ctx_packet(ctx)->header.msg_id, current_seq);
    return RSID_SERIAL_OK;
}

rsid_serial_status rsid_session_send_cancel(rsid_ctx_t* ctx)
{
    RSID_DBG(&ctx->platform, "session_send_cancel");
    return rsid_send_raw(&ctx->platform, FACE_CANCEL_CMD, (uint32_t)(sizeof(FACE_CANCEL_CMD) - 1));
}
