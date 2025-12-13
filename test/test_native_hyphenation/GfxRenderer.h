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
    // Mock implementation: count UTF-8 codepoints, each is 10 pixels, hyphen is 5
    // This properly handles multi-byte UTF-8 sequences
    int width = 0;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (*p != '\0') {
      if (*p == '-') {
        width += 5;
        p++;
      } else {
        // Count UTF-8 codepoint bytes
        if (*p < 0x80) {
          // 1-byte ASCII
          p++;
        } else if ((*p & 0xE0) == 0xC0) {
          // 2-byte sequence
          p += 2;
        } else if ((*p & 0xF0) == 0xE0) {
          // 3-byte sequence
          p += 3;
        } else if ((*p & 0xF8) == 0xF0) {
          // 4-byte sequence
          p += 4;
        } else {
          // Invalid UTF-8, skip one byte
          p++;
        }
        width += 10;
      }
    }
    return width;
  }
};
