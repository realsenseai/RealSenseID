# CLAUDE.md — c_embedded (Pure C Embedded Host SDK)

## What This Is

Pure C99 reimplementation of the RealSenseID host protocol for resource-constrained MCU hosts (STM32, NXP RW series). Zero C++ runtime, zero dynamic allocation, zero OS dependencies. The user provides UART read/write via function pointers (`rsid_platform_t`).

Protocol is binary-compatible with the C++ host SDK — same packet format, same CRC, same MsgId values.

## Build

```bash
# Standalone (any platform with make + C99 compiler)
make                     # → librsid_embedded.a
make CC=arm-none-eabi-gcc  # cross-compile for ARM

# CMake (also builds CLI tool on Windows)
cmake -B build
cmake --build build --config Debug

# WSL verification (strict C99)
wsl -- bash -c "cd $(pwd) && make clean && make CFLAGS='-std=c99 -pedantic -Wall -Werror -O2'"
```

The CLI tool (`rsid_embedded_cli`) connects to a device over a serial port for interactive testing:
```
build\Debug\rsid_embedded_cli.exe COM3
> ping
> version
> enroll John
> auth
> users
```

## File Map

All source and headers live in `src/` (single include path). No `include/` vs `src/` split.

| File | Purpose | Ported from |
|------|---------|-------------|
| `src/rsid.h` | Single public header — platform, enums, config, ctx, all API functions | Merges platform + status + API |
| `src/rsid_common.h` | Shared internal definitions | New |
| `src/rsid_packet.c/h` | Packed packet structs, constructors, CRC-16 | `src/PacketManager/SerialPacket.h/.cc` + `Crc16.cc` |
| `src/rsid_session.c/h` | Session protocol: start, send, recv, cancel | `src/PacketManager/PacketSender.cc` + `NonSecureSession.cc` |
| `src/rsid_authenticator.c` | Enroll, auth, user mgmt, device config | `src/FaceAuthenticator/Impl/FaceAuthenticatorCommon.cc` |
| `src/rsid_device_controller.c` | Ping, FW version, reboot, power mgmt, serial number, OTP, temp, color gains | `src/DeviceController/DeviceControllerImpl.cc` |
| `cli/rsid_embedded_cli.c` | CLI test tool (Win32 + POSIX serial) | New |
| `cli/plat.h` | Platform transport interface, `DEBUG_PLAT` flag, `plat_cancel_io` | New |
| `cli/plat_win32.c` | Win32 overlapped serial I/O with log callback + `CancelIoEx` cancel | New |
| `cli/plat_linux.c` | POSIX serial I/O with log callback + self-pipe cancel | New |
| `example/stm32_example.c` | `#if 0`-guarded reference snippet showing how to wire `rsid_platform_t` to STM32 HAL | New |
| `example/stm32f407-discovery/` | STM32CubeIDE project (F407G-Disco + F500 over UART). Ships **`.ioc` only** — no vendored ST HAL or CMSIS. User regenerates `Drivers/` via CubeMX (`Project → Generate Code`) before first build. SDK sources linked to `../../src/` via Eclipse linked resources (`STM32CubeIDE/.project`) and `../../../../src` include path (`STM32CubeIDE/.cproject`). Renaming or moving files under `src/` requires updating both XML files. | New |

## API Scope

Enroll, Authenticate, AuthenticateLoop, Cancel, RemoveUser, RemoveAll, QueryUserIds, QueryNumberOfUsers, SetDeviceConfig, QueryDeviceConfig, Ping, QueryFirmwareVersion, QuerySerialNumber, QueryOtpVersion, GetTemperature, GetColorGains, SetColorGains, Reboot, Standby, Hibernate, Unlock.

**Not included**: preview, image-based enrollment/auth, faceprints extraction/matching, face detection (host-side), person/pose/barcode detection, FW update, secure mode (ECDSA).

### Planned: OnFaceCroppedImage (MsgId `'w'`)

Device sends a BGR24 face crop during auth/enroll as chunked data packets. Each chunk has a 10-byte header (`chunk_n:u16, width:u16, height:u16, timestamp:u32`) followed by up to 7,670 bytes of image data. Total image can be up to 900KB (`width * height * 3`).

**Agreed design: two-phase callback (Option B).**
1. `on_image_start(width, height, timestamp, ctx)` — user learns the image size, returns a `uint8_t*` buffer to fill (or NULL to skip).
2. `on_image_ready(buffer, width, height, timestamp, ctx)` — all chunks received, image is complete.

If user returns NULL from `on_image_start`, SDK receives and discards chunks (can't skip — they're in the stream). No SDK-side allocation; user owns the buffer. This is a "bigger host" feature (Pi, i.MX, Windows/Linux), not for small MCUs.

## Architecture

```
User code
  │
  ▼
rsid.h              ── single public header (platform, enums, config, ctx, API)
  │                     includes rsid_session.h for session types
  │
  ▼  (internal only — not included by user code)
rsid_common.h       ── shared helpers, includes rsid.h + rsid_packet.h
  │
  ├── rsid_session.h ── session protocol (start/send/recv/cancel + sequence numbers)
  └── rsid_packet.h  ── packet framing (8KB SerialPacket, CRC-16, sync bytes @F)
```

### Context Struct (~9 KB)

`rsid_ctx_t` contains platform callbacks and an `_internal` struct (session state + 8KB packet buffer). The packet buffer is opaque (`uint8_t[]`) — wire-format types are not exposed in the public header. Intended for **static allocation** — no malloc needed. Call `rsid_init()` once at startup.

```c
static rsid_ctx_t ctx;  /* allocate once, statically */
ctx.platform.send = my_send;
ctx.platform.recv = my_recv;
ctx.platform.get_time_ms = my_ms;
ctx.platform.sleep_ms = my_sleep;
if (rsid_init(&ctx) != RSID_Ok) { /* handle error */ }
```

### Wire Protocol Details

- **Packet**: `@F` sync + protocol_ver(3) + msg_id + iv[16] + payload_size(u16) + payload + hmac[32] + crc(u16)
- **Session start**: send `\r\n__FACE_API__\r\n` + StartSession('o') data packet, wait for echo; 3 retries, 250ms between
- **Send**: `\r\n__FACE_API__\r\n` + header+payload + hmac + crc; sequence number incremented per packet
- **Recv**: scan for `@F`, validate protocol_ver==3, read header+payload+hmac+crc, validate CRC, validate seq_number (delta <= 20)
- **Cancel**: send `\r\n__FACE_CANCEL__\r\n` raw text
- **Text commands** (no packet framing): `\r\nbspver\r\n` (version), `\r\nreset\r\n` (reboot), `\r\nsleep 1\r\n` (hibernate)
- **Timeouts**: session start 4s, auth 10s, enroll 12s, default recv 5s, keep-alive interval 4s, remove-all 30s
- **CRC**: CRC-16/AUG-CCITT, initial=0x1d0f, poly=0x1021, computed over header+payload+hmac

### MsgId Reference (chars used in this SDK)

| Char | Name | Direction |
|------|------|-----------|
| `'A'` | Authenticate | Host→Device |
| `'C'` | RemoveAllUsers | Host→Device |
| `'D'` | RemoveUser | Host→Device |
| `'E'` | Enroll | Host→Device |
| `'H'` | Hint | Device→Host |
| `'L'` | AuthenticateLoop | Host→Device |
| `'P'` | Progress / KeepAlive | Both |
| `'R'` | Result | Device→Host |
| `'U'` | Unlock | Host→Device |
| `'Y'` | Reply (terminal) | Device→Host |
| `'g'` | FaceDetected | Device→Host |
| `'n'` | GetNumberOfUsers | Both |
| `'o'` | StartSession | Both |
| `'p'` | Ping | Both |
| `'q'` | QueryDeviceConfig | Both |
| `'s'` | SetDeviceConfig | Both |
| `'t'` | StandBy | Host→Device |
| `'u'` | GetUserIds | Both |
| `'w'` | FaceCroppedImage | Device→Host (planned, not yet implemented) |

### AuthConfigPayload (60 bytes, packed)

Wire-format struct for Set/QueryDeviceConfig. Must stay in sync with `src/PacketManager/AuthConfigPayload.h`. The `gpio_auth_toggling` field uses 0x0b for enabled, 0x00 for disabled (not a simple bool).

## Logging & Debugging

### SDK Logging (src/)

Single mechanism controlled by one flag:

- **`RSID_DBG(platform, fmt, ...)`** — the sole logging macro. Compile with `-DRSID_DEBUG=1` to enable. Adds `[timestamp]` prefix via `snprintf` into a 128-byte stack buffer. Logs errors, API lifecycle, protocol events, hints, results, keep-alive, cancel. When disabled (default), compiles to `((void)0)` — zero overhead, no strings in binary.

### CLI Logging (cli/)

- **`fprintf(stderr, ...)`** — CLI platform files use stderr directly for errors. No callback indirection.
- **`PLAT_DBG(...)`** — CLI transport debug tracing, enabled at runtime with `-v` flag (`g_plat_verbose`).
- **`plat_cancel_io()`** — cancels pending I/O on the serial handle from another thread. Windows uses `CancelIoEx`, Linux uses a self-pipe to wake `select`.

## Rules

- **Pure C99, little-endian only**: No C++ features, no `//` comments in headers (use `/* */` for max portability), no VLAs, no compound literals in older compilers. The wire protocol assumes little-endian byte order — big-endian platforms are not supported. Test with `-std=c99 -pedantic`.
- **No dynamic allocation**: All state lives in `rsid_ctx_t`. No malloc, calloc, realloc, free.
- **No OS dependencies**: Timer comes from user's `get_time_ms()`. Serial from user's `send()`/`recv()`. No threads, no mutexes (except `volatile` cancel flag).
- **strncpy is intentional**: MSVC warns about it but embedded toolchains don't have `strncpy_s`. Suppress with `_CRT_SECURE_NO_WARNINGS` on MSVC.

### C++ Sync (CRITICAL)

This SDK must stay in sync with the C++ RealSenseID host SDK. Protocol-breaking drift is the highest-risk class of bug. Before any change, verify:

- **Enum values must match C++**: `rsid_status`, `rsid_auth_status`, `rsid_enroll_status` numeric values are used on the wire. Compare against `Status.h`, `AuthenticateStatus.h`, `EnrollStatus.h`. Changing them breaks protocol compatibility.
- **Packet struct layout must match C++**: `rsid_serial_packet_t` must be binary-identical to `PacketManager::SerialPacket`. `rsid_auth_config_payload_t` must match `AuthConfigPayload`. Payload must be 32-byte aligned. Run size checks after any struct change.
- **MsgId chars must match C++**: Every `RSID_MSGID_*` define must match the corresponding `MsgId` enum in `SerialPacket.h`.
- **Timeout values must match C++**: `RSID_ENROLL_TIMEOUT_MS`, `RSID_AUTH_TIMEOUT_MS`, `RSID_REMOVE_ALL_TIMEOUT_MS`, etc. must match `FaceAuthenticatorCommon` constants.
- **fa_to_* range checks**: `fa_to_status`, `fa_to_auth_status`, `fa_to_enroll_status`, `fa_to_face_pose` in `rsid_authenticator.c` use first/last enum members as bounds. When adding new enum values to `rsid.h`, verify these bounds still cover the full range.
- **Version must match Host SDK**: `RSID_EMBEDDED_VER_MAJOR/MINOR/PATCH` in `rsid.h` must match `RSID_VER_MAJOR/MINOR/PATCH` in `include/RealSenseID/Version.h`. CMake enforces this at configure time when built together.

## Gotchas

- `Makefile` is gitignored by a repo-level rule — must use `git add -f` to stage it.
- The 8KB packet buffer means `rsid_ctx_t` is ~9 KB. On Cortex-M0 with 8KB SRAM this won't fit — need at least 16KB+ RAM.
- `RSID_PACKET_BUF_SIZE` in `rsid.h` must match `sizeof(rsid_serial_packet_t)`. `rsid_common.h` enforces this with a compile-time static assert.
- `rsid_session_send()` always prefixes with `__FACE_API__` — this is how the device distinguishes binary protocol from text console commands.
- Ping doesn't use sessions (no sequence numbers). It sends `__FACE_API__` + packet directly via transport, and receives a raw echo.
- `QueryUserIds` is chunked (50 users per request) with a new session per chunk — same pattern as the C++ implementation.
- FW version query uses text mode (not binary packets) — sends `\r\nbspver\r\n` and parses line-by-line response until timeout. Replaced C++ `std::regex` with simple `strstr` parsing.

## Code Review Checklist

When reviewing this code, check **every** site mechanically — don't skip "simple" ones:

- **Every `memcpy` from packet data**: verify `payload_size` is checked before the copy. Device can send truncated packets.
- **Every `strncpy`/`strnlen` from packet data**: verify `cur_pos` is bounded by actual `payload_size`, not buffer capacity.
- **Every timeout**: verify it uses `(now - start) >= timeout` pattern, not `now >= deadline` (uint32_t rollover).
- **Every doc/README code snippet**: verify it matches the actual current API signatures.
- **Every `#define` constant**: verify it matches the corresponding C++ value.
- **No magic numbers**: every numeric literal must be a named `#define` or `sizeof` expression.
