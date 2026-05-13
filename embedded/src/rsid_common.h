/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/*
 * rsid_common.h -- Shared internal definitions for the embedded SDK.
 * Not part of the public API. Used by all .c files.
 */

#ifndef RSID_COMMON_H
#define RSID_COMMON_H

#include <stddef.h>
#include <string.h>

#include "rsid.h"
#include "rsid_session.h"
#include "rsid_packet.h"

/* Diagnostic logging macro — calls platform->debug callback.
 * Disabled by default. Compile with -DRSID_DEBUG=1 to enable.
 * When disabled, compiles to nothing (zero overhead, no strings in binary).
 * Uses ##__VA_ARGS__ (GNU/MSVC extension) and snprintf (<stdio.h>). */
#if defined(RSID_DEBUG) && RSID_DEBUG
#include <stdio.h>
#define RSID_DBG_BUF_SIZE 128
#define RSID_DBG(platform, fmt, ...)                                                                                                       \
    do                                                                                                                                     \
    {                                                                                                                                      \
        if ((platform) && (platform)->debug)                                                                                               \
        {                                                                                                                                  \
            char _dbg_buf[RSID_DBG_BUF_SIZE];                                                                                              \
            snprintf(_dbg_buf, sizeof(_dbg_buf), fmt, ##__VA_ARGS__);                                                                      \
            (platform)->debug(_dbg_buf, (platform)->app_ctx);                                                                              \
        }                                                                                                                                  \
    } while (0)
#else
#define RSID_DBG(platform, fmt, ...) ((void)0)
#endif

/*
 * Compile-time assert for C99. If 'cond' is false, the compiler rejects the
 * negative array size and the build fails.
 * __LINE__ is appended so multiple asserts can appear in the same file without name collisions.
 */
#define RSID_CONCAT_(a, b)       a##b
#define RSID_CONCAT(a, b)        RSID_CONCAT_(a, b)
#define RSID_STATIC_ASSERT(cond) typedef char RSID_CONCAT(static_assert_, __LINE__)[(cond) ? 1 : -1]

/* Verify RSID_PACKET_BUF_SIZE in rsid.h equals the actual packet struct size */
RSID_STATIC_ASSERT(sizeof(rsid_serial_packet_t) == RSID_PACKET_BUF_SIZE);

/* strnlen is POSIX, not C99. Use our own to avoid portability issues. */
static inline size_t rsid_strnlen(const char* s, size_t maxlen)
{
    size_t i;
    for (i = 0; i < maxlen && s[i] != '\0'; i++)
        ;
    return i;
}

/* Validate that ctx and all mandatory platform callbacks are set.
 * Returns 1 if valid, 0 if not. Logs the reason on failure. */
static inline int validate_ctx(rsid_ctx_t* ctx)
{
    if (ctx == NULL)
        return 0;
    if (ctx->platform.send == NULL || ctx->platform.recv == NULL || ctx->platform.get_time_ms == NULL || ctx->platform.sleep_ms == NULL ||
        ctx->platform.purge == NULL)
    {
        RSID_DBG(&ctx->platform, "missing mandatory platform callback");
        return 0;
    }
    return 1;
}

/* Access the internal packet buffer as rsid_serial_packet_t */
static inline rsid_serial_packet_t* ctx_packet(rsid_ctx_t* ctx)
{
    if (ctx == NULL)
        return NULL;
    return (rsid_serial_packet_t*)ctx->_internal.packet_buf;
}

/* Map internal serial status to public rsid_status */
static inline rsid_status serial_to_status(rsid_serial_status ss)
{
    switch (ss)
    {
    case RSID_SERIAL_OK:
        return RSID_Ok;
    case RSID_SERIAL_VERSION_MISMATCH:
        return RSID_VersionMismatch;
    case RSID_SERIAL_CRC_ERROR:
        return RSID_CrcError;
    case RSID_SERIAL_SECURITY_ERROR:
        return RSID_SecurityError;
    default:
        return RSID_SerialError;
    }
}

/* Transport-level functions (no session state — no sequence numbers) */
rsid_serial_status rsid_send_raw(rsid_platform_t* platform, const char* data, uint32_t len);
rsid_serial_status rsid_send_packet(rsid_platform_t* platform, rsid_serial_packet_t* pkt);
rsid_serial_status rsid_recv_packet(rsid_platform_t* platform, rsid_serial_packet_t* pkt, uint32_t timeout_ms);

#endif /* RSID_COMMON_H */
