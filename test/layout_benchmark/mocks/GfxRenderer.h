#pragma once

#include <cstddef>
#include <map>

#include <EpdFontFamily.h>

class GfxRenderer {
 public:
  void insertFont(const int fontId, const EpdFontFamily& font) { fontMap.insert({fontId, font}); }

  int getTextWidth(const int fontId, const char* text,
                   const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) {
      return 0;
    }

    int w = 0;
    int h = 0;
    it->second.getTextDimensions(text, &w, &h, style);
    return w;
  }

  int getTextWidth(const int fontId, const char* text, const size_t length,
                   const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) {
      return 0;
    }

    int w = 0;
    int h = 0;
    it->second.getTextDimensions(text, length, &w, &h, style);
    return w;
  }

  int getTextWidthWithAppend(const int fontId, const char* text, const size_t length, const uint32_t appendCp,
                             const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) {
      return 0;
    }

    int w = 0;
    int h = 0;
    it->second.getTextDimensionsWithAppend(text, length, appendCp, &w, &h, style);
    return w;
  }

  int getTextWidthWithAppendSkippingSoftHyphen(const int fontId, const char* text, const size_t length,
                                               const uint32_t appendCp,
                                               const EpdFontFamily::Style style = EpdFontFamily::REGULAR) const {
    auto it = fontMap.find(fontId);
    if (it == fontMap.end()) {
      return 0;
    }

    int w = 0;
    int h = 0;
    it->second.getTextDimensionsWithAppendSkippingSoftHyphen(text, length, appendCp, &w, &h, style);
    return w;
  }

  int getSpaceWidth(const int fontId) const { return getTextWidth(fontId, " "); }
  int getHyphenWidth(const int fontId) const { return getTextWidth(fontId, "-"); }

 private:
  std::map<int, EpdFontFamily> fontMap;
};
