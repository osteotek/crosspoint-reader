#include "ParsedText.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <functional>
#include <iterator>
#include <limits>
#include <utility>
#include <vector>

#include "hyphenation/Hyphenator.h"

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

// Large sentinel scores; these dominate any reasonable width score.
constexpr float SCORE_INFTY = std::numeric_limits<float>::max();
constexpr float SCORE_OVERFULL = 1e12f;   // Overfull lines are treated as extremely bad.
constexpr float SCORE_DESPERATE = 1e10f;  // Desperate breaks are worse than hyphenation.

// Penalty/heuristic multipliers matching Minikin's scoring scheme.
constexpr float LAST_LINE_PENALTY_MULTIPLIER = 4.0f;  // Penalize hyphens on the last line.
constexpr float LINE_PENALTY_MULTIPLIER = 2.0f;       // Penalize extra lines (ragged text).
constexpr float SHRINK_PENALTY_MULTIPLIER = 4.0f;     // Penalize space shrinking in justified text.
constexpr float SHRINKABILITY = 1.0f / 3.0f;          // Max fraction of space width that can shrink.

// Cap fallback breakpoints for very long words to keep DP cost predictable.
constexpr size_t MAX_FALLBACK_BREAKPOINTS = 6;

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

  // Minikin-style algorithm overview:
  // 1) Build a linear list of break candidates (word boundaries + intra-word breaks).
  //    Each candidate stores cumulative widths before/after the break to make
  //    line width computation O(1) during the DP step.
  // 2) Use dynamic programming to find the lowest-cost path through candidates.
  //    The cost mixes raggedness (distance from page width), hyphen penalties,
  //    and a per-line penalty for non-justified paragraphs.
  // 3) Apply any chosen intra-word splits to our word list, then emit line break indices.
  //
  // Oversized words are handled by "desperate" break candidates inserted during
  // candidate generation (fallback hyphenation points with large penalties).

  // Use pointer views to avoid copying the word and style lists. The underlying
  // lists stay stable during candidate generation, so pointers remain valid.
  std::vector<const std::string*> wordPtrs;
  wordPtrs.reserve(words.size());
  for (const auto& word : words) {
    wordPtrs.push_back(&word);
  }

  std::vector<const EpdFontFamily::Style*> stylePtrs;
  stylePtrs.reserve(wordStyles.size());
  for (const auto& styleEntry : wordStyles) {
    stylePtrs.push_back(&styleEntry);
  }

  const size_t totalWordCount = wordPtrs.size();
  if (totalWordCount == 0) {
    return {};
  }

  // Precompute cumulative widths assuming a single space between words. This mirrors Minikin's
  // use of preBreak/postBreak so we can compute line widths by subtraction and avoid O(n^2) sums.
  std::vector<float> baseCumulative(totalWordCount + 1, 0.0f);
  for (size_t i = 0; i < totalWordCount; ++i) {
    baseCumulative[i + 1] = baseCumulative[i] + static_cast<float>(wordWidths[i]);
    if (i + 1 < totalWordCount) {
      baseCumulative[i + 1] += static_cast<float>(spaceWidth);
    }
  }

  // Track the number of spaces before each word boundary for shrink penalties (justified text).
  // The counts map line widths to how many spaces could be shrunk if the line is overfull.
  std::vector<int> spaceCounts(totalWordCount + 1, 0);
  for (size_t i = 0; i < totalWordCount; ++i) {
    spaceCounts[i] = static_cast<int>(i);
  }
  spaceCounts[totalWordCount] = static_cast<int>(totalWordCount - 1);

  // Translate Minikin penalty model into our word-based width units.
  // This sets the relative cost of hyphenation and extra lines.
  const bool justified = style == TextBlock::JUSTIFIED;
  const float spaceWidthF = static_cast<float>(spaceWidth);
  const float pageWidthF = static_cast<float>(pageWidth);
  float hyphenPenalty = 0.5f * std::max(spaceWidthF, 1.0f) * pageWidthF;
  if (justified) {
    hyphenPenalty *= 0.25f;
  }
  const float linePenalty = justified ? 0.0f : hyphenPenalty * LINE_PENALTY_MULTIPLIER;

  // Precompute hyphenation break info per word so we can reserve candidate storage
  // and avoid repeated hyphenation work in the inner loop.
  std::vector<std::vector<Hyphenator::BreakInfo>> hyphenBreaksPerWord(totalWordCount);
  std::vector<std::vector<Hyphenator::BreakInfo>> fallbackBreaksPerWord(totalWordCount);
  size_t extraCandidateCount = 0;
  auto downsampleBreaks = [](std::vector<Hyphenator::BreakInfo>& breaks, const size_t maxCount) {
    // Cap candidate density to keep the DP cost predictable for very long words.
    if (breaks.size() <= maxCount) {
      return;
    }
    std::vector<Hyphenator::BreakInfo> reduced;
    reduced.reserve(maxCount);
    const size_t last = breaks.size() - 1;
    for (size_t slot = 0; slot < maxCount; ++slot) {
      const size_t idx = (slot * last) / (maxCount - 1);
      reduced.push_back(breaks[idx]);
    }
    breaks.swap(reduced);
  };
  for (size_t i = 0; i < totalWordCount; ++i) {
    const std::string& word = *wordPtrs[i];
    if (hyphenationEnabled) {
      hyphenBreaksPerWord[i] = Hyphenator::breakOffsets(word, /*includeFallback=*/false);
      extraCandidateCount += hyphenBreaksPerWord[i].size();
    }
    if (wordWidths[i] > pageWidth && (!hyphenationEnabled || hyphenBreaksPerWord[i].empty())) {
      // Oversized words with no language hyphenation get fallback breakpoints.
      fallbackBreaksPerWord[i] = Hyphenator::breakOffsets(word, /*includeFallback=*/true);
      downsampleBreaks(fallbackBreaksPerWord[i], MAX_FALLBACK_BREAKPOINTS);
      extraCandidateCount += fallbackBreaksPerWord[i].size();
    }
  }

  // Build the candidate list in reading order. The DP expects monotonic widths so that
  // subtracting preBreak from postBreak yields line widths.
  // Each candidate captures how far we've advanced in the paragraph at that breakpoint.
  // Reuse scratch buffers to avoid per-call allocations.
  auto& candidates = candidatesScratch;
  candidates.clear();
  candidates.reserve(totalWordCount * 2 + 1 + extraCandidateCount);

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
    const std::string& word = *wordPtrs[i];
    const auto style = *stylePtrs[i];
    const size_t wordSize = word.size();
    std::vector<OptimalBreakCandidate> intraCandidates;
    // Cache prefix widths by byte offset to avoid repeated substring measurement.
    // Each entry stores {prefix width without hyphen, prefix width with hyphen}.
    std::vector<std::pair<float, float>> prefixWidths(wordSize);
    std::vector<uint8_t> prefixWidthValid(wordSize, 0);
    std::string prefixScratch;
    prefixScratch.reserve(wordSize);
    auto getPrefixWidths = [&](const size_t offset) {
      // Prefix width calculation is expensive; memoize per offset within the word.
      // Offsets are byte offsets (matching hyphenation outputs), not codepoints.
      if (offset < prefixWidthValid.size() && prefixWidthValid[offset]) {
        return prefixWidths[offset];
      }
      // Reuse a single buffer to avoid allocations for every prefix.
      prefixScratch.assign(word.data(), offset);
      const float prefixNoHyphen = static_cast<float>(
          measureWordWidth(renderer, fontId, prefixScratch, style, /*appendHyphen=*/false));
      const float prefixWithHyphen =
          static_cast<float>(measureWordWidth(renderer, fontId, prefixScratch, style, /*appendHyphen=*/true));
      const std::pair<float, float> widths = {prefixNoHyphen, prefixWithHyphen};
      // Store both widths so later candidates can reuse without re-measuring.
      if (offset < prefixWidths.size()) {
        prefixWidths[offset] = widths;
        prefixWidthValid[offset] = 1;
      }
      return widths;
    };

    // Build intra-word candidates from breakInfos. This keeps the generation logic in one place
    // while allowing different penalties.
    auto appendIntraCandidates = [&](const std::vector<Hyphenator::BreakInfo>& breakInfos, const float penalty) {
      for (const auto& info : breakInfos) {
        if (info.byteOffset == 0 || info.byteOffset >= wordSize) {
          continue;
        }
        const auto widths = getPrefixWidths(info.byteOffset);

        // preBreak keeps the flowing width (no break); postBreak includes a visible hyphen if required.
        // These two values let DP compute line width between any pair of candidates.
        OptimalBreakCandidate candidate;
        candidate.wordIndex = i;
        candidate.byteOffset = info.byteOffset;
        candidate.isHyphenation = true;
        candidate.insertHyphen = info.requiresInsertedHyphen;
        candidate.preBreak = baseCumulative[i] + widths.first;
        candidate.postBreak =
            baseCumulative[i] + (info.requiresInsertedHyphen ? widths.second : widths.first);
        candidate.penalty = penalty;
        candidate.preSpaceCount = spaceCounts[i];
        candidate.postSpaceCount = spaceCounts[i];
        intraCandidates.push_back(candidate);
      }
    };

    // Cache the language hyphenation breakpoints once per word.
    const auto& hyphenBreaks = hyphenBreaksPerWord[i];
    if (hyphenationEnabled && !hyphenBreaks.empty()) {
      // Hyphenation candidates are inserted inside the current word and carry a hyphen penalty.
      appendIntraCandidates(hyphenBreaks, hyphenPenalty);
    }

    if (wordWidths[i] > pageWidth) {
      // Desperate candidates are sourced from hyphenation points with fallback enabled so that
      // even language-unknown words can break if they don't fit on the line. They carry a large
      // penalty so that DP prefers normal hyphenation or clean word breaks when possible.
      const auto& fallbackBreaks = fallbackBreaksPerWord[i];
      if (!fallbackBreaks.empty()) {
        appendIntraCandidates(fallbackBreaks, SCORE_DESPERATE);
      }
    }

    if (!intraCandidates.empty()) {
      // Hyphenator returns offsets in ascending order, so we can append without extra sorting.
      // This preserves monotonic width assumptions used by the DP.
      candidates.insert(candidates.end(), intraCandidates.begin(), intraCandidates.end());
    }
    pushBoundaryCandidate(i + 1);
  }

  if (candidates.size() < 2) {
    return {totalWordCount};
  }

  const size_t candidateCount = candidates.size();
  // Reuse DP storage to reduce allocations on repeated layout calls.
  auto& breaksData = breaksDataScratch;
  breaksData.clear();
  breaksData.reserve(candidateCount);
  // The first candidate always begins the first line.
  breaksData.push_back({0.0f, 0, 0});

  // "active" tracks the earliest viable start candidate for the current line.
  size_t active = 0;
  const float maxShrink = justified ? SHRINKABILITY * spaceWidthF : 0.0f;

  // "i" iterates candidates for the end of the line.
  for (size_t i = 1; i < candidateCount; ++i) {
    const bool atEnd = i == candidateCount - 1;
    float best = SCORE_INFTY;
    size_t bestPrev = 0;

    // The left edge represents the target line start for this candidate.
    // Any candidate "j" before it will define a line whose width is:
    // candidates[i].postBreak - candidates[j].preBreak.
    float leftEdge = candidates[i].postBreak - pageWidthF;
    float bestHope = 0.0f;

    // "j" iterates candidates for the beginning of the line, starting at the active window.
    for (size_t j = active; j < i; ++j) {
      const float jScore = breaksData[j].score;
      if (jScore + bestHope >= best) {
        // If this start cannot beat the best score, skip it.
        continue;
      }

      // delta is the difference between actual line width and target width.
      // Positive delta means the line is underfull; negative means overfull.
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
        // Quadratic penalty favors lines closer to the target width.
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

      if (!atEnd && delta >= 0.0f && widthScore >= best) {
        // For non-final lines, widthScore grows with delta. If widthScore already exceeds
        // the best score found so far, later candidates cannot improve it, so we can stop.
        break;
      }

      // Update the active window when we are already overfull to skip hopeless starts.
      if (delta < 0.0f) {
        active = j + 1;
      } else {
        // bestHope optimization assumes widthScore grows monotonically for non-negative delta.
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
  // The path is stored by "prev" links in the DP table.
  auto& breakCandidateIndices = breakCandidateIndicesScratch;
  breakCandidateIndices.clear();
  breakCandidateIndices.reserve(candidateCount);
  for (size_t i = candidateCount - 1; i > 0; i = breaksData[i].prev) {
    breakCandidateIndices.push_back(i);
  }
  std::reverse(breakCandidateIndices.begin(), breakCandidateIndices.end());

  // Apply hyphenation splits that were chosen by the optimal path and translate to break indices.
  // This mutates the words list, so we track index shifts and already-consumed offsets.
  auto& lineBreakIndices = lineBreakIndicesScratch;
  lineBreakIndices.clear();
  lineBreakIndices.reserve(breakCandidateIndices.size());
  size_t indexShift = 0;
  // Track how many bytes of each original word were already consumed by earlier splits.
  consumedOffsetsScratch.assign(totalWordCount, 0);
  auto& consumedOffsets = consumedOffsetsScratch;

  for (const size_t candidateIndex : breakCandidateIndices) {
    const OptimalBreakCandidate& candidate = candidates[candidateIndex];
    if (!candidate.isHyphenation) {
      // Word boundary: break right after the word (accounting for splits already inserted).
      lineBreakIndices.push_back(candidate.wordIndex + indexShift);
      continue;
    }

    // Hyphenation candidate: split the word and break after the inserted prefix.
    const size_t currentIndex = candidate.wordIndex + indexShift;
    const size_t consumed = consumedOffsets[candidate.wordIndex];
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

bool ParsedText::splitWordAtIndex(const size_t wordIndex, size_t splitByteOffset, bool insertHyphen,
                                  const GfxRenderer& renderer, const int fontId,
                                  std::vector<uint16_t>& wordWidths) {
  // This helper applies an explicit split chosen by the optimal breaker.
  if (wordIndex >= words.size()) {
    return false;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  std::advance(wordIt, wordIndex);
  std::advance(styleIt, wordIndex);

  const std::string& word = *wordIt;
  const auto style = *styleIt;

  if (splitByteOffset >= word.size()) {
    return false;
  }

  // Split the word into prefix and remainder and insert a visible hyphen if requested.
  std::string remainder = word.substr(splitByteOffset);
  wordIt->resize(splitByteOffset);
  if (insertHyphen) {
    wordIt->push_back('-');
  }

  auto insertWordIt = std::next(wordIt);
  auto insertStyleIt = std::next(styleIt);
  words.insert(insertWordIt, remainder);
  wordStyles.insert(insertStyleIt, style);

  // Update cached widths for both fragments to keep later layout consistent.
  wordWidths[wordIndex] = measureWordWidth(renderer, fontId, *wordIt, style);
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
