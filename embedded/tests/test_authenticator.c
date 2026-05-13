/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Unit tests for rsid_authenticator.c — init validation, user_id validation. */

#include "unity.h"
#include "mock_platform.h"
#include <string.h>

static rsid_ctx_t ctx;
static mock_state_t mock;

/* Dummy callbacks for testing that rsid_init rejects missing callbacks */
static int dummy_send(const uint8_t* d, uint32_t l, void* u)
{
    (void)d;
    (void)l;
    (void)u;
    return 0;
}
static int dummy_recv(uint8_t* d, uint32_t l, uint32_t t, void* u)
{
    (void)d;
    (void)l;
    (void)t;
    (void)u;
    return -1;
}
static uint32_t dummy_get_time(void* u)
{
    (void)u;
    return 0;
}
static void dummy_sleep(uint32_t ms, void* u)
{
    (void)ms;
    (void)u;
}
static void dummy_purge(void* u)
{
    (void)u;
}

/* Fill all mandatory platform callbacks so a single NULL-out tests one field. */
static void fill_all_callbacks(rsid_ctx_t* c)
{
    memset(c, 0, sizeof(*c));
    c->platform.send = dummy_send;
    c->platform.recv = dummy_recv;
    c->platform.get_time_ms = dummy_get_time;
    c->platform.sleep_ms = dummy_sleep;
    c->platform.purge = dummy_purge;
}

/* ---- rsid_version ---- */

static void test_version_returns_string(void)
{
    const char* v = rsid_version();
    TEST_ASSERT_NOT_NULL(v);
    /* Must match RSID_EMBEDDED_VER_MAJOR.MINOR.PATCH */
    TEST_ASSERT_EQUAL_STRING("3.3.0", v);
}

/* ---- rsid_init ---- */

/* Verify rsid_init rejects NULL context pointer */
static void test_init_null_ctx(void)
{
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(NULL));
}

/* Verify rsid_init rejects missing send callback */
static void test_init_null_send(void)
{
    fill_all_callbacks(&ctx);
    ctx.platform.send = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify rsid_init rejects missing recv callback */
static void test_init_null_recv(void)
{
    fill_all_callbacks(&ctx);
    ctx.platform.recv = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify rsid_init rejects missing get_time_ms callback */
static void test_init_null_get_time_ms(void)
{
    fill_all_callbacks(&ctx);
    ctx.platform.get_time_ms = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify rsid_init rejects missing sleep_ms callback */
static void test_init_null_sleep_ms(void)
{
    fill_all_callbacks(&ctx);
    ctx.platform.sleep_ms = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify rsid_init rejects missing purge callback */
static void test_init_null_purge(void)
{
    fill_all_callbacks(&ctx);
    ctx.platform.purge = NULL;
    TEST_ASSERT_EQUAL(RSID_Error, rsid_init(&ctx));
}

/* Verify rsid_init succeeds and zeroes session state */
static void test_init_success(void)
{
    mock_init(&ctx, &mock);
    /* mock_init calls rsid_init internally — verify state is zeroed */
    TEST_ASSERT_EQUAL_UINT32(0, ctx._internal.last_sent_seq);
    TEST_ASSERT_EQUAL_UINT32(0, ctx._internal.last_recv_seq);
    TEST_ASSERT_EQUAL_UINT8(0, ctx._internal.cancel_requested);
}

/* ---- User ID validation (via rsid_enroll / rsid_remove_user) ---- */

/* Verify enroll rejects NULL context pointer */
static void test_enroll_null_ctx(void)
{
    TEST_ASSERT_EQUAL(RSID_Error, rsid_enroll(NULL, "Alice", NULL, NULL));
}

/* Verify enroll rejects NULL user_id */
static void test_enroll_null_user_id(void)
{
    mock_init(&ctx, &mock);
    TEST_ASSERT_EQUAL(RSID_Error, rsid_enroll(&ctx, NULL, NULL, NULL));
}

/* Verify enroll rejects empty string user_id */
static void test_enroll_empty_user_id(void)
{
    mock_init(&ctx, &mock);
    TEST_ASSERT_EQUAL(RSID_Error, rsid_enroll(&ctx, "", NULL, NULL));
}

/* Verify enroll rejects user_id exceeding RSID_MAX_USER_ID */
static void test_enroll_too_long_user_id(void)
{
    char name[RSID_MAX_USER_ID + 2];
    mock_init(&ctx, &mock);
    memset(name, 'X', RSID_MAX_USER_ID + 1);
    name[RSID_MAX_USER_ID + 1] = '\0';
    TEST_ASSERT_EQUAL(RSID_Error, rsid_enroll(&ctx, name, NULL, NULL));
}

/* Verify remove_user rejects NULL user_id */
static void test_remove_user_null_user_id(void)
{
    mock_init(&ctx, &mock);
    TEST_ASSERT_EQUAL(RSID_Error, rsid_remove_user(&ctx, NULL));
}

/* Verify remove_user rejects empty string user_id */
static void test_remove_user_empty_user_id(void)
{
    mock_init(&ctx, &mock);
    TEST_ASSERT_EQUAL(RSID_Error, rsid_remove_user(&ctx, ""));
}

/* ---- Runner ---- */

void test_authenticator_run(void)
{
    RUN_TEST(test_version_returns_string);
    RUN_TEST(test_init_null_ctx);
    RUN_TEST(test_init_null_send);
    RUN_TEST(test_init_null_recv);
    RUN_TEST(test_init_null_get_time_ms);
    RUN_TEST(test_init_null_sleep_ms);
    RUN_TEST(test_init_null_purge);
    RUN_TEST(test_init_success);
    RUN_TEST(test_enroll_null_ctx);
    RUN_TEST(test_enroll_null_user_id);
    RUN_TEST(test_enroll_empty_user_id);
    RUN_TEST(test_enroll_too_long_user_id);
    RUN_TEST(test_remove_user_null_user_id);
    RUN_TEST(test_remove_user_empty_user_id);
}
