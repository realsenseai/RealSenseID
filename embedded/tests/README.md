# Unit Tests

Host-based unit tests for the embedded C SDK using the [Unity](https://github.com/ThrowTheSwitch/Unity) test framework.

## Build & Run

```bash
cd embedded
cmake -B build_test -DRSID_EMBEDDED_TESTS=ON
cmake --build build_test --config Debug
ctest --test-dir build_test --output-on-failure
```

Or run directly:

```bash
build_test/tests/Debug/rsid_embedded_tests.exe   # Windows
build_test/tests/rsid_embedded_tests              # Linux
```

Tests are off by default (`RSID_EMBEDDED_TESTS=OFF`) and do not affect the production library or CLI builds.

## Test Files

| File | Tests | What it covers |
|------|-------|----------------|
| `test_packet.c` | 20 | Packet init, FA/data construction, CRC-16, field accessors, 32-byte alignment, max payload, NUL termination |
| `test_session.c` | 23 | Session handshake, retry/purge/sleep, sequence validation, CRC/version errors, cancel flag, timeouts |
| `test_authenticator.c` | 14 | `rsid_init` validation, user ID validation (NULL, empty, too long) |
| `test_device_controller.c` | 31 | Text parsers (temperature, FW version, serial number, OTP, color gains), ping, NULL inputs, truncation |
| `test_robustness.c` | 32 | Malformed packets, garbage on UART, send/recv failures, status mapping boundaries, NULL callbacks, data parsing edge cases |
| `test_api.c` | 86 | End-to-end: enroll, authenticate, auth loop, user management, config round-trip, power management, cancel, NULL validation |

## Mock Platform

`mock_platform.h/c` provides a fake `rsid_platform_t` for deterministic testing without hardware:

- **send**: records all sent bytes into `send_buf` for verification
- **recv**: feeds canned bytes from `recv_buf`; returns -1 when exhausted (simulates timeout)
- **get_time_ms**: returns `time_ms` (frozen unless advanced by `sleep_ms` or overridden per-test)
- **sleep_ms**: advances the mock timer by the requested duration
- **purge/log**: count calls; log records last message

Key helpers for building device responses:

- `mock_build_wire_packet()` — constructs a valid wire-format packet (with correct CRC) into a caller-provided buffer
- `mock_append_wire_packet()` — appends a data packet (lowercase msg_id) to the recv buffer
- `mock_append_fa_packet()` — appends an FA packet (uppercase msg_id) with explicit user_id and status
- `mock_set_recv_data()` — loads raw bytes into recv (used for text-mode command responses)

## Adding Tests

1. Add test functions to the appropriate `test_*.c` file
2. Register them with `RUN_TEST(test_name)` in that file's `test_*_run()` function
3. Rebuild — no other changes needed

Typical test pattern for binary-protocol APIs (enroll, authenticate, config, etc.):

```c
static void test_something(void)
{
    init();                                                     /* reset ctx + mock */
    mock.recv_len = 0;
    mock.recv_pos = 0;
    mock_append_wire_packet(&mock, 'o', NULL, 0, 0);           /* StartSession response */
    mock_append_fa_packet(&mock, 'Y', NULL, (char)RSID_Ok, 1); /* device reply */

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_some_api(&ctx, ...));
}
```

For text-mode commands (FW version, temperature, serial number), use `mock_set_recv_data()` with a raw string instead:

```c
static void test_fw_version(void)
{
    char output[256];
    init();
    mock_set_recv_data(&mock, (const uint8_t*)"OPFW : 4.2.0.1\r\n", 17);
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
}
```
