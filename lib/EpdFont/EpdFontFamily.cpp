#include "EpdFontFamily.h"

const EpdFont* EpdFontFamily::getFont(const Style style) const {
  if (style == BOLD && bold) {
    return bold;
  }
  if (style == ITALIC && italic) {
    return italic;
  }
  if (style == BOLD_ITALIC) {
    if (boldItalic) {
      return boldItalic;
    }
    if (bold) {
      return bold;
    }
    if (italic) {
      return italic;
    }
  }

  return regular;
}

void EpdFontFamily::getTextDimensions(const char* string, int* w, int* h, const Style style) const {
  getFont(style)->getTextDimensions(string, w, h);
}

void EpdFontFamily::getTextDimensions(const char* string, const size_t length, int* w, int* h,
                                      const Style style) const {
  getFont(style)->getTextDimensions(string, length, w, h);
}

void EpdFontFamily::getTextDimensionsWithAppend(const char* string, const size_t length, const uint32_t appendCp,
                                                int* w, int* h, const Style style) const {
  getFont(style)->getTextDimensionsWithAppend(string, length, appendCp, w, h);
}

void EpdFontFamily::getTextDimensionsWithAppendSkippingSoftHyphen(const char* string, const size_t length,
                                                                  const uint32_t appendCp, int* w, int* h,
                                                                  const Style style) const {
  getFont(style)->getTextDimensionsWithAppendSkippingSoftHyphen(string, length, appendCp, w, h);
}

bool EpdFontFamily::hasPrintableChars(const char* string, const Style style) const {
  return getFont(style)->hasPrintableChars(string);
}

bool EpdFontFamily::hasPrintableChars(const char* string, const size_t length, const Style style) const {
  return getFont(style)->hasPrintableChars(string, length);
}

const EpdFontData* EpdFontFamily::getData(const Style style) const { return getFont(style)->data; }

const EpdGlyph* EpdFontFamily::getGlyph(const uint32_t cp, const Style style) const {
  return getFont(style)->getGlyph(cp);
};
