#pragma once

#include <EpdFontFamily.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(CROSSPOINT_LAYOUT_BENCH)
#include <cstdint>
#include <vector>

class TextBlock {
 public:
  enum Style : uint8_t {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
  };

  explicit TextBlock(std::vector<std::string> wordsIn, std::vector<uint16_t>, std::vector<EpdFontFamily::Style>, Style)
      : words(std::move(wordsIn)) {}

  const std::vector<std::string>& getWords() const { return words; }

 private:
  std::vector<std::string> words;
};
#else
#include "blocks/TextBlock.h"
#endif

class GfxRenderer;

class ParsedText {
  // Word storage for the current paragraph. Words/styles are kept in parallel arrays.
  std::vector<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  // Logical start of the active window in the word arrays (avoids erasing on every layout pass).
  size_t startIndex = 0;
  // Byte offset into words[startIndex] when the line break splits a word.
  size_t startOffset = 0;
  // Tracks whether paragraph indent has already been applied for this paragraph.
  bool indentApplied = false;
  TextBlock::Style style;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;
  uint8_t hyphenationAggressiveness;

  // Simplified Minikin-style break candidate over a word-based layout model.
  // Each candidate represents either a word boundary or a hyphenation point.
  struct OptimalBreakCandidate {
    size_t wordIndex;     // Index in the current words list where the candidate belongs.
    size_t byteOffset;    // Byte offset inside the word if this is a hyphenation candidate.
    bool isHyphenation;   // True when this candidate splits within a word (hyphenation or desperate).
    bool insertHyphen;    // True when we should append a visible '-' to the prefix.
    int preBreak;         // Width of text before this candidate (without breaking here).
    int postBreak;        // Width of text if we break here (may include hyphen width).
    int64_t penalty;      // Penalty for taking this break (hyphenation penalty).
    int preSpaceCount;    // Spaces before this break, used for shrink calculations.
    int postSpaceCount;   // Spaces after this break, used for shrink calculations.
  };

  // DP state for optimal line breaks.
  struct OptimalBreakData {
    int64_t score;     // Best score up to this candidate.
    size_t prev;       // Index of previous candidate in the optimal path.
    size_t lineNumber; // Computed line number at this candidate.
  };

  // Scratch buffers reused across layout passes to reduce allocations.
  std::vector<OptimalBreakCandidate> candidatesScratch;
  std::vector<OptimalBreakData> breaksDataScratch;
  std::vector<size_t> breakCandidateIndicesScratch;
  std::vector<size_t> lineBreakIndicesScratch;
  std::vector<size_t> lineBreakOffsetsScratch;
  std::vector<char> lineBreakInsertHyphenScratch;

  void applyParagraphIndent();
  // Returns line break positions for the current word slice (no word mutation).
  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth, int spaceWidth,
                                        std::vector<uint16_t>& wordWidths, size_t baseIndex);
  // Builds a single line TextBlock from the computed line breaks.
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const GfxRenderer& renderer, int fontId,
                   const std::vector<uint16_t>& wordWidths, const std::vector<size_t>& lineBreakIndices,
                   const std::vector<size_t>& lineBreakOffsets, const std::vector<char>& lineBreakInsertHyphen,
                   size_t baseIndex, size_t baseOffset,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine);
  // Measures widths of the active word slice (used by the line breaker).
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId, size_t baseIndex);

 public:
  explicit ParsedText(const TextBlock::Style style, const bool extraParagraphSpacing,
                      const bool hyphenationEnabled = false, const uint8_t hyphenationAggressiveness = 1)
      : style(style),
        extraParagraphSpacing(extraParagraphSpacing),
        hyphenationEnabled(hyphenationEnabled),
        hyphenationAggressiveness(hyphenationAggressiveness) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle);
  void setStyle(const TextBlock::Style style) { this->style = style; }
  TextBlock::Style getStyle() const { return style; }
  size_t size() const { return words.size() > startIndex ? words.size() - startIndex : 0; }
  bool isEmpty() const { return size() == 0; }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
