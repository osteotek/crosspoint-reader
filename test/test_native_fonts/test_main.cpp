#include <EpdFont/EpdFontFamily.h>
#include <unity.h>

#include "../../lib/EpdFont/EpdFont.cpp"
#include "../../lib/EpdFont/EpdFontFamily.cpp"
#include "../../lib/Utf8/Utf8.cpp"

static const uint8_t DUMMY_BITMAP[]{0xFF};

static const EpdGlyph REGULAR_GLYPHS[] = {
    {3, 5, 4, 0, 4, 0, 0},  // '?'
    {4, 6, 5, 0, 6, 0, 0},  // 'A'
    {5, 7, 6, 0, 7, 0, 0},  // Cyrillic fallback glyph
};

static const EpdUnicodeInterval REGULAR_INTERVALS[] = {
    {'?', '?', 0},
    {'A', 'A', 1},
    {0x410, 0x410, 2},
};

static const EpdFontData REGULAR_FONT_DATA{
    DUMMY_BITMAP,
    REGULAR_GLYPHS,
    REGULAR_INTERVALS,
    sizeof(REGULAR_INTERVALS) / sizeof(REGULAR_INTERVALS[0]),
    8,
    7,
    -2,
    false};

static const EpdGlyph BOLD_GLYPHS[] = {
    {4, 6, 5, 0, 6, 0, 0},  // '?'
    {5, 7, 6, 0, 7, 0, 0},  // 'A'
};

static const EpdUnicodeInterval BOLD_INTERVALS[] = {
    {'?', '?', 0},
    {'A', 'A', 1},
};

static const EpdFontData BOLD_FONT_DATA{
    DUMMY_BITMAP,
    BOLD_GLYPHS,
    BOLD_INTERVALS,
    sizeof(BOLD_INTERVALS) / sizeof(BOLD_INTERVALS[0]),
    9,
    8,
    -3,
    false};

static const EpdFont REGULAR_FONT(&REGULAR_FONT_DATA);
static const EpdFont BOLD_FONT(&BOLD_FONT_DATA);
static const EpdFontFamily FONT_FAMILY(&REGULAR_FONT, &BOLD_FONT);

void test_get_glyph_exact_match() {
  const EpdGlyph* glyph = REGULAR_FONT.getGlyph('A');
  TEST_ASSERT_NOT_NULL(glyph);
  TEST_ASSERT_EQUAL_UINT8(4, glyph->width);
  TEST_ASSERT_EQUAL_UINT8(6, glyph->height);
}

void test_get_glyph_missing_returns_null() {
  TEST_ASSERT_NULL(REGULAR_FONT.getGlyph('B'));
}

void test_get_text_dimensions_uses_fallback() {
  int width = 0;
  int height = 0;
  REGULAR_FONT.getTextDimensions("B", &width, &height);
  TEST_ASSERT_EQUAL_INT(3, width);   // falls back to '?'
  TEST_ASSERT_EQUAL_INT(5, height);
}

void test_has_printable_chars_false_for_empty() {
  TEST_ASSERT_FALSE(REGULAR_FONT.hasPrintableChars(""));
}

void test_has_printable_chars_true_for_unknown_unicode() {
  TEST_ASSERT_TRUE(REGULAR_FONT.hasPrintableChars("\x01"));
}

void test_font_family_returns_bold_data() {
  const EpdFontData* boldData = FONT_FAMILY.getData(BOLD);
  TEST_ASSERT_EQUAL_INT(8, boldData->ascender);
}

void test_font_family_fallback_for_bold_italic() {
  const EpdGlyph* glyph = FONT_FAMILY.getGlyph('A', BOLD_ITALIC);
  TEST_ASSERT_NOT_NULL(glyph);
  TEST_ASSERT_EQUAL_UINT8(5, glyph->width);  // from bold font
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_get_glyph_exact_match);
  RUN_TEST(test_get_glyph_missing_returns_null);
  RUN_TEST(test_get_text_dimensions_uses_fallback);
  RUN_TEST(test_has_printable_chars_false_for_empty);
  RUN_TEST(test_has_printable_chars_true_for_unknown_unicode);
  RUN_TEST(test_font_family_returns_bold_data);
  RUN_TEST(test_font_family_fallback_for_bold_italic);
  return UNITY_END();
}
