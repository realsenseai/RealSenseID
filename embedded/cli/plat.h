/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

#ifndef RSID_CLI_PLAT_H
#define RSID_CLI_PLAT_H

#include <stdint.h>

/* Set to 1 at runtime (via -v flag) to enable verbose I/O tracing */
extern int g_plat_verbose;

/* Cancel pending I/O on the serial handle (unblocks a plat_recv waiting in another thread).
 * Windows: CancelIoEx. Linux: self-pipe wakes select. */
void plat_cancel_io(void);

/* Platform transport and timing — implemented per-platform in plat_win32.c / plat_linux.c */
int plat_send(const uint8_t* data, uint32_t len, void* app_ctx);
int plat_recv(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx);
void plat_purge(void* app_ctx);
uint32_t plat_get_time_ms(void* app_ctx);
void plat_sleep_ms(uint32_t ms, void* app_ctx);
int open_serial(const char* port);
void close_serial(void);

#endif /* RSID_CLI_PLAT_H */
