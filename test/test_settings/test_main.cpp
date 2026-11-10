#include <unity.h>
#include <cstring>

#include "logic/settings.h"
#include "logic/hold_detector.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t MAC[6] = {0x24, 0x58, 0x7C, 0x01, 0xAB, 0x0F};

void test_defaults(void) {
    Settings s;
    defaultSettings(MAC, s);
    TEST_ASSERT_EQUAL_STRING("EthAir-AB0F", s.ssid);
    TEST_ASSERT_EQUAL_STRING("ethair123", s.password);
    TEST_ASSERT_EQUAL_UINT8(6, s.channel);
    TEST_ASSERT_EQUAL_STRING("", s.deviceHostname);  // mDNS fallback off
    TEST_ASSERT_NULL(validateSettings(s));
}

void test_ssid_rules(void) {
    TEST_ASSERT_NOT_NULL(validateSsid(""));
    TEST_ASSERT_NULL(validateSsid("a"));
    TEST_ASSERT_NULL(validateSsid("12345678901234567890123456789012"));
    TEST_ASSERT_NOT_NULL(validateSsid("123456789012345678901234567890123"));
    TEST_ASSERT_NOT_NULL(validateSsid("Камера"));
    TEST_ASSERT_NOT_NULL(validateSsid("tab\there"));
    TEST_ASSERT_NULL(validateSsid("My Cam <1> & \"x\""));
}

void test_password_rules(void) {
    TEST_ASSERT_NOT_NULL(validatePassword("1234567"));
    TEST_ASSERT_NULL(validatePassword("12345678"));
    char p63[64];
    memset(p63, 'x', 63);
    p63[63] = '\0';
    TEST_ASSERT_NULL(validatePassword(p63));
    char p64[65];
    memset(p64, 'x', 64);
    p64[64] = '\0';
    TEST_ASSERT_NOT_NULL(validatePassword(p64));
    TEST_ASSERT_NOT_NULL(validatePassword("пароль123"));
}

void test_channel_rules(void) {
    TEST_ASSERT_NOT_NULL(validateChannel(0));
    TEST_ASSERT_NULL(validateChannel(1));
    TEST_ASSERT_NULL(validateChannel(13));
    TEST_ASSERT_NOT_NULL(validateChannel(14));
}

void test_hostname_rules(void) {
    TEST_ASSERT_NULL(validateHostname("micro-studio-camera-4k-g2"));
    TEST_ASSERT_NULL(validateHostname("Cam1"));
    TEST_ASSERT_NULL(validateHostname(""));  // empty = off
    TEST_ASSERT_NOT_NULL(validateHostname("-cam"));
    TEST_ASSERT_NOT_NULL(validateHostname("cam-"));
    TEST_ASSERT_NOT_NULL(validateHostname("cam.local"));
    TEST_ASSERT_NOT_NULL(validateHostname("cam one"));
}

void test_hold_detector_fires_once_after_hold(void) {
    HoldDetector h(5000);
    TEST_ASSERT_FALSE(h.update(true, 1000));
    TEST_ASSERT_FALSE(h.update(true, 5999));
    TEST_ASSERT_EQUAL_UINT32(4999, h.heldMs(5999));
    TEST_ASSERT_TRUE(h.update(true, 6000));
    TEST_ASSERT_FALSE(h.update(true, 9000));
    TEST_ASSERT_FALSE(h.update(false, 9100));
    TEST_ASSERT_EQUAL_UINT32(0, h.heldMs(9100));
}

void test_hold_detector_short_presses_never_fire(void) {
    HoldDetector h(5000);
    for (uint32_t t = 0; t < 30000; t += 1000) {
        TEST_ASSERT_FALSE(h.update(true, t));
        TEST_ASSERT_FALSE(h.update(false, t + 500));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_ssid_rules);
    RUN_TEST(test_password_rules);
    RUN_TEST(test_channel_rules);
    RUN_TEST(test_hostname_rules);
    RUN_TEST(test_hold_detector_fires_once_after_hold);
    RUN_TEST(test_hold_detector_short_presses_never_fire);
    return UNITY_END();
}
