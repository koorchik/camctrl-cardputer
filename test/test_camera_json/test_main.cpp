#include <unity.h>
#include <cstring>

#include "logic/camera_json.h"

void setUp(void) {}
void tearDown(void) {}

static bool apply(CameraValues& v, const char* path, const char* body) {
    return cameraApplyJson(v, path, body, strlen(body));
}

// Bodies as returned by a Micro Studio Camera 4K G2 (firmware 9.6.2).
void test_lens(void) {
    CameraValues v;
    TEST_ASSERT_TRUE(apply(v, "/lens/focus", "{\"normalised\": 0.6910420656204224}"));
    TEST_ASSERT_TRUE(v.focusKnown);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.691f, v.focus);
    TEST_ASSERT_TRUE(apply(v, "/lens/iris",
        "{\"apertureNumber\":132,\"apertureStop\":1.4249999523162842,"
        "\"continuousApertureAutoExposure\":false,\"normalised\":0.0087}"));
    TEST_ASSERT_TRUE(v.irisKnown);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 1.425f, v.irisStop);
    TEST_ASSERT_FALSE(v.irisAuto);
    TEST_ASSERT_TRUE(apply(v, "/lens/iris/description",
        "{\"apertureStop\":{\"max\":16.4375,\"min\":1.399999976158142},\"controllable\":true}"));
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 1.4f, v.irisMin);
    TEST_ASSERT_FLOAT_WITHIN(1e-3, 16.4375f, v.irisMax);
    TEST_ASSERT_TRUE(apply(v, "/lens/focus/description",
        "{\"controllable\":false,\"focusDistance\":{\"adjustable\":true}}"));
    TEST_ASSERT_FALSE(v.focusControllable);
}

void test_video(void) {
    CameraValues v;
    TEST_ASSERT_TRUE(apply(v, "/video/iso", "{\"iso\": 400}"));
    TEST_ASSERT_EQUAL(400, v.iso);
    TEST_ASSERT_TRUE(apply(v, "/video/supportedISOs",
        "{\"supportedISOs\":[800,100,400,400,25600,-5]}"));
    TEST_ASSERT_EQUAL_size_t(4, v.isoCount);
    TEST_ASSERT_EQUAL(100, v.isoList[0]);
    TEST_ASSERT_EQUAL(25600, v.isoList[3]);
    TEST_ASSERT_TRUE(apply(v, "/video/whiteBalance", "{\"whiteBalance\": 5600}"));
    TEST_ASSERT_EQUAL(5600, v.wb);
    TEST_ASSERT_TRUE(apply(v, "/video/whiteBalance/description",
        "{\"whiteBalance\":{\"max\":10000,\"min\":2500}}"));
    TEST_ASSERT_EQUAL(2500, v.wbMin);
    TEST_ASSERT_TRUE(apply(v, "/video/whiteBalanceTint", "{\"whiteBalanceTint\": -12}"));
    TEST_ASSERT_EQUAL(-12, v.tint);
    TEST_ASSERT_TRUE(apply(v, "/video/whiteBalanceTint/description",
        "{\"whiteBalanceTint\":{\"max\":50,\"min\":-50}}"));
    TEST_ASSERT_EQUAL(-50, v.tintMin);
}

void test_missing_keys_keep_values(void) {
    CameraValues v;
    apply(v, "/video/iso", "{\"iso\": 800}");
    TEST_ASSERT_TRUE(apply(v, "/video/iso", "{}"));
    TEST_ASSERT_EQUAL(800, v.iso);
    TEST_ASSERT_TRUE(apply(v, "/video/whiteBalance/description", "{\"whiteBalance\":{\"max\":1,\"min\":9}}"));
    TEST_ASSERT_EQUAL(2500, v.wbMin);  // inverted range ignored
}

void test_displays_and_clean_feed(void) {
    CameraValues v;
    TEST_ASSERT_TRUE(apply(v, "/monitoring/display",
        "{\"displays\":[\"MainSDI\",\"HDMI\",\"FrontUSBC\"]}"));
    TEST_ASSERT_EQUAL_size_t(3, v.displayCount);
    TEST_ASSERT_TRUE(apply(v, "/monitoring/MainSDI/cleanFeed", "{\"enabled\": true}"));
    TEST_ASSERT_TRUE(v.cleanFeedKnown[0]);
    TEST_ASSERT_TRUE(v.cleanFeed[0]);
    TEST_ASSERT_FALSE(v.cleanFeedKnown[1]);
    TEST_ASSERT_FALSE(apply(v, "/monitoring/LCD/cleanFeed", "{\"enabled\": true}"));
    // Re-reading the list keeps known states of displays that remain.
    TEST_ASSERT_TRUE(apply(v, "/monitoring/display", "[\"HDMI\",\"MainSDI\"]"));
    TEST_ASSERT_EQUAL_size_t(2, v.displayCount);
    TEST_ASSERT_EQUAL_STRING("MainSDI", v.displays[1]);
    TEST_ASSERT_TRUE(v.cleanFeedKnown[1]);
    TEST_ASSERT_TRUE(v.cleanFeed[1]);
    TEST_ASSERT_FALSE(v.cleanFeedKnown[0]);
}

void test_product_name(void) {
    CameraValues v;
    TEST_ASSERT_TRUE(apply(v, "/system/product",
        "{\"deviceName\":\"Micro Studio Camera 4K G2\",\"productName\":\"x\",\"softwareVersion\":\"9.6.2\"}"));
    TEST_ASSERT_EQUAL_STRING("Micro Studio Camera 4K G2", v.name);
}

void test_garbage_and_unknown_paths(void) {
    CameraValues v;
    TEST_ASSERT_FALSE(apply(v, "/video/iso", "not json"));
    TEST_ASSERT_FALSE(apply(v, "/video/shutter", "{\"shutterSpeed\":50}"));
    TEST_ASSERT_FALSE(apply(v, "/monitoring/a/b/cleanFeed", "{\"enabled\":true}"));
}

void test_ws_messages(void) {
    CameraValues v;
    const char* opened = "{\"type\":\"event\",\"data\":{\"action\":\"websocketOpened\"}}";
    TEST_ASSERT_EQUAL(CameraWsKind::Opened, cameraApplyWsMessage(v, opened, strlen(opened)));
    const char* ev = "{\"type\":\"event\",\"data\":{\"action\":\"propertyValueChanged\","
                     "\"property\":\"/video/iso\",\"value\":{\"iso\":1600}}}";
    TEST_ASSERT_EQUAL(CameraWsKind::PropertyChanged, cameraApplyWsMessage(v, ev, strlen(ev)));
    TEST_ASSERT_EQUAL(1600, v.iso);
    const char* resp = "{\"type\":\"response\",\"data\":{\"action\":\"subscribe\",\"success\":true}}";
    TEST_ASSERT_EQUAL(CameraWsKind::Other, cameraApplyWsMessage(v, resp, strlen(resp)));
    TEST_ASSERT_EQUAL(CameraWsKind::Other, cameraApplyWsMessage(v, "{", 1));
}

void test_subscribe_message(void) {
    CameraValues v;
    apply(v, "/monitoring/display", "{\"displays\":[\"HDMI\"]}");
    char buf[512];
    const size_t n = cameraSubscribeMessage(v, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"action\":\"subscribe\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"/lens/focus\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"/monitoring/HDMI/cleanFeed\""));
    TEST_ASSERT_EQUAL_size_t(0, cameraSubscribeMessage(v, buf, 20));
}

void test_encode_path_segment(void) {
    char out[32];
    TEST_ASSERT_EQUAL_size_t(7, cameraEncodePathSegment("MainSDI", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("MainSDI", out);
    cameraEncodePathSegment("LCD 1/A", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("LCD%201%2FA", out);
    TEST_ASSERT_EQUAL_size_t(0, cameraEncodePathSegment("abcdef", out, 4));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_lens);
    RUN_TEST(test_video);
    RUN_TEST(test_missing_keys_keep_values);
    RUN_TEST(test_displays_and_clean_feed);
    RUN_TEST(test_product_name);
    RUN_TEST(test_garbage_and_unknown_paths);
    RUN_TEST(test_ws_messages);
    RUN_TEST(test_subscribe_message);
    RUN_TEST(test_encode_path_segment);
    return UNITY_END();
}
