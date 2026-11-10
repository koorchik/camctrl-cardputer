#include <unity.h>
#include <cstring>

#include "logic/web_text.h"

void setUp(void) {}
void tearDown(void) {}

void test_form_field_decodes(void) {
    char v[64];
    const char* body = "ssid=My+Cam%21&password=a%26b%3Dc&channel=11";
    TEST_ASSERT_TRUE(formField(body, "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("My Cam!", v);
    TEST_ASSERT_TRUE(formField(body, "password", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("a&b=c", v);
    TEST_ASSERT_TRUE(formField(body, "channel", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("11", v);
}

void test_form_field_missing_and_prefix_keys(void) {
    char v[16];
    TEST_ASSERT_FALSE(formField("ssidx=1&x=2", "ssid", v, sizeof(v)));
    TEST_ASSERT_FALSE(formField("", "ssid", v, sizeof(v)));
    TEST_ASSERT_TRUE(formField("a=1&ssid=", "ssid", v, sizeof(v)));
    TEST_ASSERT_EQUAL_STRING("", v);
}

void test_form_field_rejects_bad_escape_and_overflow(void) {
    char v[4];
    TEST_ASSERT_FALSE(formField("ssid=%G1", "ssid", v, sizeof(v)));
    TEST_ASSERT_FALSE(formField("ssid=%4", "ssid", v, sizeof(v)));
    TEST_ASSERT_FALSE(formField("ssid=abcd", "ssid", v, sizeof(v)));
    TEST_ASSERT_TRUE(formField("ssid=abc", "ssid", v, sizeof(v)));
}

void test_html_escape(void) {
    char out[64];
    TEST_ASSERT_TRUE(htmlEscape("<a href=\"x\">'&'</a>", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("&lt;a href=&quot;x&quot;&gt;&#39;&amp;&#39;&lt;/a&gt;", out);
    char tiny[5];
    TEST_ASSERT_FALSE(htmlEscape("&&", tiny, sizeof(tiny)));
}

void test_json_escape(void) {
    char out[64];
    TEST_ASSERT_TRUE(jsonEscape("a\"b\\c\n", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\u000a", out);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_form_field_decodes);
    RUN_TEST(test_form_field_missing_and_prefix_keys);
    RUN_TEST(test_form_field_rejects_bad_escape_and_overflow);
    RUN_TEST(test_html_escape);
    RUN_TEST(test_json_escape);
    return UNITY_END();
}
