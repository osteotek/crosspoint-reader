#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <list>
#include <sstream>
#include <string>
#include <vector>

#include <GfxRenderer.h>

#include "ParsedText.h"
#include "hyphenation/Hyphenator.h"

#include <EpdFont.h>
#include <EpdFontFamily.h>

#include <builtinFonts/bookerly_12_regular.h>

namespace {

constexpr int kFontId = 1;

std::vector<std::string> splitWords(const std::string& text) {
  std::vector<std::string> words;
  std::istringstream stream(text);
  std::string word;
  while (stream >> word) {
    words.push_back(word);
  }
  return words;
}

std::vector<std::string> buildWordList(const std::vector<std::string>& baseWords, const size_t targetCount) {
  std::vector<std::string> words;
  words.reserve(targetCount);
  // Repeat the base text to reach a stable word count for benchmarking.
  size_t idx = 0;
  while (words.size() < targetCount) {
    words.push_back(baseWords[idx]);
    idx = (idx + 1) % baseWords.size();
  }
  return words;
}

std::string joinWords(const std::list<std::string>& words) {
  std::string result;
  bool first = true;
  for (const auto& word : words) {
    if (!first) {
      result.push_back(' ');
    }
    first = false;
    result += word;
  }
  return result;
}

size_t countHyphens(const std::string& text) {
  return static_cast<size_t>(std::count(text.begin(), text.end(), '-'));
}

size_t runLayoutOnce(const std::vector<std::string>& words, const GfxRenderer& renderer, const int fontId,
                     const int viewportWidth, const bool hyphenationEnabled, const uint8_t hyphenAggressiveness,
                     const bool includeLastLine, std::string* sampleLineOut = nullptr,
                     std::vector<std::string>* sampleLinesOut = nullptr, const size_t sampleLineLimit = 0) {
  ParsedText text(TextBlock::JUSTIFIED, /*extraParagraphSpacing=*/false, hyphenationEnabled, hyphenAggressiveness);
  for (const auto& word : words) {
    text.addWord(word, EpdFontFamily::REGULAR);
  }

  size_t lineCount = 0;
  // Consume lines to avoid accumulating TextBlock allocations across iterations.
  text.layoutAndExtractLines(renderer, fontId, static_cast<uint16_t>(viewportWidth),
                             [&lineCount, &sampleLineOut, &sampleLinesOut, sampleLineLimit](
                                 std::shared_ptr<TextBlock> line) {
                               ++lineCount;
                               if (sampleLineOut && sampleLineOut->empty()) {
                                 *sampleLineOut = joinWords(line->getWords());
                               }
                               if (sampleLinesOut && sampleLinesOut->size() < sampleLineLimit) {
                                 sampleLinesOut->push_back(joinWords(line->getWords()));
                               }
                             },
                             includeLastLine);
  return lineCount;
}

}  // namespace

int main(int argc, char** argv) {
  int iterations = 50;
  size_t wordCount = 2000;

  // width in pixels
  int viewportWidth = 360;
  bool hyphenationEnabled = true;
  bool includeLastLine = true;
  size_t samplePageLineLimit = 100;
  uint8_t hyphenAggressiveness = 2;  // 0=conservative, 1=normal, 2=aggressive

  if (argc > 1) {
    iterations = std::stoi(argv[1]);
  }
  if (argc > 2) {
    wordCount = static_cast<size_t>(std::stoul(argv[2]));
  }
  if (argc > 3) {
    viewportWidth = std::stoi(argv[3]);
  }
  if (argc > 4) {
    hyphenationEnabled = std::stoi(argv[4]) != 0;
  }
  if (argc > 5) {
    includeLastLine = std::stoi(argv[5]) != 0;
  }
  if (argc > 6) {
    samplePageLineLimit = static_cast<size_t>(std::stoul(argv[6]));
  }
  if (argc > 7) {
    hyphenAggressiveness = static_cast<uint8_t>(std::stoul(argv[7]));
  }

  const std::string sampleText =
      "The quick brown fox jumps over the lazy dog and then demonstrates characteristically "
      "multinationalizations with extraordinary consideration for microarchitecture and "
      "substantialization of word-breaking behavior in preprocessed manuscripts.";

  const auto baseWords = splitWords(sampleText);
  const auto words = buildWordList(baseWords, wordCount);

  EpdFont regularFont(&bookerly_12_regular);
  EpdFontFamily fontFamily(&regularFont);
  GfxRenderer renderer;
  renderer.insertFont(kFontId, fontFamily);

  Hyphenator::setPreferredLanguage("en");

  // Warm up to avoid first-run effects in the timing window.
  const int warmupIterations = std::max(1, iterations / 10);
  size_t warmupLines = 0;
  size_t sampleLines = 0;
  std::string sampleLine;
  std::vector<std::string> samplePageLines;
  for (int i = 0; i < warmupIterations; ++i) {
    const size_t lines =
        runLayoutOnce(words, renderer, kFontId, viewportWidth, hyphenationEnabled, hyphenAggressiveness,
                      includeLastLine,
                      (i == 0) ? &sampleLine : nullptr, (i == 0) ? &samplePageLines : nullptr,
                      (i == 0) ? samplePageLineLimit : 0);
    if (i == 0) {
      sampleLines = lines;
    }
    warmupLines += lines;
  }

  const auto start = std::chrono::high_resolution_clock::now();
  size_t totalLines = 0;
  for (int i = 0; i < iterations; ++i) {
    totalLines += runLayoutOnce(words, renderer, kFontId, viewportWidth, hyphenationEnabled, hyphenAggressiveness,
                                includeLastLine);
  }
  const auto end = std::chrono::high_resolution_clock::now();

  const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  // Average time per iteration in microseconds
  const double avgUs = iterations > 0 ? static_cast<double>(elapsed) / iterations : 0.0;

  std::cout << "Layout benchmark (ParsedText)\n";
  std::cout << "  iterations: " << iterations << "\n";
  std::cout << "  words:      " << wordCount << "\n";
  std::cout << "  width:      " << viewportWidth << "\n";
  std::cout << "  hyphenate:  " << (hyphenationEnabled ? "on" : "off") << "\n";
  std::cout << "  hyph_mode:  " << static_cast<int>(hyphenAggressiveness) << "\n";
  std::cout << "  avg_us:     " << avgUs << "\n";
  std::cout << "  lines:      " << totalLines << " (warmup " << warmupLines << ")\n";
  std::cout << "  sample:     " << sampleLines << " lines (warmup)\n";
  std::cout << "  sample_text: " << sampleLine << "\n";
  std::cout << "  sample_page_lines: " << samplePageLines.size() << " (limit " << samplePageLineLimit << ")\n";
  size_t sampleHyphens = 0;
  for (const auto& line : samplePageLines) {
    sampleHyphens += countHyphens(line);
  }
  std::cout << "  sample_hyphens: " << sampleHyphens << "\n";
  for (const auto& line : samplePageLines) {
    std::cout << "  | " << line << "\n";
  }

  return 0;
}
