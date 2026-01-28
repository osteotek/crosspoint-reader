#pragma once

#include <cstddef>

#include "EpdFontData.h"

class EpdFont {
  void getTextBounds(const char* string, int startX, int startY, int* minX, int* minY, int* maxX, int* maxY) const;
  void getTextBounds(const char* string, size_t length, int startX, int startY, int* minX, int* minY, int* maxX,
                     int* maxY) const;

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
};
