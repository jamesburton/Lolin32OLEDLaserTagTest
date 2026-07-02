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

// --- applyOverride -----------------------------------------------------------

void test_override_valid_matrix_pin() {
  BoardProfile p = active();
  TEST_ASSERT_TRUE(applyOverride(p, "matrixPin", 27));
  TEST_ASSERT_EQUAL_INT8(27, p.matrixPin);
}

void test_override_unknown_key_ignored() {
  BoardProfile p = active();
  int8_t before = p.matrixPin;
  TEST_ASSERT_FALSE(applyOverride(p, "nope", 5));
  TEST_ASSERT_EQUAL_INT8(before, p.matrixPin);
}

void test_override_out_of_range_pin_ignored() {
  BoardProfile p = active();
  int8_t before = p.matrixPin;
  TEST_ASSERT_FALSE(applyOverride(p, "matrixPin", 999));
  TEST_ASSERT_EQUAL_INT8(before, p.matrixPin); // safe fallback: unchanged
}

void test_override_matrix_order() {
  BoardProfile p = active();
  TEST_ASSERT_TRUE(applyOverride(p, "matrixOrder", 1));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ColourOrder::Rgb),
                        static_cast<int>(p.matrixOrder));
  TEST_ASSERT_FALSE(applyOverride(p, "matrixOrder", 5)); // out of range
}

// --- SD pins --------------------------------------------------------------

void test_s3_matrix_profile_sd_pins() {
#if defined(BOARD_S3_MATRIX)
  const BoardProfile &p = active();
  TEST_ASSERT_EQUAL_INT8(36, p.sdCsPin);
  TEST_ASSERT_EQUAL_INT8(34, p.sdMosiPin);
  TEST_ASSERT_EQUAL_INT8(35, p.sdMisoPin);
  TEST_ASSERT_EQUAL_INT8(33, p.sdSckPin);
  TEST_ASSERT_TRUE(p.hasSdCard());
#else
  const BoardProfile &p = active();
  TEST_ASSERT_EQUAL_INT8(-1, p.sdCsPin);
  TEST_ASSERT_FALSE(p.hasSdCard());
#endif
}

void test_override_valid_sd_cs_pin() {
  BoardProfile p = active();
  TEST_ASSERT_TRUE(applyOverride(p, "sdCsPin", 5));
  TEST_ASSERT_EQUAL_INT8(5, p.sdCsPin);
  TEST_ASSERT_TRUE(p.hasSdCard());
}

void test_override_out_of_range_sd_pin_ignored() {
  BoardProfile p = active();
  int8_t before = p.sdSckPin;
  TEST_ASSERT_FALSE(applyOverride(p, "sdSckPin", 999));
  TEST_ASSERT_EQUAL_INT8(before, p.sdSckPin);
}

// --- main ---------------------------------------------------------------------

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_active_profile_has_name);
  RUN_TEST(test_parse_hex_colour_with_hash);
  RUN_TEST(test_parse_hex_colour_without_hash);
  RUN_TEST(test_parse_hex_colour_rejects_malformed);
  RUN_TEST(test_override_valid_matrix_pin);
  RUN_TEST(test_override_unknown_key_ignored);
  RUN_TEST(test_override_out_of_range_pin_ignored);
  RUN_TEST(test_override_matrix_order);
  RUN_TEST(test_s3_matrix_profile_sd_pins);
  RUN_TEST(test_override_valid_sd_cs_pin);
  RUN_TEST(test_override_out_of_range_sd_pin_ignored);
  return UNITY_END();
}
