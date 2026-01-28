#pragma once

#include <cstdint>
#include <list>
#include <string>

#include <EpdFontFamily.h>

class TextBlock {
 public:
  enum Style : uint8_t {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
  };

  explicit TextBlock(std::list<std::string>, std::list<uint16_t>, std::list<EpdFontFamily::Style>, Style) {}
};
