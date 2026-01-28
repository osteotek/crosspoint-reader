#include "ParsedText.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

#include "hyphenation/Hyphenator.h"

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;

// Large sentinel scores; these dominate any reasonable width score.
constexpr int64_t SCORE_INFTY = std::numeric_limits<int64_t>::max() / 4;
constexpr int64_t SCORE_OVERFULL = 1000000000000LL;   // Overfull lines are treated as extremely bad.
constexpr int64_t SCORE_DESPERATE = 10000000000LL;    // Desperate breaks are worse than hyphenation.

// Penalty/heuristic multipliers matching Minikin's scoring scheme.
constexpr int LAST_LINE_PENALTY_MULTIPLIER = 4;   // Penalize hyphens on the last line.
constexpr int LINE_PENALTY_MULTIPLIER = 2;        // Penalize extra lines (ragged text).
constexpr int SHRINK_PENALTY_MULTIPLIER = 4;      // Penalize space shrinking in justified text.
constexpr int SHRINKABILITY_DENOM = 3;            // Max fraction of space width that can shrink (1/3).
constexpr int JUSTIFIED_HYPHEN_PENALTY = 2;       // Penalty for hyphens in justified text.

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
  // Centralized word measurement: handles soft-hyphens and optional visible hyphen.
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextWidth(fontId, word.c_str(), style);
  }
  if (hasSoftHyphen) {
    const int baseWidth = renderer.getTextWidthWithAppendSkippingSoftHyphen(fontId, word.data(), word.size(), 0, style);
    return appendHyphen ? static_cast<uint16_t>(baseWidth + renderer.getHyphenWidth(fontId))
                        : static_cast<uint16_t>(baseWidth);
  }
  const int baseWidth = renderer.getTextWidth(fontId, word.c_str(), style);
  return appendHyphen ? static_cast<uint16_t>(baseWidth + renderer.getHyphenWidth(fontId))
                      : static_cast<uint16_t>(baseWidth);
}

// Returns the rendered width for a slice of a word [start, start+len), handling soft hyphens and optional append.
uint16_t measureWordSliceWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                               const EpdFontFamily::Style style, const size_t start, const size_t len,
                               const bool appendHyphen = false) {
  if (len == 0) {
    return 0;
  }
  const char* ptr = word.data() + start;
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (hasSoftHyphen) {
    const int baseWidth = renderer.getTextWidthWithAppendSkippingSoftHyphen(fontId, ptr, len, 0, style);
    return appendHyphen ? static_cast<uint16_t>(baseWidth + renderer.getHyphenWidth(fontId))
                        : static_cast<uint16_t>(baseWidth);
  }
  const int baseWidth = renderer.getTextWidth(fontId, ptr, len, style);
  return appendHyphen ? static_cast<uint16_t>(baseWidth + renderer.getHyphenWidth(fontId))
                      : static_cast<uint16_t>(baseWidth);
}

// Simplified Minikin-style break candidate over a word-based layout model.
// Each candidate represents either a word boundary or a hyphenation point.
}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle) {
  if (word.empty()) return;

  // Store words/styles in parallel for fast indexed access.
  words.push_back(std::move(word));
  wordStyles.push_back(fontStyle);
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (isEmpty()) {
    return;
  }

  // Apply fixed transforms before any per-line layout work.
  // This currently only inserts a paragraph indent for the first line and
  // must be done before width measurement or line breaking.
  applyParagraphIndent();

  // Compute widths for the active slice, then run the line breaker.
  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);
  auto wordWidths = calculateWordWidths(renderer, fontId, startIndex);
  std::vector<size_t> lineBreakIndices =
      computeLineBreaks(renderer, fontId, pageWidth, spaceWidth, wordWidths, startIndex);
  if (lineBreakIndices.empty()) {
    // Fallback: treat the remaining words as a single line if DP produced no breaks.
    lineBreakIndices.push_back(wordWidths.size());
    lineBreakOffsetsScratch.assign(lineBreakIndices.size(), 0);
    lineBreakInsertHyphenScratch.assign(lineBreakIndices.size(), 0);
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : (lineBreakIndices.size() - 1);

  // Emit each line and hand it to the caller.
  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, renderer, fontId, wordWidths, lineBreakIndices, lineBreakOffsetsScratch,
                lineBreakInsertHyphenScratch, startIndex, startOffset, processLine);
  }

  // Advance the logical window to avoid erasing the prefix every pass.
  // lineBreakIndices are relative to the current slice (startIndex).
  if (lineCount > 0) {
    const size_t lastBreakIndex = lineBreakIndices[lineCount - 1];
    const size_t lastBreakOffset = lineBreakOffsetsScratch[lineCount - 1];
    startIndex += lastBreakIndex;
    startOffset = lastBreakOffset;
  }

  // Periodic compaction to bound memory growth when the window advances far.
  // This preserves relative ordering while reclaiming the prefix memory.
  if (startIndex >= words.size()) {
    startIndex = words.size();
    startOffset = 0;
  } else {
    constexpr size_t kCompactionThreshold = 256;
    if (startIndex >= kCompactionThreshold && startIndex >= words.size() / 2) {
      words.erase(words.begin(), words.begin() + static_cast<std::ptrdiff_t>(startIndex));
      wordStyles.erase(wordStyles.begin(), wordStyles.begin() + static_cast<std::ptrdiff_t>(startIndex));
      startIndex = 0;
    }
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId,
                                                      const size_t baseIndex) {
  const size_t totalWordCount = words.size() > baseIndex ? words.size() - baseIndex : 0;

  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(totalWordCount);

  // Measure each word in the active slice.
  // The measured width accounts for soft hyphens (they are skipped unless a visible
  // hyphen is explicitly appended during a split).
  for (size_t i = 0; i < totalWordCount; ++i) {
    const size_t idx = baseIndex + i;
    const bool hasStartOffset = (baseIndex == startIndex && i == 0 && startOffset > 0);
    if (hasStartOffset) {
      const size_t offset = startOffset;
      const size_t len = words[idx].size() > offset ? (words[idx].size() - offset) : 0;
      wordWidths.push_back(measureWordSliceWidth(renderer, fontId, words[idx], wordStyles[idx], offset, len));
    } else {
      wordWidths.push_back(measureWordWidth(renderer, fontId, words[idx], wordStyles[idx]));
    }
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                                  const int spaceWidth, std::vector<uint16_t>& wordWidths,
                                                  const size_t baseIndex) {
  if (wordWidths.empty()) {
    return {};
  }

  // Minikin-style algorithm overview:
  // 1) Build a linear list of break candidates (word boundaries + intra-word breaks).
  //    Each candidate stores cumulative widths before/after the break to make
  //    line width computation O(1) during the DP step.
  // 2) Use dynamic programming to find the lowest-cost path through candidates.
  //    The cost mixes raggedness (distance from page width), hyphen penalties,
  //    and a per-line penalty for non-justified paragraphs.
  // 3) Emit chosen break positions (word index + byte offset) without mutating the word list.
  //
  // Oversized words are handled by "desperate" break candidates inserted during
  // candidate generation (fallback hyphenation points with large penalties).
  //
  // Key data model notes:
  // - The layout unit is a "word", not a glyph run. We treat a paragraph as a sequence
  //   of words separated by single spaces, then insert intra-word candidates for hyphenation.
  // - Each candidate stores two cumulative widths:
  //     preBreak: how far the text has advanced if we *do not* break here.
  //     postBreak: how far the text advances if we *do* break here (may include hyphen).
  //   This mirrors Minikin's pre/post break widths and lets us compute a line width by
  //   simple subtraction: candidates[i].postBreak - candidates[j].preBreak.
  // - The DP state stores the best score to reach each candidate, plus a back-pointer.

  // Use pointer views to avoid copying the word and style lists. The underlying
  // storage stays stable during candidate generation, so pointers remain valid.
  std::vector<const std::string*> wordPtrs;
  wordPtrs.reserve(wordWidths.size());
  std::vector<const EpdFontFamily::Style*> stylePtrs;
  stylePtrs.reserve(wordWidths.size());
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    const size_t idx = baseIndex + i;
    wordPtrs.push_back(&words[idx]);
    stylePtrs.push_back(&wordStyles[idx]);
  }

  const size_t totalWordCount = wordPtrs.size();
  if (totalWordCount == 0) {
    return {};
  }

  // Precompute cumulative widths assuming a single space between words. This mirrors Minikin's
  // use of preBreak/postBreak so we can compute line widths by subtraction and avoid O(n^2) sums.
  // baseCumulative[i] is the width up to word i (exclusive) including inter-word spaces.
  std::vector<int> baseCumulative(totalWordCount + 1, 0);
  for (size_t i = 0; i < totalWordCount; ++i) {
    baseCumulative[i + 1] = baseCumulative[i] + static_cast<int>(wordWidths[i]);
    if (i + 1 < totalWordCount) {
      baseCumulative[i + 1] += spaceWidth;
    }
  }

  auto spaceCountForIndex = [&](const size_t index) {
    if (index < totalWordCount) {
      return static_cast<int>(index);
    }
    return totalWordCount > 0 ? static_cast<int>(totalWordCount - 1) : 0;
  };

  // Translate Minikin penalty model into our word-based width units.
  // This sets the relative cost of hyphenation and extra lines.
  const bool justified = style == TextBlock::JUSTIFIED;
  // Track the widest word to bound the active window in the DP.
  int maxWordWidth = 0;
  for (const auto width : wordWidths) {
    maxWordWidth = std::max(maxWordWidth, static_cast<int>(width));
  }
  // Hyphen penalty scales with line width and space width to stay proportional to layout size.
  int64_t hyphenPenalty = static_cast<int64_t>(std::max(spaceWidth, 1) * pageWidth) / 2;
  if (justified) {
    // hyphenPenalty *= 0.25f;
    hyphenPenalty *= JUSTIFIED_HYPHEN_PENALTY;
  }
  switch (hyphenationAggressiveness) {
    case 0:  // Conservative: fewer hyphens
      hyphenPenalty *= 2;
      break;
    case 2:  // Aggressive: more hyphens
      hyphenPenalty = std::max<int64_t>(1, hyphenPenalty / 2);
      break;
    case 1:
    default:
      break;
  }
  const int64_t linePenalty = justified ? 0 : hyphenPenalty * LINE_PENALTY_MULTIPLIER;
  const bool needsFallback = maxWordWidth > pageWidth;
  const bool allowIntraBreaks = hyphenationEnabled || needsFallback;

  // Build the candidate list in reading order. The DP expects monotonic widths so that
  // subtracting preBreak from postBreak yields line widths.
  // Each candidate captures how far we've advanced in the paragraph at that breakpoint.
  // Reuse scratch buffers to avoid per-call allocations.
  auto& candidates = candidatesScratch;
  candidates.clear();

  auto pushBoundaryCandidate = [&](const size_t wordIndex) {
    // Word boundary candidates represent legal break points between words.
    // For wordIndex==0 we insert a synthetic start boundary.
    OptimalBreakCandidate candidate;
    candidate.wordIndex = wordIndex;
    candidate.byteOffset = 0;
    candidate.isHyphenation = false;
    candidate.insertHyphen = false;
    candidate.preBreak = baseCumulative[wordIndex];
    candidate.postBreak = candidate.preBreak;
    candidate.penalty = 0;
    candidate.preSpaceCount = spaceCountForIndex(wordIndex);
    candidate.postSpaceCount = candidate.preSpaceCount;
    if (wordIndex > 0 && wordIndex < totalWordCount) {
      // Breaking between words doesn't consume the trailing space on the line.
      candidate.postBreak -= spaceWidth;
      candidate.postSpaceCount -= 1;
    }
    candidates.push_back(candidate);
  };

  if (!allowIntraBreaks) {
    // Fast path: no hyphenation and no oversized words, so only word-boundary breaks exist.
    candidates.reserve(totalWordCount + 1);
    pushBoundaryCandidate(0);
    for (size_t i = 0; i < totalWordCount; ++i) {
      pushBoundaryCandidate(i + 1);
    }
  } else {
    // Precompute hyphenation break info in pooled buffers to avoid per-word allocations.
    struct BreakRange {
      size_t start = 0;
      size_t count = 0;
    };
    std::vector<Hyphenator::BreakInfo> hyphenBreaksPool;
    std::vector<Hyphenator::BreakInfo> fallbackBreaksPool;
    std::vector<BreakRange> hyphenRanges(totalWordCount);
    std::vector<BreakRange> fallbackRanges(totalWordCount);
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
    auto appendToPool = [&](const std::vector<Hyphenator::BreakInfo>& breaks,
                            std::vector<Hyphenator::BreakInfo>& pool, std::vector<BreakRange>& ranges,
                            const size_t wordIndex) {
      if (breaks.empty()) {
        ranges[wordIndex] = {};
        return;
      }
      BreakRange range;
      range.start = pool.size();
      range.count = breaks.size();
      pool.insert(pool.end(), breaks.begin(), breaks.end());
      ranges[wordIndex] = range;
      extraCandidateCount += range.count;
    };
    for (size_t i = 0; i < totalWordCount; ++i) {
      const std::string& word = *wordPtrs[i];
      std::vector<Hyphenator::BreakInfo> hyphenBreaks;
      if (hyphenationEnabled) {
        // Language-aware breakpoints (if enabled).
        hyphenBreaks = Hyphenator::breakOffsets(word, /*includeFallback=*/false);
        appendToPool(hyphenBreaks, hyphenBreaksPool, hyphenRanges, i);
      } else {
        hyphenRanges[i] = {};
      }

      if (needsFallback && wordWidths[i] > pageWidth &&
          (!hyphenationEnabled || (hyphenationEnabled && hyphenBreaks.empty()))) {
        // Oversized words with no language hyphenation get fallback breakpoints.
        auto fallbackBreaks = Hyphenator::breakOffsets(word, /*includeFallback=*/true);
        downsampleBreaks(fallbackBreaks, MAX_FALLBACK_BREAKPOINTS);
        appendToPool(fallbackBreaks, fallbackBreaksPool, fallbackRanges, i);
      } else {
        fallbackRanges[i] = {};
      }
    }

    candidates.reserve(totalWordCount * 2 + 1 + extraCandidateCount);
    // Candidate 0 is a synthetic "paragraph start" boundary.
    pushBoundaryCandidate(0);

    // Reuse intra-word scratch buffers to avoid per-word allocations.
    std::vector<OptimalBreakCandidate> intraCandidates;
    std::vector<int> prefixNoHyphenWidths;
    std::vector<int> prefixWithHyphenWidths;
    std::vector<char> prefixNoHyphenValid;
    std::vector<char> prefixWithHyphenValid;
    const int hyphenWidth = renderer.getHyphenWidth(fontId);
    for (size_t i = 0; i < totalWordCount; ++i) {
      const std::string& word = *wordPtrs[i];
      const BreakRange hyphenRange = hyphenationEnabled ? hyphenRanges[i] : BreakRange{};
      const BreakRange fallbackRange = needsFallback ? fallbackRanges[i] : BreakRange{};
      const bool hasHyphenCandidates = hyphenRange.count > 0;
      const bool hasFallbackCandidates = fallbackRange.count > 0;
      const size_t wordStartOffset = (baseIndex == startIndex && i == 0) ? startOffset : 0;

      if (hasHyphenCandidates || hasFallbackCandidates) {
        const auto style = *stylePtrs[i];
        const size_t wordSize = word.size();
        const bool wordHasSoftHyphen = containsSoftHyphen(word);
        // Cache prefix widths by byte offset to avoid repeated substring measurement.
        // This is an inner-loop optimization: each hyphen break references a byte offset.
        intraCandidates.clear();
        intraCandidates.reserve(hyphenRange.count + fallbackRange.count);
        prefixNoHyphenWidths.resize(wordSize);
        prefixWithHyphenWidths.resize(wordSize);
        prefixNoHyphenValid.assign(wordSize, 0);
        prefixWithHyphenValid.assign(wordSize, 0);
        auto getPrefixNoHyphen = [&](const size_t offset) {
          // Prefix width calculation is expensive; memoize per offset within the word.
          // Offsets are byte offsets (matching hyphenation outputs), not codepoints.
          if (offset < prefixNoHyphenValid.size() && prefixNoHyphenValid[offset]) {
            return prefixNoHyphenWidths[offset];
          }
          int prefixNoHyphen = 0;
          if (offset > wordStartOffset) {
            const size_t len = offset - wordStartOffset;
            const char* ptr = word.data() + wordStartOffset;
            if (!wordHasSoftHyphen) {
              prefixNoHyphen = renderer.getTextWidth(fontId, ptr, len, style);
            } else {
              prefixNoHyphen = renderer.getTextWidthWithAppendSkippingSoftHyphen(fontId, ptr, len, 0, style);
            }
          }
          if (offset < prefixNoHyphenWidths.size()) {
            prefixNoHyphenWidths[offset] = prefixNoHyphen;
            prefixNoHyphenValid[offset] = 1;
          }
          return prefixNoHyphen;
        };

        auto getPrefixWithHyphen = [&](const size_t offset) {
          if (offset < prefixWithHyphenValid.size() && prefixWithHyphenValid[offset]) {
            return prefixWithHyphenWidths[offset];
          }
          const int prefixWithHyphen = getPrefixNoHyphen(offset) + hyphenWidth;
          if (offset < prefixWithHyphenWidths.size()) {
            prefixWithHyphenWidths[offset] = prefixWithHyphen;
            prefixWithHyphenValid[offset] = 1;
          }
          return prefixWithHyphen;
        };

        // Build intra-word candidates from breakInfos. This keeps the generation logic in one place
        // while allowing different penalties.
        auto appendIntraCandidates = [&](const std::vector<Hyphenator::BreakInfo>& pool, const BreakRange range,
                                         const int64_t penalty) {
          for (size_t idx = 0; idx < range.count; ++idx) {
            const auto& info = pool[range.start + idx];
            if (info.byteOffset <= wordStartOffset || info.byteOffset >= wordSize) {
              continue;
            }
            const int prefixNoHyphen = getPrefixNoHyphen(info.byteOffset);
            const int prefixWithHyphen =
                info.requiresInsertedHyphen ? getPrefixWithHyphen(info.byteOffset) : prefixNoHyphen;

            // preBreak keeps the flowing width (no break); postBreak includes a visible hyphen if required.
            // These two values let DP compute line width between any pair of candidates.
            OptimalBreakCandidate candidate;
            candidate.wordIndex = i;
            candidate.byteOffset = info.byteOffset;
            candidate.isHyphenation = true;
            candidate.insertHyphen = info.requiresInsertedHyphen;
            candidate.preBreak = baseCumulative[i] + prefixNoHyphen;
            candidate.postBreak = baseCumulative[i] + prefixWithHyphen;
            candidate.penalty = penalty;
            candidate.preSpaceCount = static_cast<int>(i);
            candidate.postSpaceCount = static_cast<int>(i);
            intraCandidates.push_back(candidate);
          }
        };

        if (hasHyphenCandidates) {
          // Hyphenation candidates are inserted inside the current word and carry a hyphen penalty.
          appendIntraCandidates(hyphenBreaksPool, hyphenRange, hyphenPenalty);
        }

        if (hasFallbackCandidates) {
          // Desperate candidates are sourced from hyphenation points with fallback enabled so that
          // even language-unknown words can break if they don't fit on the line. They carry a large
          // penalty so that DP prefers normal hyphenation or clean word breaks when possible.
          appendIntraCandidates(fallbackBreaksPool, fallbackRange, SCORE_DESPERATE);
        }

        // Hyphenator returns offsets in ascending order, so we can append without extra sorting.
        // This preserves monotonic width assumptions used by the DP.
        candidates.insert(candidates.end(), intraCandidates.begin(), intraCandidates.end());
      }
      pushBoundaryCandidate(i + 1);
    }
  }

  const size_t candidateCount = candidates.size();
  // Reuse DP storage to reduce allocations on repeated layout calls.
  auto& breaksData = breaksDataScratch;
  breaksData.clear();
  breaksData.reserve(candidateCount);
  // The first candidate always begins the first line.
  breaksData.push_back({0, 0, 0});

  // "active" tracks the earliest viable start candidate for the current line.
  // We advance it when earlier candidates would force a line to be too wide.
  size_t active = 0;
  const int maxShrink = justified ? (spaceWidth / SHRINKABILITY_DENOM) : 0;

  // "i" iterates candidates for the end of the line.
  for (size_t i = 1; i < candidateCount; ++i) {
    const bool atEnd = i == candidateCount - 1;
    int64_t best = SCORE_INFTY;
    size_t bestPrev = 0;

    // Skip start candidates that would force a line to be wildly overfull.
    while (active < i) {
      const int lineWidth = candidates[i].postBreak - candidates[active].preBreak;
      const int spaces = candidates[i].postSpaceCount - candidates[active].preSpaceCount;
      const int shrinkLimit = justified ? (maxShrink * spaces) : 0;
      const int maxLineWidth = pageWidth + std::max(maxWordWidth, shrinkLimit);
      if (lineWidth <= maxLineWidth) {
        break;
      }
      ++active;
    }

    // Any candidate "j" before it will define a line whose width is:
    // candidates[i].postBreak - candidates[j].preBreak.
    // Lower bound for widthScore of the remaining candidates (monotonicity heuristic).
    int64_t bestLowerBound = 0;

    // "j" iterates candidates for the beginning of the line, starting at the active window.
    // This is the O(n^2) core of the algorithm, pruned by "active" and "bestLowerBound".
    for (size_t j = active; j < i; ++j) {
      const int64_t jScore = breaksData[j].score;
      if (jScore + bestLowerBound >= best) {
        // If this start cannot beat the best score, skip it.
        continue;
      }

      // delta is the difference between target width and actual line width.
      // Positive delta means the line is underfull; negative means overfull.
      const int lineWidth = candidates[i].postBreak - candidates[j].preBreak;
      const int delta = pageWidth - lineWidth;
      int64_t widthScore = 0;
      int64_t additionalPenalty = 0;

      // Mirror Minikin's width scoring: overfull lines are heavily penalized.
      if ((atEnd || !justified) && delta < 0) {
        widthScore = SCORE_OVERFULL;
      } else if (atEnd) {
        // Encourage fewer hyphens on the final line.
        additionalPenalty = static_cast<int64_t>(LAST_LINE_PENALTY_MULTIPLIER) * candidates[j].penalty;
      } else {
        // Quadratic penalty favors lines closer to the target width.
        widthScore = static_cast<int64_t>(delta) * static_cast<int64_t>(delta);
        if (delta < 0) {
          const int64_t shrinkLimit =
              static_cast<int64_t>(maxShrink) *
              static_cast<int64_t>(candidates[i].postSpaceCount - candidates[j].preSpaceCount);
          if (-delta < shrinkLimit) {
            // Small overflow can be handled by shrinking spaces (justified mode).
            widthScore *= SHRINK_PENALTY_MULTIPLIER;
          } else {
            widthScore = SCORE_OVERFULL;
          }
        }
      }

      if (!atEnd && delta >= 0 && widthScore >= best) {
        // For non-final lines, widthScore grows with delta. If widthScore already exceeds
        // the best score found so far, later candidates cannot improve it, so we can stop.
        break;
      }

      // Update the active window when we are already overfull to skip hopeless starts.
      if (delta < 0) {
        active = j + 1;
      } else {
        // bestLowerBound assumes widthScore grows monotonically for non-negative delta.
        bestLowerBound = widthScore;
      }

      const int64_t score = jScore + widthScore + additionalPenalty;
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

  // Translate chosen candidates into line break positions.
  auto& lineBreakIndices = lineBreakIndicesScratch;
  lineBreakIndices.clear();
  lineBreakIndices.reserve(breakCandidateIndices.size());
  auto& lineBreakOffsets = lineBreakOffsetsScratch;
  lineBreakOffsets.clear();
  lineBreakOffsets.reserve(breakCandidateIndices.size());
  auto& lineBreakInsertHyphen = lineBreakInsertHyphenScratch;
  lineBreakInsertHyphen.clear();
  lineBreakInsertHyphen.reserve(breakCandidateIndices.size());

  for (const size_t candidateIndex : breakCandidateIndices) {
    const OptimalBreakCandidate& candidate = candidates[candidateIndex];
    lineBreakIndices.push_back(candidate.wordIndex);
    if (candidate.isHyphenation) {
      lineBreakOffsets.push_back(candidate.byteOffset);
      lineBreakInsertHyphen.push_back(candidate.insertHyphen ? 1 : 0);
    } else {
      lineBreakOffsets.push_back(0);
      lineBreakInsertHyphen.push_back(0);
    }
  }

  return lineBreakIndices;
}

void ParsedText::applyParagraphIndent() {
  if (extraParagraphSpacing || indentApplied || startIndex != 0 || startOffset != 0 || words.empty()) {
    return;
  }

  if (style == TextBlock::JUSTIFIED || style == TextBlock::LEFT_ALIGN) {
    words.front().insert(0, "\xe2\x80\x83");
    indentApplied = true;
  }
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const GfxRenderer& renderer, const int fontId, const std::vector<uint16_t>& wordWidths,
                             const std::vector<size_t>& lineBreakIndices, const std::vector<size_t>& lineBreakOffsets,
                             const std::vector<char>& lineBreakInsertHyphen, const size_t baseIndex,
                             const size_t baseOffset,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine) {
  const size_t wordCount = wordWidths.size();
  const size_t lineEndIndex = lineBreakIndices[breakIndex];
  const size_t lineEndOffset = lineBreakOffsets[breakIndex];
  const bool insertHyphen = lineBreakInsertHyphen[breakIndex] != 0;

  const size_t lineStartIndex = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineStartOffset = breakIndex > 0 ? lineBreakOffsets[breakIndex - 1] : baseOffset;

  size_t segmentStart = lineStartIndex;
  size_t segmentEndExclusive = lineEndIndex;
  const bool endIncludesPartialWord = (lineEndIndex < wordCount && lineEndOffset > 0);
  if (endIncludesPartialWord) {
    segmentEndExclusive = lineEndIndex + 1;
  }

  std::vector<std::string> lineWords;
  std::vector<EpdFontFamily::Style> lineWordStyles;
  std::vector<uint16_t> lineWordWidths;
  if (segmentEndExclusive > segmentStart) {
    const size_t reserveCount = segmentEndExclusive - segmentStart;
    lineWords.reserve(reserveCount);
    lineWordStyles.reserve(reserveCount);
    lineWordWidths.reserve(reserveCount);
  }

  int lineWordWidthSum = 0;
  for (size_t i = segmentStart; i < segmentEndExclusive; ++i) {
    const size_t idx = baseIndex + i;
    const std::string& word = words[idx];
    const auto style = wordStyles[idx];
    size_t segStart = 0;
    size_t segEnd = word.size();
    if (i == lineStartIndex) {
      segStart = lineStartOffset;
    }
    if (i == lineEndIndex && lineEndOffset > 0) {
      segEnd = std::min(lineEndOffset, word.size());
    }
    if (segStart >= segEnd) {
      continue;
    }

    const bool appendHyphen = insertHyphen && (i == lineEndIndex) && (lineEndOffset > 0);
    std::string segment;
    if (segStart == 0 && segEnd == word.size()) {
      segment = word;
    } else {
      segment.assign(word.data() + segStart, segEnd - segStart);
    }
    if (appendHyphen) {
      segment.push_back('-');
    }

    uint16_t segmentWidth = 0;
    if (segStart == 0 && segEnd == word.size() && !appendHyphen) {
      segmentWidth = wordWidths[i];
    } else {
      segmentWidth = measureWordSliceWidth(renderer, fontId, word, style, segStart, segEnd - segStart, appendHyphen);
    }

    lineWords.push_back(std::move(segment));
    lineWordStyles.push_back(style);
    lineWordWidths.push_back(segmentWidth);
    lineWordWidthSum += segmentWidth;
  }

  const size_t lineWordCount = lineWords.size();
  if (lineWordCount == 0) {
    return;
  }

  // Calculate spacing.
  const int spareSpace = pageWidth - lineWordWidthSum;
  const int gapCount = lineWordCount > 1 ? static_cast<int>(lineWordCount - 1) : 0;
  const int baseSpaceWidth = gapCount * spaceWidth;

  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  if (style == TextBlock::JUSTIFIED && !isLastLine && gapCount > 0 && spareSpace >= 0) {
    // Only stretch spaces when there is spare space to distribute.
    spacing = spareSpace / gapCount;
  }

  // Calculate initial x position.
  uint16_t xpos = 0;
  if (style == TextBlock::RIGHT_ALIGN) {
    xpos = spareSpace - baseSpaceWidth;
  } else if (style == TextBlock::CENTER_ALIGN) {
    xpos = (spareSpace - baseSpaceWidth) / 2;
  }

  // Pre-calculate X positions for words.
  std::vector<uint16_t> lineXPos;
  lineXPos.reserve(lineWordCount);
  for (const uint16_t currentWordWidth : lineWordWidths) {
    lineXPos.push_back(xpos);
    xpos += currentWordWidth + spacing;
  }

  for (auto& word : lineWords) {
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
  }

  processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles), style));
}
