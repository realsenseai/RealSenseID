/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Unit tests for rsid_device_controller.c — text-mode parsers and ping. */

#include "unity.h"
#include "mock_platform.h"
#include <string.h>

static rsid_ctx_t ctx;
static mock_state_t mock;

static void init(void)
{
    mock_init(&ctx, &mock);
}

/* ---- Temperature ---- */

/* Verify parsing of positive SoC and board temperatures */
static void test_temperature_positive(void)
{
    float soc = 0, board = 0;
    init();
    mock_set_text_response(&mock, "SoC temperature: 42.5\r\nBoard temperature: 38.1\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_get_temperature(&ctx, &soc, &board));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 42.5f, soc);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 38.1f, board);
}

/* Verify parsing of negative temperature values */
static void test_temperature_negative(void)
{
    float soc = 0, board = 0;
    init();
    mock_set_text_response(&mock, "SoC temperature: -5.0\r\nBoard temperature: -10.2\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_get_temperature(&ctx, &soc, &board));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -5.0f, soc);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, -10.2f, board);
}

/* Verify parsing of zero temperature values */
static void test_temperature_zero(void)
{
    float soc = 99, board = 99;
    init();
    mock_set_text_response(&mock, "SoC temperature: 0.0\r\nBoard temperature: 0.0\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_get_temperature(&ctx, &soc, &board));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, soc);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, board);
}

/* Verify partial response returns SoC temp, board defaults to 0 */
static void test_temperature_partial(void)
{
    float soc = 0, board = 99;
    init();
    mock_set_text_response(&mock, "SoC temperature: 42.5\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_get_temperature(&ctx, &soc, &board));
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 42.5f, soc);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, board);
}

/* Verify temperature query returns error on send failure */
static void test_temperature_send_failure(void)
{
    float soc = 0, board = 0;
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_get_temperature(&ctx, &soc, &board));
}

/* ---- Firmware version ---- */

/* Verify FW version parses multiple lines, skips 0.0.0.0 */
static void test_firmware_version_parse(void)
{
    char output[256];
    init();
    mock_set_text_response(&mock, "OPFW : 4.2.0.1\r\nNNFW : 3.1.0.0\r\nRECOG : 0.0.0.0\r\nFLOW : 2.0.1.0\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
    /* RECOG skipped (0.0.0.0), others present, no \r in output */
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW:4.2.0.1"));
    TEST_ASSERT_NOT_NULL(strstr(output, "NNFW:3.1.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "FLOW:2.0.1.0"));
    TEST_ASSERT_NULL(strstr(output, "RECOG"));
    TEST_ASSERT_NULL(strchr(output, '\r')); /* no embedded \r */
}

/* Verify version parser handles real device output with filenames after version */
static void test_firmware_version_real_device(void)
{
    char output[512];
    init();
    mock_set_text_response(&mock, "+-----------------------------------------------+\r\n"
                                  "| RealSense F500  @ RSID F500\r\n"
                                  "| F/W:88000008h compiled at 15:06:30, Mar 25 2026\r\n"
                                  "| BSP    : SDK_USBCAM-01.12.00 r5 66b4fe63\r\n"
                                  "| basefw : BSP-9.12.0 r91200 8989ed6a\r\n"
                                  "+-----------------------------------------------+\r\n"
                                  "# boot files (BOOT.INI)\r\n"
                                  "OPFW : 2.12.0.136 SBC.2.12.0.136.BIN\r\n"
                                  "DNET : 26.2.25.0 DNET.26.2.25.0.SBIN\r\n"
                                  "RECOG : 0.0.0.0 RECOG.0.0.0.0.SBIN\r\n"
                                  "NNLED : 19.8.25.0 NNLED.19.8.25.0.SBIN\r\n"
                                  "cdc>\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
    /* Only modules with digit-only versions, skip 0.0.0.0 and non-numeric (BSP, basefw) */
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW:2.12.0.136"));
    TEST_ASSERT_NOT_NULL(strstr(output, "DNET:26.2.25.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "NNLED:19.8.25.0"));
    TEST_ASSERT_NULL(strstr(output, "RECOG"));  /* 0.0.0.0 skipped */
    TEST_ASSERT_NULL(strstr(output, "BSP"));    /* non-numeric version skipped */
    TEST_ASSERT_NULL(strstr(output, "basefw")); /* non-numeric version skipped */
    TEST_ASSERT_NULL(strstr(output, ".BIN"));   /* filename not included */
    TEST_ASSERT_NULL(strstr(output, ".SBIN"));  /* filename not included */
    TEST_ASSERT_NULL(strchr(output, '\r'));     /* no embedded \r */
}

/* Verify FW version returns error when no data received */
static void test_firmware_version_empty(void)
{
    char output[256];
    init();
    mock.recv_fail = 1; /* no data at all */
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_query_firmware_version(&ctx, output, sizeof(output)));
}

/* ---- Serial number ---- */

/* Verify serial number extracted from bracketed response */
static void test_serial_number_parse(void)
{
    char output[128];
    init();
    mock_set_text_response(&mock, "SN : [ABC123DEF]\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_serial_number(&ctx, output, sizeof(output)));
    TEST_ASSERT_EQUAL_STRING("ABC123DEF", output);
}

/* Verify serial number fails when brackets are missing */
static void test_serial_number_missing_bracket(void)
{
    char output[128];
    init();
    mock_set_text_response(&mock, "SN : ABC123\r\n");
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_serial_number(&ctx, output, sizeof(output)));
}

/* ---- OTP version ---- */

/* Verify OTP version parsed from text response */
static void test_otp_version_parse(void)
{
    uint8_t ver = 0;
    init();
    mock_set_text_response(&mock, "otp version is 3\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_otp_version(&ctx, &ver));
    TEST_ASSERT_EQUAL_UINT8('3', ver); /* raw ASCII char, matches C++ SDK */
}

/* Verify OTP version returns error on unrecognized response */
static void test_otp_version_missing(void)
{
    uint8_t ver = 0;
    init();
    mock_set_text_response(&mock, "some random text\r\n");
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_otp_version(&ctx, &ver));
}

/* ---- Color gains ---- */

/* Verify color gains parsed from bracketed [red blue] format */
static void test_color_gains_parse(void)
{
    int red = 0, blue = 0;
    init();
    mock_set_text_response(&mock, "[128 256]\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_get_color_gains(&ctx, &red, &blue));
    TEST_ASSERT_EQUAL_INT(128, red);
    TEST_ASSERT_EQUAL_INT(256, blue);
}

/* Verify color gains returns error on malformed response */
static void test_color_gains_malformed(void)
{
    int red = 0, blue = 0;
    init();
    mock_set_text_response(&mock, "[128]\r\n");
    TEST_ASSERT_EQUAL(RSID_Error, rsid_get_color_gains(&ctx, &red, &blue));
}

/* ---- Ping ---- */

/* Verify ping succeeds when device returns matching timestamp */
static void test_ping_success(void)
{
    uint8_t wire[512];
    uint32_t wire_len;
    uint32_t timestamp;

    init();
    mock.time_ms = 1000;

    /* Ping sends a timestamp, device returns the same data for verification. */
    timestamp = 1000; /* matches mock time */
    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'p', &timestamp, sizeof(timestamp), 0);
    TEST_ASSERT_TRUE(wire_len > 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_EQUAL(RSID_Ok, rsid_ping(&ctx));
}

/* Verify ping fails when returned timestamp does not match */
static void test_ping_echo_mismatch(void)
{
    uint8_t wire[512];
    uint32_t wire_len;
    uint32_t wrong_timestamp = 9999;

    init();
    mock.time_ms = 1000;

    wire_len = mock_build_wire_packet(wire, sizeof(wire), 'p', &wrong_timestamp, sizeof(wrong_timestamp), 0);
    TEST_ASSERT_TRUE(wire_len > 0);
    mock_set_recv_data(&mock, wire, wire_len);

    TEST_ASSERT_EQUAL(RSID_Error, rsid_ping(&ctx));
}

/* ---- Firmware version: truncated output ---- */

/* Verify small output buffer drops entries that don't fit, without overrun.
 * "OPFW:4.2.0.1" is 12 chars + NUL = 13. Buffer of 15 fits the first entry
 * but not "OPFW:4.2.0.1|NNFW:3.1.0.0" (25 chars). Second entry is dropped. */
static void test_firmware_version_output_truncated(void)
{
    char output[15];
    init();
    mock_set_text_response(&mock, "OPFW : 4.2.0.1\r\nNNFW : 3.1.0.0\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
    TEST_ASSERT_TRUE(strlen(output) < sizeof(output));    /* fits within buffer */
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW:4.2.0.1")); /* first entry fits */
    TEST_ASSERT_NULL(strstr(output, "NNFW"));             /* second entry dropped */
}

/* Verify the parser handles 6+ modules, each separated by '|'.
 * Exercises the pipe-separator logic and confirms nothing is dropped. */
static void test_firmware_version_many_modules(void)
{
    char output[512];
    init();
    mock_set_text_response(&mock, "OPFW : 1.0.0.0\r\nNNFW : 2.0.0.0\r\nDNET : 3.0.0.0\r\n"
                                  "FLOW : 4.0.0.0\r\nNNLED : 5.0.0.0\r\nSPOOF : 6.0.0.0\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW:1.0.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "NNFW:2.0.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "DNET:3.0.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "FLOW:4.0.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "NNLED:5.0.0.0"));
    TEST_ASSERT_NOT_NULL(strstr(output, "SPOOF:6.0.0.0"));
    /* All 6 modules separated by '|' means 5 separators */
    {
        int pipes = 0;
        const char* p = output;
        while (*p)
        {
            if (*p == '|')
                pipes++;
            p++;
        }
        TEST_ASSERT_EQUAL_INT(5, pipes);
    }
}

/* Verify version parser stops at the first non-digit, non-dot character.
 * "4.2.abc" → only "4.2." is extracted as the version string. */
static void test_firmware_version_non_digit_version(void)
{
    char output[256];
    init();
    mock_set_text_response(&mock, "OPFW : 4.2.abc\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW:4.2."));
}

/* Verify the parser trims leading tabs/spaces from module names.
 * "\t  OPFW  : 1.2.3.4" should produce "OPFW:1.2.3.4" in the output. */
static void test_firmware_version_whitespace_variants(void)
{
    char output[256];
    init();
    mock_set_text_response(&mock, "\t  OPFW  : 1.2.3.4\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_firmware_version(&ctx, output, sizeof(output)));
    TEST_ASSERT_NOT_NULL(strstr(output, "OPFW:1.2.3.4"));
}

/* Verify FW version returns serial error when send fails */
static void test_firmware_version_send_fails(void)
{
    char output[256];
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_query_firmware_version(&ctx, output, sizeof(output)));
}

/* ---- Serial number: truncated output ---- */

/* Verify serial number is safely truncated when output buffer is smaller than SN.
 * SN="ABCDEFGHIJKLMN" (14 chars) into a 5-byte buffer → "ABCD\0". */
static void test_serial_number_output_truncated(void)
{
    char output[5];
    init();
    mock_set_text_response(&mock, "SN : [ABCDEFGHIJKLMN]\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_query_serial_number(&ctx, output, sizeof(output)));
    TEST_ASSERT_EQUAL_UINT32(4, strlen(output)); /* truncated to 4 chars */
    TEST_ASSERT_EQUAL_STRING("ABCD", output);    /* correct prefix */
    TEST_ASSERT_EQUAL_CHAR('\0', output[4]);     /* NUL at boundary */
}

/* Verify serial number returns serial error when send fails */
static void test_serial_number_send_fails(void)
{
    char output[128];
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_query_serial_number(&ctx, output, sizeof(output)));
}

/* ---- Temperature: NULL pointer ---- */

/* Verify temperature query returns error when soc_temp pointer is NULL */
static void test_temperature_null_soc(void)
{
    float board = 0;
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_get_temperature(&ctx, NULL, &board));
}

/* ---- OTP version: send fails ---- */

/* Verify OTP version returns serial error when send fails */
static void test_otp_version_send_fails(void)
{
    uint8_t ver = 0;
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_query_otp_version(&ctx, &ver));
}

/* ---- Color gains: send fails and negative values ---- */

/* Verify color gains returns serial error when send fails */
static void test_color_gains_send_fails(void)
{
    int red = 0, blue = 0;
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_get_color_gains(&ctx, &red, &blue));
}

/* Verify strtol inside the parser handles negative values (e.g., "[-10 300]").
 * Negative gains aren't valid hardware values, but the parser shouldn't crash. */
static void test_color_gains_negative_values(void)
{
    int red = 0, blue = 0;
    init();
    mock_set_text_response(&mock, "[-10 300]\r\n");
    TEST_ASSERT_EQUAL(RSID_Ok, rsid_get_color_gains(&ctx, &red, &blue));
    TEST_ASSERT_EQUAL_INT(-10, red);
    TEST_ASSERT_EQUAL_INT(300, blue);
}

/* ---- Ping: recv timeout and NULL ctx ---- */

/* Verify ping returns error when the device doesn't respond (recv buffer empty).
 * Exercises the recv-timeout path in rsid_ping. */
static void test_ping_recv_timeout(void)
{
    init();
    /* No recv data loaded — recv will fail immediately */
    TEST_ASSERT_NOT_EQUAL(RSID_Ok, rsid_ping(&ctx));
}

/* Verify ping returns error with NULL context */
static void test_ping_null_ctx(void)
{
    TEST_ASSERT_EQUAL(RSID_Error, rsid_ping(NULL));
}

/* ---- BSP version: send fails and NULL output ---- */

/* Verify BSP version returns serial error when send fails */
static void test_bsp_version_send_fails(void)
{
    char output[256];
    init();
    mock.send_fail = 1;
    TEST_ASSERT_EQUAL(RSID_SerialError, rsid_query_bsp_version(&ctx, output, sizeof(output)));
}

/* Verify BSP version returns error with NULL output buffer */
static void test_bsp_version_null_output(void)
{
    init();
    TEST_ASSERT_EQUAL(RSID_Error, rsid_query_bsp_version(&ctx, NULL, 100));
}

/* ---- Runner ---- */

void test_device_controller_run(void)
{
    RUN_TEST(test_temperature_positive);
    RUN_TEST(test_temperature_negative);
    RUN_TEST(test_temperature_zero);
    RUN_TEST(test_temperature_partial);
    RUN_TEST(test_temperature_send_failure);
    RUN_TEST(test_firmware_version_parse);
    RUN_TEST(test_firmware_version_real_device);
    RUN_TEST(test_firmware_version_empty);
    RUN_TEST(test_firmware_version_output_truncated);
    RUN_TEST(test_firmware_version_many_modules);
    RUN_TEST(test_firmware_version_non_digit_version);
    RUN_TEST(test_firmware_version_whitespace_variants);
    RUN_TEST(test_firmware_version_send_fails);
    RUN_TEST(test_serial_number_parse);
    RUN_TEST(test_serial_number_missing_bracket);
    RUN_TEST(test_serial_number_output_truncated);
    RUN_TEST(test_serial_number_send_fails);
    RUN_TEST(test_temperature_null_soc);
    RUN_TEST(test_otp_version_parse);
    RUN_TEST(test_otp_version_missing);
    RUN_TEST(test_otp_version_send_fails);
    RUN_TEST(test_color_gains_parse);
    RUN_TEST(test_color_gains_malformed);
    RUN_TEST(test_color_gains_send_fails);
    RUN_TEST(test_color_gains_negative_values);
    RUN_TEST(test_ping_success);
    RUN_TEST(test_ping_echo_mismatch);
    RUN_TEST(test_ping_recv_timeout);
    RUN_TEST(test_ping_null_ctx);
    RUN_TEST(test_bsp_version_send_fails);
    RUN_TEST(test_bsp_version_null_output);
}
