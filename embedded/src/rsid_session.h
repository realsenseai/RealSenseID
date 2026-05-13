/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/*
 * rsid_session.h — Session-layer protocol for RealSenseID serial communication.
 *
 * A "session" wraps a sequence of packet exchanges with the device. The flow:
 *
 *   1. Host sends "\r\n__FACE_API__\r\n" (switches device to binary packet mode)
 *   2. Host sends a StartSession('o') data packet
 *   3. Device echoes StartSession('o') on success, or Reply('Y') on rejection
 *   4. Host and device exchange command/response packets with incrementing seq numbers
 *   5. Session ends when device sends a Reply('Y') terminal packet, or host cancels
 *
 * Every send prepends "\r\n__FACE_API__\r\n" before the binary packet. This is how
 * the device's UART console distinguishes binary protocol traffic from text commands.
 *
 * Cancel is sent as raw text: "\r\n__FACE_CANCEL__\r\n" (no packet framing).
 *
 * Sequence numbers provide replay protection: recv validates that each incoming
 * seq_number is strictly increasing and within RSID_MAX_SEQ_DELTA (20) of the
 * last received value.
 */

#ifndef RSID_SESSION_H
#define RSID_SESSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define RSID_DEFAULT_RECV_TIMEOUT_MS  5000 /* General recv timeout */
#define RSID_SESSION_START_TIMEOUT_MS 4000 /* Timeout waiting for session start echo */
#define RSID_MAX_SEQ_DELTA            20   /* Max allowed gap in sequence numbers (anti-replay) */
#define RSID_KEEP_ALIVE_INTERVAL_MS   4000 /* Host->Device keep-alive in auth-loop mode */

    struct rsid_ctx; /* forward declaration — defined in rsid.h */

    /* Serial-level status codes (internal) */
    typedef enum
    {
        RSID_SERIAL_OK = 0,
        RSID_SERIAL_SEND_FAILED,
        RSID_SERIAL_RECV_TIMEOUT,
        RSID_SERIAL_RECV_FAILED,
        RSID_SERIAL_VERSION_MISMATCH,
        RSID_SERIAL_CRC_ERROR,
        RSID_SERIAL_SECURITY_ERROR,
        RSID_SERIAL_UNEXPECTED_PACKET
    } rsid_serial_status;

    /** Start a session: handshake with device. Retries up to RSID_SESSION_MAX_RETRIES times. */
    rsid_serial_status rsid_session_start(struct rsid_ctx* ctx);

    /** Send a packet (increments sequence number, computes CRC). */
    rsid_serial_status rsid_session_send(struct rsid_ctx* ctx);

    /** Receive a packet with timeout (validates sync, protocol, CRC, sequence number). */
    rsid_serial_status rsid_session_recv(struct rsid_ctx* ctx, uint32_t timeout_ms);

    /** Send cancel command (raw text, no packet framing). */
    rsid_serial_status rsid_session_send_cancel(struct rsid_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif /* RSID_SESSION_H */
