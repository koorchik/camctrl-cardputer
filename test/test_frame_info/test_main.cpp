#include <unity.h>
#include <cstring>

#include "logic/frame_info.h"
#include "logic/dhcp_server.h"

void setUp(void) {}
void tearDown(void) {}

void test_ipv4_source(void) {
    uint8_t f[60] = {};
    f[12] = 0x08;
    f[13] = 0x00;
    f[14] = 0x45;
    f[26] = 10; f[27] = 1; f[28] = 2; f[29] = 3;
    uint32_t ip = 0;
    TEST_ASSERT_TRUE(frameIpv4Sender(f, sizeof(f), &ip));
    TEST_ASSERT_EQUAL_HEX32(ipv4(10, 1, 2, 3), ip);
}

void test_arp_sender(void) {
    uint8_t f[60] = {};
    f[12] = 0x08;
    f[13] = 0x06;
    f[28] = 192; f[29] = 168; f[30] = 73; f[31] = 2;
    uint32_t ip = 0;
    TEST_ASSERT_TRUE(frameIpv4Sender(f, sizeof(f), &ip));
    TEST_ASSERT_EQUAL_HEX32(ipv4(192, 168, 73, 2), ip);
}

void test_zero_source_and_other_types_ignored(void) {
    uint8_t f[60] = {};
    f[12] = 0x08;
    f[14] = 0x45;
    uint32_t ip = 0;
    TEST_ASSERT_FALSE(frameIpv4Sender(f, sizeof(f), &ip));  // DHCP discover: 0.0.0.0
    f[12] = 0x86;
    f[13] = 0xDD;
    TEST_ASSERT_FALSE(frameIpv4Sender(f, sizeof(f), &ip));
    TEST_ASSERT_FALSE(frameIpv4Sender(f, 20, &ip));
}

void test_subnet(void) {
    const uint32_t net = ipv4(192, 168, 73, 0);
    const uint32_t mask = ipv4(255, 255, 255, 0);
    TEST_ASSERT_TRUE(ipv4InSubnet(ipv4(192, 168, 73, 2), net, mask));
    TEST_ASSERT_FALSE(ipv4InSubnet(ipv4(192, 168, 73, 0), net, mask));
    TEST_ASSERT_FALSE(ipv4InSubnet(ipv4(192, 168, 73, 255), net, mask));
    TEST_ASSERT_FALSE(ipv4InSubnet(ipv4(10, 0, 0, 5), net, mask));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ipv4_source);
    RUN_TEST(test_arp_sender);
    RUN_TEST(test_zero_source_and_other_types_ignored);
    RUN_TEST(test_subnet);
    return UNITY_END();
}
