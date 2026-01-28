#include "EpdFont.h"

#include <Utf8.h>

#include <algorithm>
#include <cstring>

void EpdFont::getTextBounds(const char* string, const int startX, const int startY, int* minX, int* minY, int* maxX,
                            int* maxY) const {
  *minX = startX;
  *minY = startY;
  *maxX = startX;
  *maxY = startY;

  if (*string == '\0') {
    return;
  }

  if (isAsciiString(string)) {
    getTextBoundsAscii(string, std::strlen(string), startX, startY, minX, minY, maxX, maxY);
    return;
  }

  int cursorX = startX;
  const int cursorY = startY;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&string)))) {
    const EpdGlyph* glyph = getGlyph(cp);

    if (!glyph) {
      glyph = getGlyph(REPLACEMENT_GLYPH);
    }

    if (!glyph) {
      // TODO: Better handle this?
      continue;
    }

    *minX = std::min(*minX, cursorX + glyph->left);
    *maxX = std::max(*maxX, cursorX + glyph->left + glyph->width);
    *minY = std::min(*minY, cursorY + glyph->top - glyph->height);
    *maxY = std::max(*maxY, cursorY + glyph->top);
    cursorX += glyph->advanceX;
  }
}

void EpdFont::getTextBounds(const char* string, const size_t length, const int startX, const int startY, int* minX,
                            int* minY, int* maxX, int* maxY) const {
  *minX = startX;
  *minY = startY;
  *maxX = startX;
  *maxY = startY;

  if (length == 0 || *string == '\0') {
    return;
  }

  if (isAsciiString(string, length)) {
    getTextBoundsAscii(string, length, startX, startY, minX, minY, maxX, maxY);
    return;
  }

  int cursorX = startX;
  const int cursorY = startY;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
  const unsigned char* end = ptr + length;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(&ptr, end))) {
    const EpdGlyph* glyph = getGlyph(cp);

    if (!glyph) {
      glyph = getGlyph(REPLACEMENT_GLYPH);
    }

    if (!glyph) {
      // TODO: Better handle this?
      continue;
    }

    *minX = std::min(*minX, cursorX + glyph->left);
    *maxX = std::max(*maxX, cursorX + glyph->left + glyph->width);
    *minY = std::min(*minY, cursorY + glyph->top - glyph->height);
    *maxY = std::max(*maxY, cursorY + glyph->top);
    cursorX += glyph->advanceX;
  }
}

void EpdFont::getTextBoundsAscii(const char* string, const size_t length, const int startX, const int startY,
                                 int* minX, int* minY, int* maxX, int* maxY) const {
  *minX = startX;
  *minY = startY;
  *maxX = startX;
  *maxY = startY;

  if (length == 0) {
    return;
  }

  ensureAsciiCache();
  int cursorX = startX;
  const int cursorY = startY;
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = ptr[i];
    if (c == 0) {
      break;
    }
    const EpdGlyph* glyph = asciiGlyphCache[c];
    if (!glyph) {
      glyph = replacementGlyph;
    }
    if (!glyph) {
      continue;
    }
    *minX = std::min(*minX, cursorX + glyph->left);
    *maxX = std::max(*maxX, cursorX + glyph->left + glyph->width);
    *minY = std::min(*minY, cursorY + glyph->top - glyph->height);
    *maxY = std::max(*maxY, cursorY + glyph->top);
    cursorX += glyph->advanceX;
  }
}

void EpdFont::ensureAsciiCache() const {
  if (asciiCacheReady) {
    return;
  }
  for (size_t i = 0; i < asciiGlyphCache.size(); ++i) {
    asciiGlyphCache[i] = getGlyph(static_cast<uint32_t>(i));
  }
  replacementGlyph = getGlyph(REPLACEMENT_GLYPH);
  asciiCacheReady = true;
}

bool EpdFont::isAsciiString(const char* string) const {
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
  while (*ptr) {
    if (*ptr & 0x80) {
      return false;
    }
    ++ptr;
  }
  return true;
}

bool EpdFont::isAsciiString(const char* string, const size_t length) const {
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
  for (size_t i = 0; i < length; ++i) {
    if (ptr[i] & 0x80) {
      return false;
    }
  }
  return true;
}

void EpdFont::getTextDimensions(const char* string, int* w, int* h) const {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;

  getTextBounds(string, 0, 0, &minX, &minY, &maxX, &maxY);

  *w = maxX - minX;
  *h = maxY - minY;
}

void EpdFont::getTextDimensions(const char* string, const size_t length, int* w, int* h) const {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;

  getTextBounds(string, length, 0, 0, &minX, &minY, &maxX, &maxY);

  *w = maxX - minX;
  *h = maxY - minY;
}

void EpdFont::getTextDimensionsWithAppend(const char* string, const size_t length, const uint32_t appendCp, int* w,
                                          int* h) const {
  int minX = 0, minY = 0, maxX = 0, maxY = 0;

  if ((length == 0 || *string == '\0') && appendCp == 0) {
    *w = 0;
    *h = 0;
    return;
  }

  int cursorX = 0;
  const int cursorY = 0;
  if (isAsciiString(string, length)) {
    ensureAsciiCache();
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
    for (size_t i = 0; i < length; ++i) {
      const unsigned char c = ptr[i];
      if (c == 0) {
        break;
      }
      const EpdGlyph* glyph = asciiGlyphCache[c];
      if (!glyph) {
        glyph = replacementGlyph;
      }
      if (!glyph) {
        continue;
      }
      minX = std::min(minX, cursorX + glyph->left);
      maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
      minY = std::min(minY, cursorY + glyph->top - glyph->height);
      maxY = std::max(maxY, cursorY + glyph->top);
      cursorX += glyph->advanceX;
    }
  } else {
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
    const unsigned char* end = ptr + length;
    uint32_t cp;
    while (ptr < end && (cp = utf8NextCodepoint(&ptr, end))) {
      const EpdGlyph* glyph = getGlyph(cp);

      if (!glyph) {
        glyph = getGlyph(REPLACEMENT_GLYPH);
      }

      if (!glyph) {
        continue;
      }

      minX = std::min(minX, cursorX + glyph->left);
      maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
      minY = std::min(minY, cursorY + glyph->top - glyph->height);
      maxY = std::max(maxY, cursorY + glyph->top);
      cursorX += glyph->advanceX;
    }
  }

  if (appendCp != 0) {
    const EpdGlyph* glyph = nullptr;
    if (appendCp < 0x80) {
      ensureAsciiCache();
      glyph = asciiGlyphCache[appendCp];
    } else {
      glyph = getGlyph(appendCp);
    }

    if (!glyph) {
      glyph = getGlyph(REPLACEMENT_GLYPH);
    }

    if (glyph) {
      minX = std::min(minX, cursorX + glyph->left);
      maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
      minY = std::min(minY, cursorY + glyph->top - glyph->height);
      maxY = std::max(maxY, cursorY + glyph->top);
      cursorX += glyph->advanceX;
    }
  }

  *w = maxX - minX;
  *h = maxY - minY;
}

void EpdFont::getTextDimensionsWithAppendSkippingSoftHyphen(const char* string, const size_t length,
                                                            const uint32_t appendCp, int* w, int* h) const {
  constexpr uint32_t kSoftHyphen = 0x00AD;
  int minX = 0, minY = 0, maxX = 0, maxY = 0;

  if ((length == 0 || *string == '\0') && appendCp == 0) {
    *w = 0;
    *h = 0;
    return;
  }

  int cursorX = 0;
  const int cursorY = 0;
  if (isAsciiString(string, length)) {
    ensureAsciiCache();
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
    for (size_t i = 0; i < length; ++i) {
      const unsigned char c = ptr[i];
      if (c == 0) {
        break;
      }
      const EpdGlyph* glyph = asciiGlyphCache[c];
      if (!glyph) {
        glyph = replacementGlyph;
      }
      if (!glyph) {
        continue;
      }
      minX = std::min(minX, cursorX + glyph->left);
      maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
      minY = std::min(minY, cursorY + glyph->top - glyph->height);
      maxY = std::max(maxY, cursorY + glyph->top);
      cursorX += glyph->advanceX;
    }
  } else {
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(string);
    const unsigned char* end = ptr + length;
    uint32_t cp;
    while (ptr < end && (cp = utf8NextCodepoint(&ptr, end))) {
      if (cp == kSoftHyphen) {
        continue;
      }
      const EpdGlyph* glyph = getGlyph(cp);

      if (!glyph) {
        glyph = getGlyph(REPLACEMENT_GLYPH);
      }

      if (!glyph) {
        continue;
      }

      minX = std::min(minX, cursorX + glyph->left);
      maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
      minY = std::min(minY, cursorY + glyph->top - glyph->height);
      maxY = std::max(maxY, cursorY + glyph->top);
      cursorX += glyph->advanceX;
    }
  }

  if (appendCp != 0) {
    const EpdGlyph* glyph = nullptr;
    if (appendCp < 0x80) {
      ensureAsciiCache();
      glyph = asciiGlyphCache[appendCp];
    } else {
      glyph = getGlyph(appendCp);
    }

    if (!glyph) {
      glyph = getGlyph(REPLACEMENT_GLYPH);
    }

    if (glyph) {
      minX = std::min(minX, cursorX + glyph->left);
      maxX = std::max(maxX, cursorX + glyph->left + glyph->width);
      minY = std::min(minY, cursorY + glyph->top - glyph->height);
      maxY = std::max(maxY, cursorY + glyph->top);
      cursorX += glyph->advanceX;
    }
  }

  *w = maxX - minX;
  *h = maxY - minY;
}

bool EpdFont::hasPrintableChars(const char* string) const {
  int w = 0, h = 0;

  getTextDimensions(string, &w, &h);

  return w > 0 || h > 0;
}

bool EpdFont::hasPrintableChars(const char* string, const size_t length) const {
  int w = 0, h = 0;

  getTextDimensions(string, length, &w, &h);

  return w > 0 || h > 0;
}

const EpdGlyph* EpdFont::getGlyph(const uint32_t cp) const {
  const EpdUnicodeInterval* intervals = data->intervals;
  const int count = data->intervalCount;

  if (count == 0) return nullptr;

  // Binary search for O(log n) lookup instead of O(n)
  // Critical for Korean fonts with many unicode intervals
  int left = 0;
  int right = count - 1;

  while (left <= right) {
    const int mid = left + (right - left) / 2;
    const EpdUnicodeInterval* interval = &intervals[mid];

    if (cp < interval->first) {
      right = mid - 1;
    } else if (cp > interval->last) {
      left = mid + 1;
    } else {
      // Found: cp >= interval->first && cp <= interval->last
      return &data->glyph[interval->offset + (cp - interval->first)];
    }
  }

  return nullptr;
}
