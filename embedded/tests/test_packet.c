/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Unit tests for rsid_packet.c — packet construction, CRC, field accessors. */

#include "unity.h"
#include "rsid.h"
#include "rsid_packet.h"
#include <string.h>

/* ---- Packet init ---- */

/* Verify init sets sync bytes, protocol version, and defaults */
static void test_packet_init_sets_sync_and_version(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init(&pkt);
    TEST_ASSERT_EQUAL_CHAR('@', pkt.header.sync1);
    TEST_ASSERT_EQUAL_CHAR('F', pkt.header.sync2);
    TEST_ASSERT_EQUAL_UINT8(3, pkt.header.protocol_ver);
    TEST_ASSERT_EQUAL_CHAR('-', pkt.header.msg_id);
    TEST_ASSERT_EQUAL_UINT16(0, pkt.header.payload_size);
}

/* Verify init zeroes the entire payload region */
static void test_packet_init_zeroes_payload(void)
{
    rsid_serial_packet_t pkt;
    memset(&pkt, 0xFF, sizeof(pkt));
    rsid_packet_init_fa(&pkt, 'A', NULL, 0);
    /* init_fa zeroes payload + 32-byte CRC region (payload_size + HMAC_SIZE) */
    TEST_ASSERT_EQUAL_UINT32(0, pkt.payload.sequence_number);
    TEST_ASSERT_EQUAL_CHAR('\0', pkt.payload.message.fa_msg.user_id[0]);
}

/* ---- FA packet ---- */

/* Verify FA packet sets msg_id, user_id, and status */
static void test_packet_init_fa_basic(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init_fa(&pkt, 'E', "Alice", 0);
    TEST_ASSERT_EQUAL_CHAR('E', pkt.header.msg_id);
    TEST_ASSERT_EQUAL_STRING("Alice", rsid_packet_get_user_id(&pkt));
    TEST_ASSERT_EQUAL_CHAR('0', pkt.payload.message.fa_msg.fa_status);
}

/* Verify FA packet handles NULL user_id without crash */
static void test_packet_init_fa_null_user_id(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init_fa(&pkt, 'A', NULL, 0);
    TEST_ASSERT_EQUAL_CHAR('A', pkt.header.msg_id);
    /* user_id should be all zeros */
    TEST_ASSERT_EQUAL_CHAR('\0', pkt.payload.message.fa_msg.user_id[0]);
}

/* Verify FA packet stores max-length user_id with NUL terminator */
static void test_packet_init_fa_max_length_user_id(void)
{
    rsid_serial_packet_t pkt;
    char name[RSID_MAX_USER_ID + 1];
    memset(name, 'X', RSID_MAX_USER_ID);
    name[RSID_MAX_USER_ID] = '\0';

    rsid_packet_init_fa(&pkt, 'E', name, 0);
    TEST_ASSERT_EQUAL_STRING(name, rsid_packet_get_user_id(&pkt));
    /* NUL at position 30 */
    TEST_ASSERT_EQUAL_CHAR('\0', pkt.payload.message.fa_msg.user_id[RSID_MAX_USER_ID]);
}

/* Verify FA status encodes as ASCII digit and round-trips */
static void test_packet_init_fa_status_encoding(void)
{
    rsid_serial_packet_t pkt;
    int i;
    for (i = 0; i < 10; i++)
    {
        rsid_packet_init_fa(&pkt, 'A', NULL, (char)i);
        TEST_ASSERT_EQUAL_CHAR('0' + i, pkt.payload.message.fa_msg.fa_status);
        TEST_ASSERT_EQUAL_INT(i, (int)rsid_packet_get_status_code(&pkt));
    }
}

/* Verify FA reserved bytes are filled with '0' */
static void test_packet_init_fa_reserved_filled(void)
{
    rsid_serial_packet_t pkt;
    int i;
    rsid_packet_init_fa(&pkt, 'E', "Bob", 0);
    for (i = 0; i < RSID_FA_RESERVED_SIZE; i++)
        TEST_ASSERT_EQUAL_CHAR('0', pkt.payload.message.fa_msg.reserved[i]);
}

/* ---- Data packet ---- */

/* Verify data packet stores msg_id and payload bytes */
static void test_packet_init_data_basic(void)
{
    rsid_serial_packet_t pkt;
    const char data[] = "0123456789";
    rsid_packet_init_data(&pkt, 'o', data, 10);
    TEST_ASSERT_EQUAL_CHAR('o', pkt.header.msg_id);
    TEST_ASSERT_EQUAL_MEMORY(data, pkt.payload.message.data_msg.data, 10);
}

/* Verify data packet handles NULL data pointer without crash */
static void test_packet_init_data_null(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init_data(&pkt, 'p', NULL, 0);
    TEST_ASSERT_EQUAL_CHAR('p', pkt.header.msg_id);
    /* should not crash */
}

/* Verify data packet clamps oversized length to max */
static void test_packet_init_data_clamps_to_max(void)
{
    rsid_serial_packet_t pkt;
    /* Try to init with more than RSID_DATA_MSG_SIZE — should clamp */
    rsid_packet_init_data(&pkt, 'o', NULL, RSID_DATA_MSG_SIZE + 100);
    /* payload_size should be based on clamped size, not the original */
    TEST_ASSERT_TRUE(pkt.header.payload_size <= sizeof(pkt.payload));
}

/* ---- Payload alignment ---- */

/* Verify FA payload size is 32-byte aligned (44 -> 64) */
static void test_fa_payload_size_is_32_aligned(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init_fa(&pkt, 'A', "test", 0);
    TEST_ASSERT_EQUAL_UINT16(0, pkt.header.payload_size % 32);
    /* seq(4) + fa_msg(40) = 44 -> aligned to 64 */
    TEST_ASSERT_EQUAL_UINT16(64, pkt.header.payload_size);
}

/* ---- CRC ---- */

/* Verify CRC is deterministic for the same packet */
static void test_crc_deterministic(void)
{
    rsid_serial_packet_t pkt;
    uint16_t crc1, crc2;
    rsid_packet_init_fa(&pkt, 'A', "Alice", 0);
    crc1 = rsid_packet_calc_crc(&pkt);
    crc2 = rsid_packet_calc_crc(&pkt);
    TEST_ASSERT_EQUAL_UINT16(crc1, crc2);
    TEST_ASSERT_NOT_EQUAL(0, crc1);
}

/* Verify CRC changes when payload is mutated */
static void test_crc_changes_on_mutation(void)
{
    rsid_serial_packet_t pkt;
    uint16_t crc_before, crc_after;
    rsid_packet_init_fa(&pkt, 'A', "Alice", 0);
    crc_before = rsid_packet_calc_crc(&pkt);
    pkt.payload.message.fa_msg.user_id[0] = 'Z';
    crc_after = rsid_packet_calc_crc(&pkt);
    TEST_ASSERT_NOT_EQUAL(crc_before, crc_after);
}

/* Verify CRC is non-zero even for an empty init'd packet */
static void test_crc_nonzero_for_empty_packet(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init(&pkt);
    /* CRC init value is 0x1d0f, so even an empty packet has non-zero CRC */
    TEST_ASSERT_NOT_EQUAL(0, rsid_packet_calc_crc(&pkt));
}

/* ---- Max data payload ---- */

/* Verify a max-sized data packet (8124 bytes) produces a valid, 32-aligned payload_size
 * that fits within the packet struct. Guards against overflow in align_to_32(). */
static void test_packet_max_data_payload(void)
{
    rsid_serial_packet_t pkt;
    rsid_packet_init_data(&pkt, 'o', NULL, RSID_DATA_MSG_SIZE);
    TEST_ASSERT_TRUE(pkt.header.payload_size <= sizeof(pkt.payload));
    TEST_ASSERT_EQUAL_UINT16(0, pkt.header.payload_size % 32);
}

/* Verify data packet with exactly RSID_DATA_MSG_SIZE bytes stores all data without clamping.
 * Uses static buffer to avoid ~8 KB on the stack (embedded-friendly habit). */
static void test_packet_init_data_exact_max(void)
{
    rsid_serial_packet_t pkt;
    static uint8_t data[RSID_DATA_MSG_SIZE]; /* static: avoid 8 KB stack alloc */
    memset(data, 0xAB, sizeof(data));
    data[0] = 0x11;                      /* sentinel at start */
    data[RSID_DATA_MSG_SIZE - 1] = 0xEE; /* sentinel at end */

    rsid_packet_init_data(&pkt, 'o', (const char*)data, RSID_DATA_MSG_SIZE);

    /* Both sentinels must survive — proves no clamping or truncation */
    TEST_ASSERT_EQUAL_UINT8(0x11, (uint8_t)pkt.payload.message.data_msg.data[0]);
    TEST_ASSERT_EQUAL_UINT8(0xEE, (uint8_t)pkt.payload.message.data_msg.data[RSID_DATA_MSG_SIZE - 1]);
}

/* Verify alignment at every boundary near 32-byte multiples.
 * Also confirms that an empty data packet (size=0) gets a 32-byte payload
 * because the 4-byte sequence_number still needs room. */
static void test_payload_alignment_boundary_values(void)
{
    rsid_serial_packet_t pkt;
    uint32_t sizes[] = {0, 1, 10, 31, 32, 33, 63, 64, 100};
    int i;
    for (i = 0; i < (int)(sizeof(sizes) / sizeof(sizes[0])); i++)
    {
        rsid_packet_init_data(&pkt, 'o', NULL, sizes[i]);
        TEST_ASSERT_EQUAL_UINT16(0, pkt.header.payload_size % 32);
    }

    /* Empty data (size=0): seq_number(4 bytes) rounds up to 32 */
    rsid_packet_init_data(&pkt, 'o', NULL, 0);
    TEST_ASSERT_EQUAL_UINT16(32, pkt.header.payload_size);
}

/* Simulate a malicious/buggy device filling all 31 user_id bytes with 'X' (no NUL).
 * rsid_packet_get_user_id() must force a NUL at position 30 so strlen is bounded. */
static void test_packet_get_user_id_force_nul_terminates(void)
{
    rsid_serial_packet_t pkt;
    const char* uid;

    rsid_packet_init_fa(&pkt, 'A', "test", 0);
    memset(pkt.payload.message.fa_msg.user_id, 'X', RSID_MAX_USER_ID + 1); /* overwrite NUL */

    uid = rsid_packet_get_user_id(&pkt);
    TEST_ASSERT_EQUAL_UINT32(RSID_MAX_USER_ID, (uint32_t)strlen(uid)); /* exactly 30, not 31+ */
}

/* Prove that CRC covers the HMAC region, not just header+payload.
 *
 * On the wire, CRC is computed over: header(22B) + payload(payload_size) + hmac(32B).
 * In the struct, rsid_packet_calc_crc reads contiguous bytes starting at &pkt,
 * so the "wire hmac" starts at offset sizeof(header) + payload_size.
 * For an FA packet (payload_size=64) that's byte 86, which falls inside the
 * payload union — but it's the HMAC region on the wire.
 *
 * Mutating that byte and seeing a different CRC proves the HMAC is checksummed. */
static void test_crc_changes_on_hmac_mutation(void)
{
    rsid_serial_packet_t pkt;
    uint16_t crc_before, crc_after;
    uint8_t* raw;
    size_t hmac_offset;

    rsid_packet_init_fa(&pkt, 'A', "Alice", 0);
    crc_before = rsid_packet_calc_crc(&pkt);

    /* Flip a byte in the wire-format HMAC region */
    raw = (uint8_t*)&pkt;
    hmac_offset = sizeof(pkt.header) + pkt.header.payload_size;
    raw[hmac_offset] ^= 0xFF;

    crc_after = rsid_packet_calc_crc(&pkt);
    TEST_ASSERT_NOT_EQUAL(crc_before, crc_after);
}

/* ---- Runner ---- */

void test_packet_run(void)
{
    RUN_TEST(test_packet_init_sets_sync_and_version);
    RUN_TEST(test_packet_init_zeroes_payload);
    RUN_TEST(test_packet_init_fa_basic);
    RUN_TEST(test_packet_init_fa_null_user_id);
    RUN_TEST(test_packet_init_fa_max_length_user_id);
    RUN_TEST(test_packet_init_fa_status_encoding);
    RUN_TEST(test_packet_init_fa_reserved_filled);
    RUN_TEST(test_packet_init_data_basic);
    RUN_TEST(test_packet_init_data_null);
    RUN_TEST(test_packet_init_data_clamps_to_max);
    RUN_TEST(test_fa_payload_size_is_32_aligned);
    RUN_TEST(test_crc_deterministic);
    RUN_TEST(test_crc_changes_on_mutation);
    RUN_TEST(test_crc_nonzero_for_empty_packet);
    RUN_TEST(test_packet_max_data_payload);
    RUN_TEST(test_packet_init_data_exact_max);
    RUN_TEST(test_payload_alignment_boundary_values);
    RUN_TEST(test_packet_get_user_id_force_nul_terminates);
    RUN_TEST(test_crc_changes_on_hmac_mutation);
}
