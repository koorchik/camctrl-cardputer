#include <unity.h>

#include "logic/boot_guard.h"

void setUp(void) {}
void tearDown(void) {}

void test_garbage_state_is_reset_and_normal(void) {
    BootGuardState st{0x12345678, 99, 1};
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash));
    TEST_ASSERT_EQUAL_UINT32(BOOT_GUARD_MAGIC, st.magic);
    TEST_ASSERT_EQUAL_UINT32(0, st.crashCount);
}

void test_two_crashes_enter_safe_mode(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn);
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash));
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Crash));
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Software));
}

void test_stable_run_clears_crashes(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn);
    bootGuardOnBoot(st, ResetKind::Crash);
    bootGuardMarkStable(st);
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Crash));
}

void test_power_on_clears_everything(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn);
    bootGuardOnBoot(st, ResetKind::Crash);
    bootGuardOnBoot(st, ResetKind::Crash);
    bootGuardRequestSafe(st);
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::PowerOn));
}

void test_safe_request_honoured_once(void) {
    BootGuardState st{};
    bootGuardOnBoot(st, ResetKind::PowerOn);
    bootGuardRequestSafe(st);
    TEST_ASSERT_TRUE(bootGuardOnBoot(st, ResetKind::Software));
    TEST_ASSERT_FALSE(bootGuardOnBoot(st, ResetKind::Software));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_garbage_state_is_reset_and_normal);
    RUN_TEST(test_two_crashes_enter_safe_mode);
    RUN_TEST(test_stable_run_clears_crashes);
    RUN_TEST(test_power_on_clears_everything);
    RUN_TEST(test_safe_request_honoured_once);
    return UNITY_END();
}
