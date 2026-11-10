#include <unity.h>

#include "logic/ui_nav.h"

void setUp(void) {}
void tearDown(void) {}

void test_overview_arrows_move_over_the_grid(void) {
    UiNav n;
    NavKeys k = n.onKey(RemoteKey::Right, 100, true);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
    TEST_ASSERT_EQUAL(1, k.count);
    TEST_ASSERT_EQUAL(RemoteKey::ColRight, k.keys[0]);
    k = n.onKey(RemoteKey::Left, 110, true);
    TEST_ASSERT_EQUAL(RemoteKey::ColLeft, k.keys[0]);
    k = n.onKey(RemoteKey::Down, 120, true);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
    TEST_ASSERT_EQUAL(RemoteKey::Down, k.keys[0]);
    TEST_ASSERT_FALSE(k.big);
}

void test_enter_opens_detail_without_acting(void) {
    UiNav n;
    NavKeys k = n.onKey(RemoteKey::Enter, 0, true);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    TEST_ASSERT_EQUAL(0, k.count);
    // Inside, Enter is the control's action (AF, auto WB, feed on/off).
    k = n.onKey(RemoteKey::Enter, 10, true);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    TEST_ASSERT_EQUAL(1, k.count);
    TEST_ASSERT_EQUAL(RemoteKey::Enter, k.keys[0]);
}

void test_detail_left_right_adjust_up_down_big_steps(void) {
    UiNav n;
    n.onKey(RemoteKey::Enter, 0, true);
    NavKeys k = n.onKey(RemoteKey::Right, 10, true);
    TEST_ASSERT_EQUAL(RemoteKey::Right, k.keys[0]);
    TEST_ASSERT_FALSE(k.big);
    k = n.onKey(RemoteKey::Up, 20, true);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    TEST_ASSERT_EQUAL(1, k.count);
    TEST_ASSERT_EQUAL(RemoteKey::Right, k.keys[0]);
    TEST_ASSERT_TRUE(k.big);
    k = n.onKey(RemoteKey::Down, 30, true);
    TEST_ASSERT_EQUAL(RemoteKey::Left, k.keys[0]);
    TEST_ASSERT_TRUE(k.big);
    n.onKey(RemoteKey::Back, 40, true);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
}

void test_detail_closes_after_idle_unless_busy(void) {
    UiNav n;
    n.onKey(RemoteKey::Enter, 1000, true);
    n.tick(1000 + DETAIL_IDLE_CLOSE_MS - 1, false);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    n.tick(1000 + DETAIL_IDLE_CLOSE_MS + 500, true);   // busy: stays, idle restarts
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    n.tick(1000 + DETAIL_IDLE_CLOSE_MS + 600, false);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    n.tick(1000 + 2 * DETAIL_IDLE_CLOSE_MS + 600, false);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
}

void test_space_selects_focus_and_runs_af(void) {
    UiNav n;
    NavKeys k = n.onKey(RemoteKey::AutoFocus, 0, true);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    TEST_ASSERT_EQUAL(2, k.count);
    TEST_ASSERT_EQUAL(RemoteKey::JumpFocus, k.keys[0]);
    TEST_ASSERT_EQUAL(RemoteKey::AutoFocus, k.keys[1]);
}

void test_jump_opens_detail_back_closes(void) {
    UiNav n;
    NavKeys k = n.onKey(RemoteKey::JumpWb, 0, true);
    TEST_ASSERT_EQUAL(UiView::Detail, n.view());
    TEST_ASSERT_EQUAL(RemoteKey::JumpWb, k.keys[0]);
    k = n.onKey(RemoteKey::Back, 5, true);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
    TEST_ASSERT_EQUAL(0, k.count);
}

void test_help_from_anywhere_any_key_closes_enter_starts_intro(void) {
    UiNav n;
    n.onKey(RemoteKey::Help, 0, false);  // also without a camera
    TEST_ASSERT_EQUAL(UiView::Help, n.view());
    NavKeys k = n.onKey(RemoteKey::AutoFocus, 1, true);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
    TEST_ASSERT_EQUAL(0, k.count);  // closing key is not executed
    n.onKey(RemoteKey::Help, 2, true);
    n.onKey(RemoteKey::Enter, 3, true);
    TEST_ASSERT_EQUAL(UiView::Intro, n.view());
    TEST_ASSERT_EQUAL(0, n.introCard());
}

void test_intro_cards(void) {
    UiNav n;
    n.startIntro();
    NavKeys k = n.onKey(RemoteKey::Left, 0, true);  // any other key: next card
    TEST_ASSERT_EQUAL(1, n.introCard());
    TEST_ASSERT_EQUAL(0, k.count);
    k = n.onKey(RemoteKey::Enter, 1, true);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
    TEST_ASSERT_TRUE(n.introFinished());
    TEST_ASSERT_EQUAL(RemoteKey::JumpFocus, k.keys[0]);
}

void test_intro_autofocus_runs_and_advances_when_done(void) {
    UiNav n;
    n.startIntro();
    NavKeys k = n.onKey(RemoteKey::AutoFocus, 0, true);
    TEST_ASSERT_EQUAL(RemoteKey::AutoFocus, k.keys[1]);
    TEST_ASSERT_EQUAL(0, n.introCard());
    n.onAutofocusDone(1000);
    n.tick(1000 + INTRO_AF_ADVANCE_MS - 1, false);
    TEST_ASSERT_EQUAL(0, n.introCard());
    n.tick(1000 + INTRO_AF_ADVANCE_MS, false);
    TEST_ASSERT_EQUAL(1, n.introCard());
}

void test_no_camera_ignores_control_keys(void) {
    UiNav n;
    TEST_ASSERT_EQUAL(0, n.onKey(RemoteKey::Right, 0, false).count);
    TEST_ASSERT_EQUAL(UiView::Overview, n.view());
}

void test_hint_tracker_graduates_and_falls_back(void) {
    HintTracker h(false);
    for (int i = 0; i < 3; ++i) h.onNavigate();
    for (int i = 0; i < 5; ++i) h.onConfirmedAdjust();
    TEST_ASSERT_FALSE(h.slim());
    h.onAutofocus();
    TEST_ASSERT_TRUE(h.slim());
    TEST_ASSERT_TRUE(h.takeGraduated());
    TEST_ASSERT_FALSE(h.takeGraduated());
    TEST_ASSERT_FALSE(h.onUnmappedKey(1000));
    TEST_ASSERT_TRUE(h.onUnmappedKey(3000));
    TEST_ASSERT_FALSE(h.slim());  // full hints for the rest of the session
}

void test_hint_tracker_unmapped_keys_far_apart_are_fine(void) {
    HintTracker h(true);
    TEST_ASSERT_FALSE(h.onUnmappedKey(1000));
    TEST_ASSERT_FALSE(h.onUnmappedKey(9000));
    TEST_ASSERT_TRUE(h.slim());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_overview_arrows_move_over_the_grid);
    RUN_TEST(test_enter_opens_detail_without_acting);
    RUN_TEST(test_detail_left_right_adjust_up_down_big_steps);
    RUN_TEST(test_detail_closes_after_idle_unless_busy);
    RUN_TEST(test_space_selects_focus_and_runs_af);
    RUN_TEST(test_jump_opens_detail_back_closes);
    RUN_TEST(test_help_from_anywhere_any_key_closes_enter_starts_intro);
    RUN_TEST(test_intro_cards);
    RUN_TEST(test_intro_autofocus_runs_and_advances_when_done);
    RUN_TEST(test_no_camera_ignores_control_keys);
    RUN_TEST(test_hint_tracker_graduates_and_falls_back);
    RUN_TEST(test_hint_tracker_unmapped_keys_far_apart_are_fine);
    return UNITY_END();
}
