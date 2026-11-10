#include <unity.h>

#include "logic/boot_guard.h"

void setUp(void) {}
void tearDown(void) {}

void test_garbage_state_is_reset_and_normal(void) {
    BootGuardState st{0x12345678, 99, 1, 1};
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash, 1));
    TEST_ASSERT_EQUAL_UINT32(BOOT_GUARD_MAGIC, st.magic);
    TEST_ASSERT_EQUAL_UINT32(0, st.crashCount);
}

void test_two_crashes_enter_safe_mode(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn, 1);
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash, 1));
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Crash, 1));
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Software, 1));
}

void test_stable_run_clears_crashes(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn, 1);
    bootGuardOnBoot(st, ResetKind::Crash, 1);
    bootGuardMarkStable(st);
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash, 1));
}

void test_power_on_clears_everything(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn, 1);
    bootGuardOnBoot(st, ResetKind::Crash, 1);
    bootGuardOnBoot(st, ResetKind::Crash, 1);
    bootGuardRequestSafe(st);
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::PowerOn, 1));
}

void test_safe_request_honoured_once(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn, 1);
    bootGuardRequestSafe(st);
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Software, 1));
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Software, 1));
}

void test_reflash_clears_crash_count(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn, 1);
    bootGuardOnBoot(st, ResetKind::Crash, 1);
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Crash, 1));
    // A new image after a serial flash (USB reset, not power-on) starts clean.
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Other, 2));
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash, 2));
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Crash, 2));
}

void test_safe_request_survives_reflash(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn, 1);
    bootGuardRequestSafe(st);
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Software, 2));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_garbage_state_is_reset_and_normal);
    RUN_TEST(test_two_crashes_enter_safe_mode);
    RUN_TEST(test_stable_run_clears_crashes);
    RUN_TEST(test_power_on_clears_everything);
    RUN_TEST(test_safe_request_honoured_once);
    RUN_TEST(test_reflash_clears_crash_count);
    RUN_TEST(test_safe_request_survives_reflash);
    return UNITY_END();
}
