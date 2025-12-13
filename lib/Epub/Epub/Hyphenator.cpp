#include "Hyphenator.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <initializer_list>
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

uint32_t toLowerLatin(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    return cp - 'A' + 'a';
  }
  return cp;
}

uint32_t toLowerCyrillic(uint32_t cp) {
  if (cp >= 0x0410 && cp <= 0x042F) {
    return cp + 0x20;
  }
  if (cp == 0x0401) {
    return 0x0451;
  }
  return cp;
}

bool isLatinLetter(const uint32_t cp) { return (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z'); }

bool isLatinVowel(uint32_t cp) {
  cp = toLowerLatin(cp);
  return cp == 'a' || cp == 'e' || cp == 'i' || cp == 'o' || cp == 'u' || cp == 'y';
}

bool isLatinConsonant(const uint32_t cp) { return isLatinLetter(cp) && !isLatinVowel(cp); }

bool isCyrillicLetter(const uint32_t cp) { return (cp >= 0x0400 && cp <= 0x052F); }

bool isCyrillicVowel(uint32_t cp) {
  cp = toLowerCyrillic(cp);
  switch (cp) {
    case 0x0430:  // а
    case 0x0435:  // е
    case 0x0451:  // ё
    case 0x0438:  // и
    case 0x043E:  // о
    case 0x0443:  // у
    case 0x044B:  // ы
    case 0x044D:  // э
    case 0x044E:  // ю
    case 0x044F:  // я
      return true;
    default:
      return false;
  }
}

bool isCyrillicConsonant(const uint32_t cp) { return isCyrillicLetter(cp) && !isCyrillicVowel(cp); }

bool isSoftOrHardSign(const uint32_t cp) { return cp == 0x044C || cp == 0x042C || cp == 0x044A || cp == 0x042A; }

bool isAlphabetic(const uint32_t cp) { return isLatinLetter(cp) || isCyrillicLetter(cp); }

bool isVowel(const uint32_t cp) { return isLatinVowel(cp) || isCyrillicVowel(cp); }

enum class Script { Latin, Cyrillic, Mixed };

Script detectScript(const std::vector<CodepointInfo>& cps) {
  bool hasLatin = false;
  bool hasCyrillic = false;
  for (const auto& info : cps) {
    if (isLatinLetter(info.value)) {
      hasLatin = true;
    } else if (isCyrillicLetter(info.value)) {
      hasCyrillic = true;
    }
  }

  if (hasLatin && !hasCyrillic) {
    return Script::Latin;
  }
  if (!hasLatin && hasCyrillic) {
    return Script::Cyrillic;
  }
  return Script::Mixed;
}

bool isEnglishDiphthong(const uint32_t first, const uint32_t second) {
  if (!isLatinLetter(first) || !isLatinLetter(second)) {
    return false;
  }
  const auto f = static_cast<char>(toLowerLatin(first));
  const auto s = static_cast<char>(toLowerLatin(second));
  switch (f) {
    case 'a':
      return s == 'i' || s == 'y' || s == 'u';
    case 'e':
      return s == 'a' || s == 'e' || s == 'i' || s == 'o' || s == 'u' || s == 'y';
    case 'i':
      return s == 'e' || s == 'u' || s == 'a';
    case 'o':
      return s == 'a' || s == 'e' || s == 'i' || s == 'o' || s == 'u' || s == 'y';
    case 'u':
      return s == 'i' || s == 'a' || s == 'e';
  }
  return false;
}

char lowerLatinChar(const uint32_t cp) {
  if (!isLatinLetter(cp)) {
    return 0;
  }
  return static_cast<char>(toLowerLatin(cp));
}

bool isEnglishApproximantChar(const char c) { return c == 'l' || c == 'r' || c == 'w' || c == 'y'; }

bool isEnglishStopChar(const char c) {
  switch (c) {
    case 'p':
    case 'b':
    case 't':
    case 'd':
    case 'k':
    case 'g':
    case 'c':
    case 'q':
      return true;
    default:
      return false;
  }
}

bool isEnglishFricativeChar(const char c) {
  switch (c) {
    case 'f':
    case 'v':
    case 's':
    case 'z':
    case 'h':
    case 'x':
      return true;
    default:
      return false;
  }
}

struct CharPair {
  char first;
  char second;
};

bool matchesDigraph(const char first, const char second, const std::initializer_list<CharPair>& pairs) {
  for (const auto& pair : pairs) {
    if (pair.first == first && pair.second == second) {
      return true;
    }
  }
  return false;
}

bool isValidEnglishOnsetBigram(const uint32_t firstCp, const uint32_t secondCp) {
  const char first = lowerLatinChar(firstCp);
  const char second = lowerLatinChar(secondCp);
  if (!first || !second) {
    return false;
  }

  if (matchesDigraph(first, second,
                     {{'c', 'h'}, {'s', 'h'}, {'t', 'h'}, {'p', 'h'}, {'w', 'h'}, {'w', 'r'}, {'k', 'n'},
                      {'g', 'n'}, {'p', 's'}, {'p', 't'}, {'p', 'n'}, {'r', 'h'}})) {
    return true;
  }

  if (isEnglishStopChar(first) && isEnglishApproximantChar(second)) {
    return true;
  }

  if (isEnglishFricativeChar(first) && isEnglishApproximantChar(second)) {
    return true;
  }

  if (first == 's' && (second == 'p' || second == 't' || second == 'k' || second == 'm' || second == 'n' ||
                       second == 'f' || second == 'l' || second == 'w' || second == 'c')) {
    return true;
  }

  if (second == 'y' && (first == 'p' || first == 'b' || first == 't' || first == 'd' || first == 'f' || first == 'k' ||
                        first == 'g' || first == 'h' || first == 'm' || first == 'n' || first == 'l' || first == 's')) {
    return true;
  }

  return false;
}

bool isValidEnglishOnsetTrigram(const uint32_t firstCp, const uint32_t secondCp, const uint32_t thirdCp) {
  const char first = lowerLatinChar(firstCp);
  const char second = lowerLatinChar(secondCp);
  const char third = lowerLatinChar(thirdCp);
  if (!first || !second || !third) {
    return false;
  }

  if (first == 's') {
    if (second == 'p' && (third == 'l' || third == 'r' || third == 'w')) {
      return true;
    }
    if (second == 't' && (third == 'r' || third == 'w' || third == 'y')) {
      return true;
    }
    if (second == 'k' && (third == 'l' || third == 'r' || third == 'w')) {
      return true;
    }
    if (second == 'c' && (third == 'l' || third == 'r')) {
      return true;
    }
    if (second == 'f' && third == 'r') {
      return true;
    }
    if (second == 'h' && third == 'r') {
      return true;
    }
  }

  if (first == 't' && second == 'h' && third == 'r') {
    return true;
  }

  return false;
}

bool englishClusterIsValidOnset(const std::vector<CodepointInfo>& cps, const size_t start, const size_t end) {
  if (start >= end) {
    return false;
  }

  for (size_t i = start; i < end; ++i) {
    const char ch = lowerLatinChar(cps[i].value);
    if (!ch) {
      return false;
    }
    if (!isLatinConsonant(cps[i].value) && ch != 'y') {
      return false;
    }
  }

  const size_t len = end - start;
  if (len == 1) {
    return true;
  }
  if (len == 2) {
    return isValidEnglishOnsetBigram(cps[start].value, cps[start + 1].value);
  }
  if (len == 3) {
    return isValidEnglishOnsetTrigram(cps[start].value, cps[start + 1].value, cps[start + 2].value);
  }

  return false;
}

size_t englishOnsetLength(const std::vector<CodepointInfo>& cps, const size_t clusterStart, const size_t clusterEnd) {
  const size_t clusterLen = clusterEnd - clusterStart;
  if (clusterLen == 0) {
    return 0;
  }

  const size_t maxLen = std::min<size_t>(3, clusterLen);
  for (size_t len = maxLen; len >= 1; --len) {
    const size_t suffixStart = clusterEnd - len;
    if (englishClusterIsValidOnset(cps, suffixStart, clusterEnd)) {
      return len;
    }
  }

  return 1;
}

bool isRussianPrefixConsonant(uint32_t cp) {
  cp = toLowerCyrillic(cp);
  return cp == 0x0432 || cp == 0x0437 || cp == 0x0441;  // в, з, с
}

bool isRussianSibilant(uint32_t cp) {
  cp = toLowerCyrillic(cp);
  switch (cp) {
    case 0x0437:  // з
    case 0x0441:  // с
    case 0x0436:  // ж
    case 0x0448:  // ш
    case 0x0449:  // щ
    case 0x0447:  // ч
    case 0x0446:  // ц
      return true;
    default:
      return false;
  }
}

bool isRussianStop(uint32_t cp) {
  cp = toLowerCyrillic(cp);
  switch (cp) {
    case 0x0431:  // б
    case 0x0433:  // г
    case 0x0434:  // д
    case 0x043F:  // п
    case 0x0442:  // т
    case 0x043A:  // к
      return true;
    default:
      return false;
  }
}

int russianSonority(uint32_t cp) {
  cp = toLowerCyrillic(cp);
  switch (cp) {
    case 0x043B:  // л
    case 0x0440:  // р
    case 0x0439:  // й
      return 4;
    case 0x043C:  // м
    case 0x043D:  // н
      return 3;
    case 0x0432:  // в
    case 0x0437:  // з
    case 0x0436:  // ж
      return 2;
    case 0x0444:  // ф
    case 0x0441:  // с
    case 0x0448:  // ш
    case 0x0449:  // щ
    case 0x0447:  // ч
    case 0x0446:  // ц
    case 0x0445:  // х
      return 1;
    case 0x0431:  // б
    case 0x0433:  // г
    case 0x0434:  // д
    case 0x043F:  // п
    case 0x0442:  // т
    case 0x043A:  // к
      return 0;
    default:
      return 1;
  }
}

bool russianClusterIsValidOnset(const std::vector<CodepointInfo>& cps, const size_t start, const size_t end) {
  if (start >= end) {
    return false;
  }

  for (size_t i = start; i < end; ++i) {
    const auto cp = cps[i].value;
    if (!isCyrillicConsonant(cp) || isSoftOrHardSign(cp)) {
      return false;
    }
  }

  if (end - start == 1) {
    return true;
  }

  for (size_t i = start; i + 1 < end; ++i) {
    const uint32_t current = cps[i].value;
    const uint32_t next = cps[i + 1].value;
    const int currentRank = russianSonority(current);
    const int nextRank = russianSonority(next);
    if (currentRank > nextRank) {
      const bool atClusterStart = (i == start);
      const bool prefixAllowance = atClusterStart && isRussianPrefixConsonant(current);
      const bool sibilantAllowance = isRussianSibilant(current) && isRussianStop(next);
      if (!prefixAllowance && !sibilantAllowance) {
        return false;
      }
    }
  }

  return true;
}

size_t russianOnsetLength(const std::vector<CodepointInfo>& cps, const size_t clusterStart, const size_t clusterEnd) {
  const size_t clusterLen = clusterEnd - clusterStart;
  if (clusterLen == 0) {
    return 0;
  }

  const size_t maxLen = std::min<size_t>(4, clusterLen);
  for (size_t len = maxLen; len >= 1; --len) {
    const size_t suffixStart = clusterEnd - len;
    if (russianClusterIsValidOnset(cps, suffixStart, clusterEnd)) {
      return len;
    }
  }

  return 1;
}

bool nextToApostrophe(const std::vector<CodepointInfo>& cps, size_t index) {
  if (index == 0 || index >= cps.size()) {
    return false;
  }
  const auto left = cps[index - 1].value;
  const auto right = cps[index].value;
  return left == '\'' || right == '\'';
}

bool nextToSoftSign(const std::vector<CodepointInfo>& cps, size_t index) {
  if (index == 0 || index >= cps.size()) {
    return false;
  }
  const auto left = cps[index - 1].value;
  const auto right = cps[index].value;
  return isSoftOrHardSign(left) || isSoftOrHardSign(right);
}

std::vector<size_t> englishBreakIndexes(const std::vector<CodepointInfo>& cps) {
  std::vector<size_t> indexes;
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return indexes;
  }

  std::vector<size_t> vowelPositions;
  vowelPositions.reserve(cps.size());
  for (size_t i = 0; i < cps.size(); ++i) {
    if (isLatinVowel(cps[i].value)) {
      vowelPositions.push_back(i);
    }
  }

  if (vowelPositions.size() < 2) {
    return indexes;
  }

  for (size_t v = 0; v + 1 < vowelPositions.size(); ++v) {
    const size_t leftVowel = vowelPositions[v];
    const size_t rightVowel = vowelPositions[v + 1];

    if (rightVowel - leftVowel == 1) {
      if (!isEnglishDiphthong(cps[leftVowel].value, cps[rightVowel].value) &&
          rightVowel >= MIN_PREFIX_CP && cps.size() - rightVowel >= MIN_SUFFIX_CP &&
          !nextToApostrophe(cps, rightVowel)) {
        indexes.push_back(rightVowel);
      }
      continue;
    }

    const size_t clusterStart = leftVowel + 1;
    const size_t clusterEnd = rightVowel;
    const size_t onsetLen = englishOnsetLength(cps, clusterStart, clusterEnd);
    size_t breakIndex = clusterEnd - onsetLen;

    if (breakIndex < MIN_PREFIX_CP || cps.size() - breakIndex < MIN_SUFFIX_CP) {
      continue;
    }
    if (nextToApostrophe(cps, breakIndex)) {
      continue;
    }
    indexes.push_back(breakIndex);
  }

  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  return indexes;
}

std::vector<size_t> russianBreakIndexes(const std::vector<CodepointInfo>& cps) {
  std::vector<size_t> indexes;
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return indexes;
  }

  std::vector<size_t> vowelPositions;
  vowelPositions.reserve(cps.size());
  for (size_t i = 0; i < cps.size(); ++i) {
    if (isCyrillicVowel(cps[i].value)) {
      vowelPositions.push_back(i);
    }
  }

  if (vowelPositions.size() < 2) {
    return indexes;
  }

  for (size_t v = 0; v + 1 < vowelPositions.size(); ++v) {
    const size_t leftVowel = vowelPositions[v];
    const size_t rightVowel = vowelPositions[v + 1];

    if (rightVowel - leftVowel == 1) {
      if (rightVowel >= MIN_PREFIX_CP && cps.size() - rightVowel >= MIN_SUFFIX_CP &&
          !nextToSoftSign(cps, rightVowel)) {
        indexes.push_back(rightVowel);
      }
      continue;
    }

    const size_t clusterStart = leftVowel + 1;
    const size_t clusterEnd = rightVowel;
    const size_t onsetLen = russianOnsetLength(cps, clusterStart, clusterEnd);
    size_t breakIndex = clusterEnd - onsetLen;

    if (breakIndex < MIN_PREFIX_CP || cps.size() - breakIndex < MIN_SUFFIX_CP) {
      continue;
    }
    if (nextToSoftSign(cps, breakIndex)) {
      continue;
    }
    indexes.push_back(breakIndex);
  }

  std::sort(indexes.begin(), indexes.end());
  indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
  return indexes;
}

std::vector<size_t> fallbackBreakIndexes(const std::vector<CodepointInfo>& cps) {
  std::vector<size_t> indexes;
  if (cps.size() < MIN_PREFIX_CP + MIN_SUFFIX_CP) {
    return indexes;
  }

  for (size_t i = MIN_PREFIX_CP; i + MIN_SUFFIX_CP <= cps.size(); ++i) {
    const uint32_t prev = cps[i - 1].value;
    const uint32_t curr = cps[i].value;

    if (!isAlphabetic(prev) || !isAlphabetic(curr)) {
      continue;
    }

    const bool prevVowel = isVowel(prev);
    const bool currVowel = isVowel(curr);
    const bool prevConsonant = !prevVowel;
    const bool currConsonant = !currVowel;

    const bool breakable = (prevVowel && currConsonant) || (prevConsonant && currConsonant) ||
                           (prevConsonant && currVowel);

    if (breakable) {
      indexes.push_back(i);
    }
  }

  return indexes;
}

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
    if (!isAlphabetic(info.value)) {
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

  switch (detectScript(cps)) {
    case Script::Latin:
      indexes = englishBreakIndexes(cps);
      break;
    case Script::Cyrillic:
      indexes = russianBreakIndexes(cps);
      break;
    case Script::Mixed:
      break;
  }

  if (indexes.empty()) {
    indexes = fallbackBreakIndexes(cps);
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
        if (adjustedWidth > 0 && prefixWidth > adjustedWidth) {
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
