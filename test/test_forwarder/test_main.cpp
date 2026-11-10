#include <unity.h>
#include <cstring>

#include "logic/forwarder.h"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t LOCAL[6] = {0x02, 0xAA, 0xAA, 0xAA, 0xAA, 0x01};
static const uint8_t DEVICE[6] = {0x7C, 0x2E, 0x0D, 0x11, 0x22, 0x33};
static const uint8_t PHONE[6] = {0x3A, 0x10, 0x20, 0x30, 0x40, 0x50};
static const uint8_t BCAST[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t MDNS[6] = {0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB};

static uint8_t frame[64];

static const uint8_t* mk(const uint8_t dst[6], const uint8_t src[6]) {
    memset(frame, 0, sizeof(frame));
    memcpy(frame, dst, 6);
    memcpy(frame + 6, src, 6);
    return frame;
}

void test_runt_frame_goes_nowhere(void) {
    Forwarder f(LOCAL);
    TEST_ASSERT_EQUAL_UINT8(0, f.route(PORT_USB, mk(BCAST, DEVICE), 13, 0));
}

void test_broadcast_floods_all_other_ports(void) {
    Forwarder f(LOCAL);
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_WIFI) | portBit(PORT_LOCAL),
                            f.route(PORT_USB, mk(BCAST, DEVICE), 60, 0));
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_USB) | portBit(PORT_WIFI),
                            f.route(PORT_LOCAL, mk(BCAST, LOCAL), 60, 0));
}

void test_mdns_multicast_floods_all_other_ports(void) {
    Forwarder f(LOCAL);
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_USB) | portBit(PORT_LOCAL),
                            f.route(PORT_WIFI, mk(MDNS, PHONE), 60, 0));
}

void test_frame_for_local_mac_goes_only_local(void) {
    Forwarder f(LOCAL);
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_LOCAL), f.route(PORT_WIFI, mk(LOCAL, PHONE), 60, 0));
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_LOCAL), f.route(PORT_USB, mk(LOCAL, DEVICE), 60, 0));
}

void test_unknown_unicast_floods_but_not_local(void) {
    Forwarder f(LOCAL);
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_USB), f.route(PORT_WIFI, mk(DEVICE, PHONE), 60, 0));
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_USB) | portBit(PORT_WIFI),
                            f.route(PORT_LOCAL, mk(DEVICE, LOCAL), 60, 0));
}

void test_learned_unicast_goes_to_its_port(void) {
    Forwarder f(LOCAL);
    f.route(PORT_USB, mk(BCAST, DEVICE), 60, 0);
    f.route(PORT_WIFI, mk(BCAST, PHONE), 60, 0);
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_USB), f.route(PORT_LOCAL, mk(DEVICE, LOCAL), 60, 1));
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_WIFI), f.route(PORT_USB, mk(PHONE, DEVICE), 60, 1));
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_USB), f.route(PORT_WIFI, mk(DEVICE, PHONE), 60, 1));
}

void test_never_sends_back_to_ingress(void) {
    Forwarder f(LOCAL);
    f.route(PORT_USB, mk(BCAST, DEVICE), 60, 0);
    TEST_ASSERT_EQUAL_UINT8(0, f.route(PORT_USB, mk(DEVICE, DEVICE), 60, 1));
}

void test_lookup_and_aging(void) {
    Forwarder f(LOCAL);
    f.route(PORT_USB, mk(BCAST, DEVICE), 60, 1000);
    Port p = PORT_COUNT;
    TEST_ASSERT_TRUE(f.lookup(DEVICE, p, 2000));
    TEST_ASSERT_EQUAL_UINT8(PORT_USB, p);
    TEST_ASSERT_FALSE(f.lookup(DEVICE, p, 1000 + FORWARDER_AGE_MS));
}

void test_station_moving_ports_is_relearned(void) {
    Forwarder f(LOCAL);
    f.route(PORT_USB, mk(BCAST, PHONE), 60, 0);
    f.route(PORT_WIFI, mk(BCAST, PHONE), 60, 5);
    Port p = PORT_COUNT;
    TEST_ASSERT_TRUE(f.lookup(PHONE, p, 6));
    TEST_ASSERT_EQUAL_UINT8(PORT_WIFI, p);
}

void test_forget_port(void) {
    Forwarder f(LOCAL);
    f.route(PORT_USB, mk(BCAST, DEVICE), 60, 0);
    f.forgetPort(PORT_USB);
    Port p;
    TEST_ASSERT_FALSE(f.lookup(DEVICE, p, 1));
}

void test_table_full_evicts_oldest(void) {
    Forwarder f(LOCAL);
    uint8_t mac[6] = {0x02, 0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < FORWARDER_TABLE_SIZE; ++i) {
        mac[5] = (uint8_t)i;
        f.route(PORT_WIFI, mk(BCAST, mac), 60, 100 + i);
    }
    f.route(PORT_USB, mk(BCAST, DEVICE), 60, 500);
    Port p;
    TEST_ASSERT_TRUE(f.lookup(DEVICE, p, 501));
    mac[5] = 0;
    TEST_ASSERT_FALSE(f.lookup(mac, p, 501));
    mac[5] = 1;
    TEST_ASSERT_TRUE(f.lookup(mac, p, 501));
}

void test_local_source_not_learned(void) {
    Forwarder f(LOCAL);
    f.route(PORT_USB, mk(BCAST, LOCAL), 60, 0);
    TEST_ASSERT_EQUAL_UINT8(portBit(PORT_LOCAL), f.route(PORT_WIFI, mk(LOCAL, PHONE), 60, 1));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_runt_frame_goes_nowhere);
    RUN_TEST(test_broadcast_floods_all_other_ports);
    RUN_TEST(test_mdns_multicast_floods_all_other_ports);
    RUN_TEST(test_frame_for_local_mac_goes_only_local);
    RUN_TEST(test_unknown_unicast_floods_but_not_local);
    RUN_TEST(test_learned_unicast_goes_to_its_port);
    RUN_TEST(test_never_sends_back_to_ingress);
    RUN_TEST(test_lookup_and_aging);
    RUN_TEST(test_station_moving_ports_is_relearned);
    RUN_TEST(test_forget_port);
    RUN_TEST(test_table_full_evicts_oldest);
    RUN_TEST(test_local_source_not_learned);
    return UNITY_END();
}
