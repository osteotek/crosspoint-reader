#pragma once

// Stub header for GfxRenderer to enable testing without hardware dependencies
// This must be included before Hyphenator.cpp to shadow the real GfxRenderer.h

#include <EpdFontFamily.h>

class EInkDisplay; // Forward declaration (not used in tests)

class GfxRenderer {
 public:
  GfxRenderer() = default;
  ~GfxRenderer() = default;
  
  int getTextWidth(int fontId, const char* text, EpdFontStyle style = REGULAR) const {
    // Simple mock: count characters, each is 10 pixels, hyphen is 5
    int width = 0;
    const char* p = text;
    while (*p != '\0') {
      if (*p == '-') {
        width += 5;
      } else {
        width += 10;
      }
      p++;
    }
    return width;
  }
};
