#include "ParsedText.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <vector>

#include "hyphenation/Hyphenator.h"

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

constexpr float SCORE_INFTY = std::numeric_limits<float>::max();
constexpr float SCORE_OVERFULL = 1e12f;
constexpr float LAST_LINE_PENALTY_MULTIPLIER = 4.0f;
constexpr float LINE_PENALTY_MULTIPLIER = 2.0f;
constexpr float SHRINK_PENALTY_MULTIPLIER = 4.0f;
constexpr float SHRINKABILITY = 1.0f / 3.0f;

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the rendered width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextWidth(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextWidth(fontId, sanitized.c_str(), style);
}

// Simplified Minikin-style break candidate over a word-based layout model.
// Each candidate represents either a word boundary or a hyphenation point.
struct OptimalBreakCandidate {
  size_t wordIndex;     // Index in the current words list where the candidate belongs.
  size_t byteOffset;    // Byte offset inside the word if this is a hyphenation candidate.
  bool isHyphenation;   // True when this candidate splits within a word.
  bool insertHyphen;    // True when we should append a visible '-' to the prefix.
  float preBreak;       // Width of text before this candidate (without breaking here).
  float postBreak;      // Width of text if we break here (may include hyphen width).
  float penalty;        // Penalty for taking this break (hyphenation penalty).
  int preSpaceCount;    // Spaces before this break, used for shrink calculations.
  int postSpaceCount;   // Spaces after this break, used for shrink calculations.
};

// DP state for optimal line breaks.
struct OptimalBreakData {
  float score;       // Best score up to this candidate.
  size_t prev;       // Index of previous candidate in the optimal path.
  size_t lineNumber; // Computed line number at this candidate.
};

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle) {
  if (word.empty()) return;

  words.push_back(std::move(word));
  wordStyles.push_back(fontStyle);
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  applyParagraphIndent();

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  auto wordWidths = calculateWordWidths(renderer, fontId);
  std::vector<size_t> lineBreakIndices = computeLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths);
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, wordWidths, lineBreakIndices, processLine);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  const size_t totalWordCount = words.size();

  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(totalWordCount);

  auto wordsIt = words.begin();
  auto wordStylesIt = wordStyles.begin();

  while (wordsIt != words.end()) {
    wordWidths.push_back(measureWordWidth(renderer, fontId, *wordsIt, *wordStylesIt));

    std::advance(wordsIt, 1);
    std::advance(wordStylesIt, 1);
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  const int spaceWidth, std::vector<uint16_t>& wordWidths) {
  if (words.empty()) {
    return {};
  }

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    while (wordWidths[i] > pageWidth) {
      if (!hyphenateWordAtIndex(i, pageWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  std::vector<std::string> wordVec(words.begin(), words.end());
  std::vector<EpdFontFamily::Style> styleVec(wordStyles.begin(), wordStyles.end());
  const size_t totalWordCount = wordVec.size();
  if (totalWordCount == 0) {
    return {};
  }

  // Precompute cumulative widths assuming a single space between words.
  std::vector<float> baseCumulative(totalWordCount + 1, 0.0f);
  for (size_t i = 0; i < totalWordCount; ++i) {
    baseCumulative[i + 1] = baseCumulative[i] + static_cast<float>(wordWidths[i]);
    if (i + 1 < totalWordCount) {
      baseCumulative[i + 1] += static_cast<float>(spaceWidth);
    }
  }

  // Track the number of spaces before each word boundary for shrink penalties.
  std::vector<int> spaceCounts(totalWordCount + 1, 0);
  for (size_t i = 0; i < totalWordCount; ++i) {
    spaceCounts[i] = static_cast<int>(i);
  }
  spaceCounts[totalWordCount] = static_cast<int>(totalWordCount - 1);

  // Translate minikin penalty model into our word-based width units.
  const bool justified = style == TextBlock::JUSTIFIED;
  const float spaceWidthF = static_cast<float>(spaceWidth);
  const float pageWidthF = static_cast<float>(pageWidth);
  float hyphenPenalty = 0.5f * std::max(spaceWidthF, 1.0f) * pageWidthF;
  if (justified) {
    hyphenPenalty *= 0.25f;
  }
  const float linePenalty = justified ? 0.0f : hyphenPenalty * LINE_PENALTY_MULTIPLIER;

  // Build the candidate list in reading order. The DP expects monotonic widths.
  std::vector<OptimalBreakCandidate> candidates;
  candidates.reserve(totalWordCount * 2 + 1);

  auto pushBoundaryCandidate = [&](const size_t wordIndex) {
    // Word boundary candidates represent legal break points between words.
    OptimalBreakCandidate candidate;
    candidate.wordIndex = wordIndex;
    candidate.byteOffset = 0;
    candidate.isHyphenation = false;
    candidate.insertHyphen = false;
    candidate.preBreak = baseCumulative[wordIndex];
    candidate.postBreak = candidate.preBreak;
    candidate.penalty = 0.0f;
    candidate.preSpaceCount = spaceCounts[wordIndex];
    candidate.postSpaceCount = candidate.preSpaceCount;
    if (wordIndex > 0 && wordIndex < totalWordCount) {
      // Breaking between words doesn't consume the trailing space on the line.
      candidate.postBreak -= spaceWidthF;
      candidate.postSpaceCount -= 1;
    }
    candidates.push_back(candidate);
  };

  pushBoundaryCandidate(0);
  for (size_t i = 0; i < totalWordCount; ++i) {
    if (hyphenationEnabled) {
      // Hyphenation candidates are inserted inside the current word.
      auto breakInfos = Hyphenator::breakOffsets(wordVec[i], /*includeFallback=*/false);
      if (!breakInfos.empty()) {
        std::sort(breakInfos.begin(), breakInfos.end(),
                  [](const Hyphenator::BreakInfo& a, const Hyphenator::BreakInfo& b) {
                    return a.byteOffset < b.byteOffset;
                  });
        for (const auto& info : breakInfos) {
          if (info.byteOffset == 0 || info.byteOffset >= wordVec[i].size()) {
            continue;
          }
          const std::string prefix = wordVec[i].substr(0, info.byteOffset);
          const float prefixNoHyphen = static_cast<float>(
              measureWordWidth(renderer, fontId, prefix, styleVec[i], /*appendHyphen=*/false));
          const float prefixWithHyphen = static_cast<float>(
              measureWordWidth(renderer, fontId, prefix, styleVec[i], info.requiresInsertedHyphen));

          // preBreak represents the text width if we keep flowing past this point.
          // postBreak represents the text width if we actually break here.
          OptimalBreakCandidate candidate;
          candidate.wordIndex = i;
          candidate.byteOffset = info.byteOffset;
          candidate.isHyphenation = true;
          candidate.insertHyphen = info.requiresInsertedHyphen;
          candidate.preBreak = baseCumulative[i] + prefixNoHyphen;
          candidate.postBreak = baseCumulative[i] + prefixWithHyphen;
          candidate.penalty = hyphenPenalty;
          candidate.preSpaceCount = spaceCounts[i];
          candidate.postSpaceCount = spaceCounts[i];
          candidates.push_back(candidate);
        }
      }
    }
    pushBoundaryCandidate(i + 1);
  }

  if (candidates.size() < 2) {
    return {totalWordCount};
  }

  const size_t candidateCount = candidates.size();
  std::vector<OptimalBreakData> breaksData;
  breaksData.reserve(candidateCount);
  // The first candidate always begins the first line.
  breaksData.push_back({0.0f, 0, 0});

  size_t active = 0;
  const float maxShrink = justified ? SHRINKABILITY * spaceWidthF : 0.0f;

  for (size_t i = 1; i < candidateCount; ++i) {
    const bool atEnd = i == candidateCount - 1;
    float best = SCORE_INFTY;
    size_t bestPrev = 0;

    // The left edge represents the target line start for this candidate.
    float leftEdge = candidates[i].postBreak - pageWidthF;
    float bestHope = 0.0f;

    // Scan possible starts of the line from the active window.
    for (size_t j = active; j < i; ++j) {
      const float jScore = breaksData[j].score;
      if (jScore + bestHope >= best) {
        continue;
      }

      // delta is the difference between actual line width and target width.
      float delta = candidates[j].preBreak - leftEdge;
      float widthScore = 0.0f;
      float additionalPenalty = 0.0f;

      // Mirror Minikin's width scoring: overfull lines are heavily penalized.
      if ((atEnd || !justified) && delta < 0.0f) {
        widthScore = SCORE_OVERFULL;
      } else if (atEnd) {
        // Encourage fewer hyphens on the final line.
        additionalPenalty = LAST_LINE_PENALTY_MULTIPLIER * candidates[j].penalty;
      } else {
        widthScore = delta * delta;
        if (delta < 0.0f) {
          const float shrinkLimit =
              maxShrink * static_cast<float>(candidates[i].postSpaceCount - candidates[j].preSpaceCount);
          if (-delta < shrinkLimit) {
            // Small overflow can be handled by shrinking spaces (justified mode).
            widthScore *= SHRINK_PENALTY_MULTIPLIER;
          } else {
            widthScore = SCORE_OVERFULL;
          }
        }
      }

      // Update the active window when we are already overfull.
      if (delta < 0.0f) {
        active = j + 1;
      } else {
        bestHope = widthScore;
      }

      const float score = jScore + widthScore + additionalPenalty;
      if (score <= best) {
        best = score;
        bestPrev = j;
      }
    }

    // Store DP state for this candidate (score includes line and candidate penalty).
    breaksData.push_back(
        {best + candidates[i].penalty + linePenalty, bestPrev, breaksData[bestPrev].lineNumber + 1});
  }

  // Walk the optimal path from the end to collect chosen candidate indices.
  std::vector<size_t> breakCandidateIndices;
  for (size_t i = candidateCount - 1; i > 0; i = breaksData[i].prev) {
    breakCandidateIndices.push_back(i);
  }
  std::reverse(breakCandidateIndices.begin(), breakCandidateIndices.end());

  // Apply hyphenation splits that were chosen by the optimal path and translate to break indices.
  std::vector<size_t> lineBreakIndices;
  lineBreakIndices.reserve(breakCandidateIndices.size());
  size_t indexShift = 0;
  std::unordered_map<size_t, size_t> consumedOffsets;

  for (const size_t candidateIndex : breakCandidateIndices) {
    const OptimalBreakCandidate& candidate = candidates[candidateIndex];
    if (!candidate.isHyphenation) {
      // Word boundary: break right after the word (accounting for splits already inserted).
      lineBreakIndices.push_back(candidate.wordIndex + indexShift);
      continue;
    }

    // Hyphenation candidate: split the word and break after the inserted prefix.
    const size_t currentIndex = candidate.wordIndex + indexShift;
    const size_t consumed =
        consumedOffsets.count(candidate.wordIndex) ? consumedOffsets[candidate.wordIndex] : 0;
    const size_t relativeOffset = candidate.byteOffset - consumed;

    if (!splitWordAtIndex(currentIndex, relativeOffset, candidate.insertHyphen, renderer, fontId, wordWidths)) {
      // Fall back to breaking after the whole word if the split fails.
      lineBreakIndices.push_back(currentIndex + 1);
      continue;
    }

    // Track how much of the original word has already been consumed by prior splits.
    consumedOffsets[candidate.wordIndex] = candidate.byteOffset;
    indexShift += 1;
    lineBreakIndices.push_back(currentIndex + 1);
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent() {
  if (extraParagraphSpacing || words.empty()) {
    return;
  }

  if (style == TextBlock::JUSTIFIED || style == TextBlock::LEFT_ALIGN) {
    words.front().insert(0, "\xe2\x80\x83");
  }
}

bool ParsedText::splitWordAtIndex(const size_t wordIndex, const size_t byteOffset, const bool insertHyphen,
                                  const GfxRenderer& renderer, const int fontId,
                                  std::vector<uint16_t>& wordWidths) {
  // This is the low-level split used by the optimal breaker when a hyphenation candidate is chosen.
  if (wordIndex >= words.size()) {
    return false;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  std::advance(wordIt, wordIndex);
  std::advance(styleIt, wordIndex);

  if (byteOffset == 0 || byteOffset >= wordIt->size()) {
    return false;
  }

  // Split the word into prefix and remainder and insert a visible hyphen if requested.
  std::string remainder = wordIt->substr(byteOffset);
  wordIt->resize(byteOffset);
  if (insertHyphen) {
    wordIt->push_back('-');
  }

  auto insertWordIt = std::next(wordIt);
  auto insertStyleIt = std::next(styleIt);
  words.insert(insertWordIt, remainder);
  wordStyles.insert(insertStyleIt, *styleIt);

  // Update cached widths for both fragments to keep later layout consistent.
  wordWidths[wordIndex] = measureWordWidth(renderer, fontId, *wordIt, *styleIt);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, *styleIt);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, std::vector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  // Get iterators to target word and style.
  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  std::advance(wordIt, wordIndex);
  std::advance(styleIt, wordIndex);

  const std::string& word = *wordIt;
  const auto style = *styleIt;

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    const int prefixWidth = measureWordWidth(renderer, fontId, word.substr(0, offset), style, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  std::string remainder = word.substr(chosenOffset);
  wordIt->resize(chosenOffset);
  if (chosenNeedsHyphen) {
    wordIt->push_back('-');
  }

  // Insert the remainder word (with matching style) directly after the prefix.
  auto insertWordIt = std::next(wordIt);
  auto insertStyleIt = std::next(styleIt);
  words.insert(insertWordIt, remainder);
  wordStyles.insert(insertStyleIt, style);

  // Update cached widths to reflect the new prefix/remainder pairing.
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);
  const uint16_t remainderWidth = measureWordWidth(renderer, fontId, remainder, style);
  wordWidths.insert(wordWidths.begin() + wordIndex + 1, remainderWidth);
  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const std::vector<uint16_t>& wordWidths, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate total word width for this line
  int lineWordWidthSum = 0;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    lineWordWidthSum += wordWidths[i];
  }

  // Calculate spacing
  const int spareSpace = pageWidth - lineWordWidthSum;

  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  if (style == TextBlock::JUSTIFIED && !isLastLine && lineWordCount >= 2) {
    spacing = spareSpace / (lineWordCount - 1);
  }

  // Calculate initial x position
  uint16_t xpos = 0;
  if (style == TextBlock::RIGHT_ALIGN) {
    xpos = spareSpace - (lineWordCount - 1) * spaceWidth;
  } else if (style == TextBlock::CENTER_ALIGN) {
    xpos = (spareSpace - (lineWordCount - 1) * spaceWidth) / 2;
  }

  // Pre-calculate X positions for words
  std::list<uint16_t> lineXPos;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    const uint16_t currentWordWidth = wordWidths[i];
    lineXPos.push_back(xpos);
    xpos += currentWordWidth + spacing;
  }

  // Iterators always start at the beginning as we are moving content with splice below
  auto wordEndIt = words.begin();
  auto wordStyleEndIt = wordStyles.begin();
  std::advance(wordEndIt, lineWordCount);
  std::advance(wordStyleEndIt, lineWordCount);

  // *** CRITICAL STEP: CONSUME DATA USING SPLICE ***
  std::list<std::string> lineWords;
  lineWords.splice(lineWords.begin(), words, words.begin(), wordEndIt);
  std::list<EpdFontFamily::Style> lineWordStyles;
  lineWordStyles.splice(lineWordStyles.begin(), wordStyles, wordStyles.begin(), wordStyleEndIt);

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles), style));
}
