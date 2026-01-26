#include "LanguageRegistry.h"

#include <algorithm>
#include <array>

#include "HyphenationCommon.h"
#ifndef DISABLE_GERMAN_HYPHENATION
#include "generated/hyph-de.trie.h"
#endif
#include "generated/hyph-en.trie.h"
#include "generated/hyph-fr.trie.h"
#include "generated/hyph-hu.trie.h"
#include "generated/hyph-ru.trie.h"

namespace {

// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_us_patterns, isLatinLetter, toLowerLatin, 3, 3);
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
#ifndef DISABLE_GERMAN_HYPHENATION
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
#endif
LanguageHyphenator hungarianHyphenator(hu_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator russianHyphenator(ru_ru_patterns, isCyrillicLetter, toLowerCyrillic);

#ifdef DISABLE_GERMAN_HYPHENATION
using EntryArray = std::array<LanguageEntry, 4>;
#else
using EntryArray = std::array<LanguageEntry, 5>;
#endif

const EntryArray& entries() {
#ifdef DISABLE_GERMAN_HYPHENATION
  static const EntryArray kEntries = {{{"english", "en", &englishHyphenator},
                                       {"french", "fr", &frenchHyphenator},
                                       {"hungarian", "hu", &hungarianHyphenator},
                                       {"russian", "ru", &russianHyphenator}}};
#else
  static const EntryArray kEntries = {{{"english", "en", &englishHyphenator},
                                       {"french", "fr", &frenchHyphenator},
                                       {"german", "de", &germanHyphenator},
                                       {"hungarian", "hu", &hungarianHyphenator},
                                       {"russian", "ru", &russianHyphenator}}};
#endif
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != allEntries.end()) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
