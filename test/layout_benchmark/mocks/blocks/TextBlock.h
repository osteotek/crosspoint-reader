#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <EpdFontFamily.h>

class TextBlock {
 public:
  enum Style : uint8_t {
    JUSTIFIED = 0,
    LEFT_ALIGN = 1,
    CENTER_ALIGN = 2,
    RIGHT_ALIGN = 3,
  };

  explicit TextBlock(std::vector<std::string>, std::vector<uint16_t>, std::vector<EpdFontFamily::Style>, Style) {}
};
