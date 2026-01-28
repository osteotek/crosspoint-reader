#pragma once

#include <array>
#include <cstddef>

#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;
  void getTextBounds(const char* string, size_t length, int startX, int startY, int* minX, int* minY, int* maxX,
                     int* maxY) const;
  void getTextBoundsAscii(const char* string, size_t length, int startX, int startY, int* minX, int* minY, int* maxX,
                          int* maxY) const;
  void ensureAsciiCache() const;
  bool isAsciiString(const char* string) const;
  bool isAsciiString(const char* string, size_t length) const;

 public:
 const EpdFontData* data;
  explicit EpdFont(const EpdFontData* data) : data(data) {}
  ~EpdFont() = default;
  void getTextDimensions(const char* string, int* w, int* h) const;
  void getTextDimensions(const char* string, size_t length, int* w, int* h) const;
  void getTextDimensionsWithAppend(const char* string, size_t length, uint32_t appendCp, int* w, int* h) const;
  void getTextDimensionsWithAppendSkippingSoftHyphen(const char* string, size_t length, uint32_t appendCp, int* w,
                                                     int* h) const;
  bool hasPrintableChars(const char* string) const;
  bool hasPrintableChars(const char* string, size_t length) const;

  const EpdGlyph* getGlyph(uint32_t cp) const;

 private:
  mutable bool asciiCacheReady = false;
  mutable std::array<const EpdGlyph*, 128> asciiGlyphCache = {};
  mutable const EpdGlyph* replacementGlyph = nullptr;
};
