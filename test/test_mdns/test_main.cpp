#include <unity.h>
#include <cstring>

#include "logic/mdns.h"

void setUp(void) {}
void tearDown(void) {}

static const char* HOST = "micro-studio-camera-4k-g2";

void test_query_roundtrip(void) {
    uint8_t q[128];
    size_t n = mdnsBuildQuery(HOST, q, sizeof(q));
    TEST_ASSERT_TRUE(n > 12);
    bool qu = true;
    TEST_ASSERT_TRUE(mdnsQueryAsksFor(q, n, HOST, &qu));
    TEST_ASSERT_FALSE(qu);
    TEST_ASSERT_TRUE(mdnsQueryAsksFor(q, n, "MICRO-STUDIO-CAMERA-4K-G2", &qu));
    TEST_ASSERT_FALSE(mdnsQueryAsksFor(q, n, "other-camera", &qu));
    TEST_ASSERT_FALSE(mdnsResponseAnswers(q, n, HOST));
}

void test_qu_bit_detected(void) {
    uint8_t q[128];
    size_t n = mdnsBuildQuery(HOST, q, sizeof(q));
    q[n - 2] |= 0x80;
    bool qu = false;
    TEST_ASSERT_TRUE(mdnsQueryAsksFor(q, n, HOST, &qu));
    TEST_ASSERT_TRUE(qu);
}

void test_aaaa_query_is_not_ours(void) {
    uint8_t q[128];
    size_t n = mdnsBuildQuery(HOST, q, sizeof(q));
    q[n - 3] = 28;  // AAAA
    TEST_ASSERT_FALSE(mdnsQueryAsksFor(q, n, HOST, nullptr));
}

void test_answer_wire_format(void) {
    uint8_t a[128];
    size_t n = mdnsBuildAnswer("cam", 0xC0A84902, 120, a, sizeof(a));
    const uint8_t expected[] = {
        0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0,
        3, 'c', 'a', 'm', 5, 'l', 'o', 'c', 'a', 'l', 0,
        0, 1, 0x80, 1, 0, 0, 0, 120, 0, 4, 192, 168, 73, 2,
    };
    TEST_ASSERT_EQUAL_size_t(sizeof(expected), n);
    TEST_ASSERT_EQUAL_MEMORY(expected, a, n);
    TEST_ASSERT_TRUE(mdnsResponseAnswers(a, n, "cam"));
    TEST_ASSERT_TRUE(mdnsResponseAnswers(a, n, "CAM"));
    TEST_ASSERT_FALSE(mdnsResponseAnswers(a, n, "cam2"));
    TEST_ASSERT_FALSE(mdnsQueryAsksFor(a, n, "cam", nullptr));
}

void test_compressed_names_in_response(void) {
    // Question "cam.local" A, answer name is a pointer to offset 12.
    const uint8_t pkt[] = {
        0, 0, 0x84, 0, 0, 1, 0, 1, 0, 0, 0, 0,
        3, 'c', 'a', 'm', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 1, 0, 1,
        0xC0, 12, 0, 1, 0, 1, 0, 0, 0, 120, 0, 4, 10, 0, 0, 1,
    };
    TEST_ASSERT_TRUE(mdnsResponseAnswers(pkt, sizeof(pkt), "cam"));
}

void test_pointer_loop_rejected(void) {
    const uint8_t pkt[] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xC0, 12, 0, 1, 0, 1};
    TEST_ASSERT_FALSE(mdnsQueryAsksFor(pkt, sizeof(pkt), "cam", nullptr));
}

void test_truncated_packets_rejected(void) {
    uint8_t q[128];
    size_t n = mdnsBuildQuery(HOST, q, sizeof(q));
    for (size_t cut = 0; cut < n; ++cut) {
        TEST_ASSERT_FALSE(mdnsQueryAsksFor(q, cut, HOST, nullptr));
    }
}

void test_multiple_questions(void) {
    uint8_t pkt[256];
    size_t n = mdnsBuildQuery("other", pkt, sizeof(pkt));
    uint8_t second[128];
    size_t m = mdnsBuildQuery("cam", second, sizeof(second));
    memcpy(pkt + n, second + 12, m - 12);
    pkt[5] = 2;
    TEST_ASSERT_TRUE(mdnsQueryAsksFor(pkt, n + m - 12, "cam", nullptr));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_query_roundtrip);
    RUN_TEST(test_qu_bit_detected);
    RUN_TEST(test_aaaa_query_is_not_ours);
    RUN_TEST(test_answer_wire_format);
    RUN_TEST(test_compressed_names_in_response);
    RUN_TEST(test_pointer_loop_rejected);
    RUN_TEST(test_truncated_packets_rejected);
    RUN_TEST(test_multiple_questions);
    return UNITY_END();
}
