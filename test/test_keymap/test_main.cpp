#include <unity.h>

#include "logic/key_repeat.h"
#include "logic/keymap.h"

void setUp(void) {}
void tearDown(void) {}

// TCA8418 key number for a layout position (inverse of the ADV remap).
static uint8_t rawFor(int row, int col, bool pressed) {
    const int r = col / 2;
    const int c = row + ((col % 2) ? 4 : 0);
    return (uint8_t)((pressed ? 0x80 : 0) | (r * 10 + c + 1));
}

void test_layout_corners(void) {
    TEST_ASSERT_EQUAL_CHAR('`', keyAt(0, 0).ch);
    TEST_ASSERT_EQUAL(KeyCode::Backspace, keyAt(0, 13).code);
    TEST_ASSERT_EQUAL(KeyCode::Tab, keyAt(1, 0).code);
    TEST_ASSERT_EQUAL(KeyCode::Fn, keyAt(2, 0).code);
    TEST_ASSERT_EQUAL(KeyCode::Shift, keyAt(2, 1).code);
    TEST_ASSERT_EQUAL(KeyCode::Enter, keyAt(2, 13).code);
    TEST_ASSERT_EQUAL(KeyCode::Ctrl, keyAt(3, 0).code);
    TEST_ASSERT_EQUAL(KeyCode::Opt, keyAt(3, 1).code);
    TEST_ASSERT_EQUAL(KeyCode::Alt, keyAt(3, 2).code);
    TEST_ASSERT_EQUAL_CHAR(' ', keyAt(3, 13).ch);
    TEST_ASSERT_EQUAL(KeyCode::None, keyAt(4, 0).code);
    TEST_ASSERT_EQUAL(KeyCode::None, keyAt(0, 14).code);
}

void test_arrow_keys_positions(void) {
    TEST_ASSERT_EQUAL_CHAR(';', keyAt(2, 11).ch);
    TEST_ASSERT_EQUAL_CHAR(',', keyAt(3, 10).ch);
    TEST_ASSERT_EQUAL_CHAR('.', keyAt(3, 11).ch);
    TEST_ASSERT_EQUAL_CHAR('/', keyAt(3, 12).ch);
}

void test_tca_decode_matches_m5cardputer_remap(void) {
    for (int row = 0; row < KEY_ROWS; ++row) {
        for (int col = 0; col < KEY_COLS; ++col) {
            KeyEvent e;
            TEST_ASSERT_TRUE(tcaDecode(rawFor(row, col, true), e));
            TEST_ASSERT_EQUAL(row, e.row);
            TEST_ASSERT_EQUAL(col, e.col);
            TEST_ASSERT_TRUE(e.pressed);
            TEST_ASSERT_TRUE(tcaDecode(rawFor(row, col, false), e));
            TEST_ASSERT_FALSE(e.pressed);
        }
    }
}

void test_tca_decode_known_raw_values(void) {
    KeyEvent e;
    // Raw 1 = TCA row 0 col 0 -> layout row 0, col 0 ('`').
    TEST_ASSERT_TRUE(tcaDecode(0x81, e));
    TEST_ASSERT_EQUAL_CHAR('`', e.key.ch);
    // Raw 5 = TCA row 0 col 4 -> layout row 0, col 1 ('1').
    TEST_ASSERT_TRUE(tcaDecode(0x85, e));
    TEST_ASSERT_EQUAL_CHAR('1', e.key.ch);
}

void test_tca_decode_rejects_empty_and_unmapped(void) {
    KeyEvent e;
    TEST_ASSERT_FALSE(tcaDecode(0x00, e));
    TEST_ASSERT_FALSE(tcaDecode(0x80, e));
    // TCA row 7 -> layout col 14/15: outside the 4 x 14 layout.
    TEST_ASSERT_FALSE(tcaDecode(0x80 | (7 * 10 + 0 + 1), e));
}

void test_repeater(void) {
    KeyRepeater r(300, 100, 3);
    TEST_ASSERT_EQUAL_UINT32(0, r.poll(1000));
    r.press(1000);
    TEST_ASSERT_EQUAL_UINT32(0, r.poll(1299));
    TEST_ASSERT_EQUAL_UINT32(1, r.poll(1300));
    TEST_ASSERT_FALSE(r.boosted());
    TEST_ASSERT_EQUAL_UINT32(2, r.poll(1500));
    TEST_ASSERT_TRUE(r.boosted());
    r.release();
    TEST_ASSERT_EQUAL_UINT32(0, r.poll(5000));
    r.press(6000);
    TEST_ASSERT_FALSE(r.boosted());
}

void test_repeater_caps_burst_after_a_stall(void) {
    KeyRepeater r(300, 100, 100);
    r.press(0);
    TEST_ASSERT_EQUAL_UINT32(8, r.poll(60000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_layout_corners);
    RUN_TEST(test_arrow_keys_positions);
    RUN_TEST(test_tca_decode_matches_m5cardputer_remap);
    RUN_TEST(test_tca_decode_known_raw_values);
    RUN_TEST(test_tca_decode_rejects_empty_and_unmapped);
    RUN_TEST(test_repeater);
    RUN_TEST(test_repeater_caps_burst_after_a_stall);
    return UNITY_END();
}
