#include <unity.h>
#include <cstring>

#include "logic/remote_model.h"

void setUp(void) {}
void tearDown(void) {}

static CameraValues camera() {
    CameraValues v;
    v.connected = true;
    v.focusKnown = true;
    v.focus = 0.5f;
    v.irisKnown = true;
    v.irisStop = 2.8f;
    v.irisMin = 1.4f;
    v.irisMax = 16.0f;
    v.isoKnown = true;
    v.iso = 400;
    const int isos[] = {100, 200, 400, 800, 1600, 3200};
    memcpy(v.isoList, isos, sizeof(isos));
    v.isoCount = 6;
    v.wbKnown = true;
    v.wb = 5600;
    v.tintKnown = true;
    v.tint = 0;
    v.displayCount = 2;
    strcpy(v.displays[0], "MainSDI");
    strcpy(v.displays[1], "HDMI");
    return v;
}

void test_key_mapping(void) {
    TEST_ASSERT_EQUAL(RemoteKey::Up, remoteKeyFor({KeyCode::Char, ';'}));
    TEST_ASSERT_EQUAL(RemoteKey::Down, remoteKeyFor({KeyCode::Char, '.'}));
    TEST_ASSERT_EQUAL(RemoteKey::Left, remoteKeyFor({KeyCode::Char, ','}));
    TEST_ASSERT_EQUAL(RemoteKey::Right, remoteKeyFor({KeyCode::Char, '/'}));
    TEST_ASSERT_EQUAL(RemoteKey::AutoFocus, remoteKeyFor({KeyCode::Char, ' '}));
    TEST_ASSERT_EQUAL(RemoteKey::Enter, remoteKeyFor({KeyCode::Enter, 0}));
    TEST_ASSERT_EQUAL(RemoteKey::JumpWb, remoteKeyFor({KeyCode::Char, 'w'}));
    TEST_ASSERT_EQUAL(RemoteKey::None, remoteKeyFor({KeyCode::Shift, 0}));
    TEST_ASSERT_TRUE(remoteKeyRepeats(RemoteKey::Left));
    TEST_ASSERT_FALSE(remoteKeyRepeats(RemoteKey::Enter));
}

void test_row_navigation_wraps(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::Up, false, v);
    TEST_ASSERT_EQUAL(RemoteRow::Feed, m.row());
    m.onKey(RemoteKey::Down, false, v);
    TEST_ASSERT_EQUAL(RemoteRow::Focus, m.row());
    m.onKey(RemoteKey::JumpTint, false, v);
    TEST_ASSERT_EQUAL(RemoteRow::Tint, m.row());
}

void test_focus_steps_and_clamps(void) {
    RemoteModel m;
    CameraValues v = camera();
    RemoteCommand c = m.onKey(RemoteKey::Right, false, v);
    TEST_ASSERT_EQUAL(RemoteCmd::SetFocus, c.type);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 0.505f, c.f);
    v.focus = 0.98f;
    c = m.onKey(RemoteKey::Right, true, v);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 1.0f, c.f);
    c = m.onKey(RemoteKey::Right, true, v);
    TEST_ASSERT_EQUAL(RemoteCmd::None, c.type);
    v.focusControllable = false;
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Left, false, v).type);
}

void test_autofocus_from_space_and_enter(void) {
    RemoteModel m;
    CameraValues v = camera();
    TEST_ASSERT_EQUAL(RemoteCmd::AutoFocus, m.onKey(RemoteKey::AutoFocus, false, v).type);
    TEST_ASSERT_EQUAL(RemoteCmd::AutoFocus, m.onKey(RemoteKey::Enter, false, v).type);
    m.onKey(RemoteKey::JumpIso, false, v);
    TEST_ASSERT_EQUAL(RemoteCmd::AutoFocus, m.onKey(RemoteKey::AutoFocus, false, v).type);
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Enter, false, v).type);
}

void test_iris_sends_steps_without_guessing_the_value(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpIris, false, v);
    RemoteCommand c = m.onKey(RemoteKey::Right, false, v);
    TEST_ASSERT_EQUAL(RemoteCmd::IrisStep, c.type);
    TEST_ASSERT_EQUAL(1, c.i);
    c = m.onKey(RemoteKey::Left, true, v);
    TEST_ASSERT_EQUAL(-IRIS_BIG_STEP, c.i);
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 2.8f, v.irisStop);
    v.irisAuto = true;
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Right, false, v).type);
}

void test_iso_walks_the_list(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpIso, false, v);
    RemoteCommand c = m.onKey(RemoteKey::Right, false, v);
    TEST_ASSERT_EQUAL(RemoteCmd::SetIso, c.type);
    TEST_ASSERT_EQUAL(800, c.i);
    c = m.onKey(RemoteKey::Right, true, v);
    TEST_ASSERT_EQUAL(3200, c.i);  // clamped at the end
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Right, false, v).type);
    v.iso = 1000;  // off-list (e.g. set elsewhere): steps from the nearest
    c = m.onKey(RemoteKey::Left, false, v);
    TEST_ASSERT_EQUAL(400, c.i);
}

void test_wb_snaps_to_grid_and_clamps(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpWb, false, v);
    TEST_ASSERT_EQUAL(5650, m.onKey(RemoteKey::Right, false, v).i);
    v.wb = 5537;
    TEST_ASSERT_EQUAL(5550, m.onKey(RemoteKey::Right, false, v).i);
    v.wb = 5537;
    TEST_ASSERT_EQUAL(5500, m.onKey(RemoteKey::Left, false, v).i);
    v.wb = 9900;
    TEST_ASSERT_EQUAL(10000, m.onKey(RemoteKey::Right, true, v).i);
    TEST_ASSERT_EQUAL(RemoteCmd::AutoWb, m.onKey(RemoteKey::Enter, false, v).type);
}

void test_tint(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpTint, false, v);
    TEST_ASSERT_EQUAL(-1, m.onKey(RemoteKey::Left, false, v).i);
    v.tint = 45;
    TEST_ASSERT_EQUAL(50, m.onKey(RemoteKey::Right, true, v).i);
}

void test_clean_feed_select_and_toggle(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpFeed, false, v);
    TEST_ASSERT_EQUAL(0, m.feedDisplay());
    m.onKey(RemoteKey::Right, false, v);
    TEST_ASSERT_EQUAL(1, m.feedDisplay());
    m.onKey(RemoteKey::Right, false, v);
    TEST_ASSERT_EQUAL(0, m.feedDisplay());  // wraps
    m.onKey(RemoteKey::Left, false, v);
    RemoteCommand c = m.onKey(RemoteKey::Enter, false, v);
    TEST_ASSERT_EQUAL(RemoteCmd::SetCleanFeed, c.type);
    TEST_ASSERT_EQUAL(1, c.display);
    TEST_ASSERT_TRUE(c.b);
    TEST_ASSERT_TRUE(v.cleanFeed[1]);
    c = m.onKey(RemoteKey::Enter, false, v);
    TEST_ASSERT_FALSE(c.b);
    v.displayCount = 1;
    m.clampFeedDisplay(v);
    TEST_ASSERT_EQUAL(0, m.feedDisplay());
}

void test_help_swallows_next_key(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::Help, false, v);
    TEST_ASSERT_TRUE(m.helpVisible());
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::AutoFocus, false, v).type);
    TEST_ASSERT_FALSE(m.helpVisible());
}

void test_unknown_values_send_nothing(void) {
    RemoteModel m;
    CameraValues v;  // nothing known yet
    for (int r = 0; r < REMOTE_ROWS; ++r) {
        TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Right, false, v).type);
        m.onKey(RemoteKey::Down, false, v);
    }
}

void test_formatting(void) {
    CameraValues v = camera();
    char s[24];
    formatFocus(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("50%", s);
    formatIris(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("f/2.8", s);
    v.irisStop = 1.425f;  // quarter-stop reading near the lens minimum
    formatIris(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("f/1.4", s);
    v.irisStop = 16.4f;
    v.irisMax = 16.4375f;
    formatIris(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("f/16", s);
    v.irisAuto = true;
    formatIris(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("f/16 A", s);
    formatWb(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("5600K", s);
    v.tint = 5;
    formatTint(v, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("+5", s);
    CameraValues none;
    formatIso(none, s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("--", s);
    shortDisplayName("FrontUSBC", s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("USB", s);
    shortDisplayName("MainSDI", s, sizeof(s));
    TEST_ASSERT_EQUAL_STRING("SDI", s);
}

void test_back_key_and_iris_limits(void) {
    TEST_ASSERT_EQUAL(RemoteKey::Back, remoteKeyFor({KeyCode::Backspace, 0}));
    TEST_ASSERT_EQUAL(RemoteKey::Back, remoteKeyFor({KeyCode::Char, '`'}));
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpIris, false, v);
    v.irisStop = 1.42f;  // at the lens minimum (f/1.4)
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Left, false, v).type);
    TEST_ASSERT_TRUE(m.limitHit());
    TEST_ASSERT_EQUAL(RemoteCmd::IrisStep, m.onKey(RemoteKey::Right, false, v).type);
    TEST_ASSERT_FALSE(m.limitHit());
    v.irisStop = 16.0f;
    TEST_ASSERT_EQUAL(RemoteCmd::None, m.onKey(RemoteKey::Right, false, v).type);
    TEST_ASSERT_TRUE(m.limitHit());
}

void test_limit_hit_on_iso_end_but_not_when_unknown(void) {
    RemoteModel m;
    CameraValues v = camera();
    m.onKey(RemoteKey::JumpIso, false, v);
    v.iso = 3200;
    m.onKey(RemoteKey::Right, false, v);
    TEST_ASSERT_TRUE(m.limitHit());
    CameraValues none;
    m.onKey(RemoteKey::Right, false, none);
    TEST_ASSERT_FALSE(m.limitHit());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_key_mapping);
    RUN_TEST(test_row_navigation_wraps);
    RUN_TEST(test_focus_steps_and_clamps);
    RUN_TEST(test_autofocus_from_space_and_enter);
    RUN_TEST(test_iris_sends_steps_without_guessing_the_value);
    RUN_TEST(test_iso_walks_the_list);
    RUN_TEST(test_wb_snaps_to_grid_and_clamps);
    RUN_TEST(test_tint);
    RUN_TEST(test_clean_feed_select_and_toggle);
    RUN_TEST(test_help_swallows_next_key);
    RUN_TEST(test_unknown_values_send_nothing);
    RUN_TEST(test_formatting);
    RUN_TEST(test_back_key_and_iris_limits);
    RUN_TEST(test_limit_hit_on_iso_end_but_not_when_unknown);
    return UNITY_END();
}
