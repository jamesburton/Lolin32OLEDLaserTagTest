/*
 * Native unit tests for the Board library (BoardProfile struct, compiled-in
 * profiles, runtime overrides, and hex-colour parser).
 *
 * Run with: pio test -e native -f test_board
 */

#include <BoardProfile.h>
#include <unity.h>

using namespace Board;

void setUp() {}
void tearDown() {}

// --- Active profile -----------------------------------------------------------

void test_active_profile_has_name() {
  const BoardProfile &p = active();
  TEST_ASSERT_NOT_NULL(p.name);
  TEST_ASSERT_TRUE(p.irRxPin >= 0);
}

// --- parseHexColour -----------------------------------------------------------

void test_parse_hex_colour_with_hash() {
  Rgb c;
  TEST_ASSERT_TRUE(parseHexColour("#0000FF", c));
  TEST_ASSERT_EQUAL_UINT8(0x00, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.g);
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.b);
}

void test_parse_hex_colour_without_hash() {
  Rgb c;
  TEST_ASSERT_TRUE(parseHexColour("FF8800", c));
  TEST_ASSERT_EQUAL_UINT8(0xFF, c.r);
  TEST_ASSERT_EQUAL_UINT8(0x88, c.g);
  TEST_ASSERT_EQUAL_UINT8(0x00, c.b);
}

void test_parse_hex_colour_rejects_malformed() {
  Rgb c;
  TEST_ASSERT_FALSE(parseHexColour("#12", c));
  TEST_ASSERT_FALSE(parseHexColour("ZZZZZZ", c));
  TEST_ASSERT_FALSE(parseHexColour("", c));
}

// --- main ---------------------------------------------------------------------

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_active_profile_has_name);
  RUN_TEST(test_parse_hex_colour_with_hash);
  RUN_TEST(test_parse_hex_colour_without_hash);
  RUN_TEST(test_parse_hex_colour_rejects_malformed);
  return UNITY_END();
}
