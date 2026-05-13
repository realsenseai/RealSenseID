/* License: Apache 2.0. See LICENSE file in root directory. */
/* Copyright(c) 2026 RealSense, Inc. All Rights Reserved. */

/* Unity test runner for the embedded SDK. */

#include "unity.h"

/* Unity requires global setUp/tearDown — keep empty, each test handles its own. */
void setUp(void)
{
}
void tearDown(void)
{
}

/* Test suite runners declared in each test file */
extern void test_packet_run(void);
extern void test_session_run(void);
extern void test_authenticator_run(void);
extern void test_device_controller_run(void);
extern void test_robustness_run(void);
extern void test_api_run(void);

int main(void)
{
    UNITY_BEGIN();
    test_packet_run();
    test_session_run();
    test_authenticator_run();
    test_device_controller_run();
    test_robustness_run();
    test_api_run();
    return UNITY_END();
}
