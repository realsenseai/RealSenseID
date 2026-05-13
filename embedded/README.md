# RealSenseID Embedded C SDK

Pure C99 host SDK for RealSenseID face authentication devices. Designed for resource-constrained MCU hosts such as STM32, NXP i.MX RT, and similar embedded platforms.

## Features

- **Pure C99** — requires only the C standard library (`string.h`, `stdio.h`, `stdlib.h`, `stdint.h`). Little-endian platforms only
- **No dynamic allocation** — all state fits in a single ~9 KB static context struct
- **No OS dependencies** — you provide 6 callback functions for UART, timing, and sleep
- **Low stack usage** — API calls use ~300–400 bytes of stack (worst case: auth with landmarks callback). Debug builds (`-DRSID_DEBUG=1`) add up to 128 bytes per log call
- **Standalone** — just copy the `src/` folder into your project

## Supported Operations

| Category | Functions |
|----------|-----------|
| Authentication | `rsid_enroll`, `rsid_authenticate`, `rsid_authenticate_loop`, `rsid_cancel` |
| User Management | `rsid_remove_user`, `rsid_remove_all`, `rsid_query_user_ids`, `rsid_query_number_of_users` |
| Device Config | `rsid_set_device_config`, `rsid_query_device_config` |
| Device Control | `rsid_ping`, `rsid_query_firmware_version`, `rsid_query_bsp_version`, `rsid_query_serial_number`, `rsid_query_otp_version`, `rsid_reboot`, `rsid_standby`, `rsid_hibernate`, `rsid_unlock` |
| Device Diagnostics | `rsid_get_temperature`, `rsid_get_color_gains`, `rsid_set_color_gains` |

## Quick Start

### 1. Add files to your project

Copy the `src/` folder into your project. Add all `.c` files to your build and `src/` to your include path. The only header your code needs to include is `rsid.h`.

### 2. Implement the platform interface

The SDK communicates with the device over UART. You provide these platform callbacks:

```c
#include "rsid.h"

/* Send exactly `len` bytes. Return 0 on success, non-zero on error. */
static int my_send(const uint8_t* data, uint32_t len, void* app_ctx)
{
    (void)app_ctx;
    return HAL_UART_Transmit(&huart1, (uint8_t*)data, (uint16_t)len, 1000) == HAL_OK ? 0 : -1;
}

/* Receive exactly `len` bytes within timeout. Return 0 on success, non-zero on error/timeout. */
static int my_recv(uint8_t* data, uint32_t len, uint32_t timeout_ms, void* app_ctx)
{
    (void)app_ctx;
    return HAL_UART_Receive(&huart1, data, (uint16_t)len, timeout_ms) == HAL_OK ? 0 : -1;
}

/* Discard any stale bytes in the UART RX data register. */
static void my_purge(void* app_ctx)
{
    (void)app_ctx;
    while (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE))
        __HAL_UART_FLUSH_DRREGISTER(&huart1);
}

/* Return current time in milliseconds (monotonic). */
static uint32_t my_get_time_ms(void* app_ctx)
{
    (void)app_ctx;
    return HAL_GetTick();
}

/* Sleep for the given number of milliseconds. */
static void my_sleep_ms(uint32_t ms, void* app_ctx)
{
    (void)app_ctx;
    HAL_Delay(ms);
}

/* Optional: debug callback for diagnostics (only called when compiled with -DRSID_DEBUG=1). */
static void my_debug(const char* msg, void* app_ctx)
{
    (void)app_ctx;
    printf("[RSID] %s\n", msg);
}
```

### 3. Initialize and use

```c
#include "rsid.h"

/* Allocate context statically (~9 KB) */
static rsid_ctx_t ctx;

int main(void)
{
    /* Set up platform callbacks */
    ctx.platform.send = my_send;
    ctx.platform.recv = my_recv;
    ctx.platform.get_time_ms = my_get_time_ms;
    ctx.platform.sleep_ms = my_sleep_ms;
    ctx.platform.purge = my_purge;
    ctx.platform.debug = my_debug;     /* optional (only called with -DRSID_DEBUG=1) */
    ctx.platform.app_ctx = NULL;     /* passed to all callbacks */

    /* Initialize — call once at startup */
    if (rsid_init(&ctx) != RSID_Ok)
        return 1;

    /* Ping device */
    if (rsid_ping(&ctx) == RSID_Ok)
        printf("Device connected!\n");
}
```

### 4. Enroll a user

```c
static void on_enroll_result(rsid_enroll_status status, void* ctx)
{
    if (status == RSID_Enroll_Success)
        printf("Enrollment succeeded\n");
    else
        printf("Enrollment failed: %d\n", (int)status);
}

static void on_enroll_progress(rsid_face_pose pose, void* ctx)
{
    const char* names[] = {"Center", "Up", "Down", "Left", "Right"};
    printf("Look %s\n", names[pose]);
}

void enroll_user(void)
{
    rsid_enroll_callbacks_t cb = {0};
    cb.on_result = on_enroll_result;
    cb.on_progress = on_enroll_progress;

    rsid_enroll(&ctx, "John", &cb, NULL);
}
```

### 5. Authenticate

```c
static void on_auth_result(rsid_auth_status status, const char* user_id, short score, void* ctx)
{
    if (status == RSID_Auth_Success)
        printf("Welcome, %s! (score=%d)\n", user_id, (int)score);
    else
        printf("Authentication failed: %d\n", (int)status);
}

void authenticate(void)
{
    rsid_auth_callbacks_t cb = {0};
    cb.on_result = on_auth_result;

    rsid_authenticate(&ctx, &cb, NULL);
}
```

## Building

### CMake (for development/testing)

```bash
cmake -B build
cmake --build build
```

On Windows with Visual Studio:

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## UART Configuration

Connect to the RealSenseID device UART with these settings:

| Parameter | Value |
|-----------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Flow control | None |

## Debugging

Error handling uses return codes — every API function returns `rsid_status`. No hidden state, no get_last_error. This matches the standard embedded C approach (mbedTLS, lwIP, FreeRTOS).

Diagnostic output is optional and compile-time controlled. Compile with `-DRSID_DEBUG=1` to enable the `RSID_DBG` macro. Output is routed to the `platform.debug` callback you provide:

```c
static void my_debug(const char* msg, void* app_ctx) {
    printf("[DBG] %s\n", msg);
}
ctx.platform.debug = my_debug;
```

Logs API lifecycle (enroll/auth start/end), protocol events (hints, results, face detected), errors (unexpected reply, parse failure, timeout), and session state (send/recv with sequence numbers). Uses a 128-byte stack buffer per call.

When disabled (default), all `RSID_DBG` calls compile to `((void)0)` — zero overhead, no strings in the binary.

## Memory Footprint

| Item | Size |
|------|------|
| `rsid_ctx_t` (RAM) | ~9 KB |
| Stack per API call | ~300–400 bytes (worst case: auth with landmarks) |
| Stack with debug logging | +128 bytes per `RSID_DBG` call (`-DRSID_DEBUG=1`) |

The context struct contains an 8 KB packet buffer required by the wire protocol. Allocate it statically — do not put it on the stack.

## API Reference

See [`src/rsid.h`](src/rsid.h) for the full API — types, enums, callbacks, and all function signatures with documentation.

## CLI Test Tool

A CLI tool is included for testing on Windows and Linux (requires a device connected via USB-to-serial):

```bash
# Windows
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
build\Debug\rsid_embedded_cli.exe COM3

# Linux
cmake -B build && cmake --build build
./build/rsid_embedded_cli /dev/ttyACM0
```

The CLI uses a single-character interactive menu:

```
Choose an option ('?' for menu, 'q' to quit):
  'e' Enroll
  'a' Authenticate
  't' Authenticate loop
  'd' Delete all users
  'r' Remove user
  'u' List users
  's' Show device config
  'S' Set device config
  'x' Ping
  'v' Firmware version
  'n' Serial number
  'T' Temperature (F50x)
  'R' Reboot
```

## File Structure

```
embedded/
├── src/                        All source and headers (single include path)
│   ├── rsid.h                  Public API (single header — include this)
│   ├── rsid_common.h           Shared internal definitions
│   ├── rsid_packet.c/h         Packet framing, construction, and CRC-16
│   ├── rsid_session.c/h        Session protocol management
│   ├── rsid_authenticator.c    Auth/enroll/user/config operations
│   └── rsid_device_controller.c  Ping/version/power operations
├── cli/
│   └── rsid_embedded_cli.c              Windows + Linux CLI test tool
├── example/
│   ├── stm32_example.c                STM32 HAL integration snippet (platform callbacks, #if 0 reference)
│   └── stm32f407-discovery/           Full STM32F407G-Discovery CubeIDE project (F500 over UART)
├── CMakeLists.txt
└── README.md
```

## License

Apache 2.0. See LICENSE file in root directory.
