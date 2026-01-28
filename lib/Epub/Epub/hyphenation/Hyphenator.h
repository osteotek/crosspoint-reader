#pragma once

#include <cstddef>
#include <string>
#include <vector>

class LanguageHyphenator;

class Hyphenator {
 public:
  struct BreakInfo {
    size_t byteOffset;
    bool requiresInsertedHyphen;
  };
  // Returns byte offsets where the word may be hyphenated using strict (language + explicit) rules.
  static std::vector<BreakInfo> breakOffsetsStrict(const std::string& word);
  // Returns byte offsets for fallback breaks (min prefix/suffix), ignoring language-specific rules.
  static std::vector<BreakInfo> breakOffsetsFallback(const std::string& word);

  // Provide a publication-level language hint (e.g. "en", "en-US", "ru") used to select hyphenation rules.
  static void setPreferredLanguage(const std::string& lang);

 private:
  static const LanguageHyphenator* cachedHyphenator_;
};
