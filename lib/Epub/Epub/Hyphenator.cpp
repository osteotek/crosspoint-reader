#include "Hyphenator.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <vector>

#include <Utf8.h>

namespace {
struct CodepointInfo {
  uint32_t value;
  size_t byteOffset;
};

constexpr size_t MIN_PREFIX_CP = 3;
constexpr size_t MIN_SUFFIX_CP = 2;

bool isLatinLetter(const uint32_t cp) {
  return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z');
}

bool isLatinVowel(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    cp = cp - 'A' + 'a';
  }
  return cp == 'a' || cp == 'e' || cp == 'i' || cp == 'o' || cp == 'u' || cp == 'y';
}

bool isCyrillicLetter(const uint32_t cp) {
  return (cp >= 0x0410 && cp <= 0x044F) || cp == 0x0401 || cp == 0x0451;
}

bool isCyrillicVowel(const uint32_t cp) {
  switch (cp) {
    case 0x0410:  // А
    case 0x0430:  // а
    case 0x0415:  // Е
    case 0x0435:  // е
    case 0x0401:  // Ё
    case 0x0451:  // ё
    case 0x0418:  // И
    case 0x0438:  // и
    case 0x041E:  // О
    case 0x043E:  // о
    case 0x0423:  // У
    case 0x0443:  // у
    case 0x042B:  // Ы
    case 0x044B:  // ы
    case 0x042D:  // Э
    case 0x044D:  // э
    case 0x042E:  // Ю
    case 0x044E:  // ю
    case 0x042F:  // Я
    case 0x044F:  // я
      return true;
    default:
      return false;
  }
}

bool isLetter(const uint32_t cp) { return isLatinLetter(cp) || isCyrillicLetter(cp); }

bool isVowel(const uint32_t cp) { return isLatinVowel(cp) || isCyrillicVowel(cp); }

std::vector<CodepointInfo> collectCodepoints(const std::string& word) {
  std::vector<CodepointInfo> cps;
  cps.reserve(word.size());

  const unsigned char* base = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* ptr = base;
  while (*ptr != 0) {
    const unsigned char* current = ptr;
    const uint32_t cp = utf8NextCodepoint(&ptr);
    cps.push_back({cp, static_cast<size_t>(current - base)});
  }

  return cps;
}

bool hasOnlyAlphabetic(const std::vector<CodepointInfo>& cps) {
  if (cps.empty()) {
    return false;
  }

  for (const auto& info : cps) {
    if (!isLetter(info.value)) {
      return false;
    }
  }
  return true;
}

std::vector<size_t> collectBreakIndexes(const std::vector<CodepointInfo>& cps) {
  std::vector<size_t> indexes;
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return indexes;
  }

  for (size_t i = MIN_PREFIX_CP; i + MIN_SUFFIX_CP <= cps.size(); ++i) {
    const uint32_t prev = cps[i - 1].value;
    const uint32_t curr = cps[i].value;

    if (!isLetter(prev) || !isLetter(curr)) {
      continue;
    }

    const bool prevVowel = isVowel(prev);
    const bool currVowel = isVowel(curr);
    const bool prevConsonant = isLetter(prev) && !prevVowel;
    const bool currConsonant = isLetter(curr) && !currVowel;

    const bool breakable = (prevVowel && currConsonant) || (prevConsonant && currConsonant) ||
                           (prevConsonant && currVowel);

    if (breakable) {
      indexes.push_back(i);
    }
  }

  return indexes;
}

size_t byteOffsetForIndex(const std::vector<CodepointInfo>& cps, const size_t index) {
  if (index >= cps.size()) {
    return cps.empty() ? 0 : cps.back().byteOffset;
  }
  return cps[index].byteOffset;
}

std::string slice(const std::string& word, const size_t startByte, const size_t endByte) {
  if (startByte >= endByte || startByte >= word.size()) {
    return std::string();
  }
  const size_t boundedEnd = std::min(endByte, word.size());
  return word.substr(startByte, boundedEnd - startByte);
}

}  // namespace

bool Hyphenator::splitWord(const GfxRenderer& renderer, const int fontId, const std::string& word,
                           const EpdFontStyle style, const int availableWidth, HyphenationResult* result,
                           const bool force) {
  if (!result || word.empty()) {
    return false;
  }

  auto cps = collectCodepoints(word);
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return false;
  }

  if (!force && !hasOnlyAlphabetic(cps)) {
    return false;
  }

  const auto breakIndexes = collectBreakIndexes(cps);
  const int hyphenWidth = renderer.getTextWidth(fontId, "-", style);
  const int adjustedWidth = availableWidth - hyphenWidth;

  size_t chosenIndex = std::numeric_limits<size_t>::max();

  if (adjustedWidth > 0) {
    for (const size_t idx : breakIndexes) {
      const size_t byteOffset = byteOffsetForIndex(cps, idx);
      const std::string prefix = word.substr(0, byteOffset);
      const int prefixWidth = renderer.getTextWidth(fontId, prefix.c_str(), style);
      if (prefixWidth <= adjustedWidth) {
        chosenIndex = idx;
      } else {
        break;
      }
    }
  }

  if (chosenIndex == std::numeric_limits<size_t>::max() && force) {
    for (size_t idx = MIN_PREFIX_CP; idx + MIN_SUFFIX_CP <= cps.size(); ++idx) {
      const size_t byteOffset = byteOffsetForIndex(cps, idx);
      const std::string prefix = word.substr(0, byteOffset);
      const int prefixWidth = renderer.getTextWidth(fontId, prefix.c_str(), style);
      if (adjustedWidth <= 0 || prefixWidth <= adjustedWidth) {
        chosenIndex = idx;
        if (adjustedWidth > 0 && prefixWidth >= adjustedWidth) {
          break;
        }
      }
    }
  }

  if (chosenIndex == std::numeric_limits<size_t>::max()) {
    return false;
  }

  const size_t splitByte = byteOffsetForIndex(cps, chosenIndex);
  const std::string head = word.substr(0, splitByte);
  const std::string tail = slice(word, splitByte, word.size());

  if (head.empty() || tail.empty()) {
    return false;
  }

  result->head = head + "-";
  result->tail = tail;
  return true;
}
