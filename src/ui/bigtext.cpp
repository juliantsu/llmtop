#include <array>
#include <string>
#include <utility>

#include "ui/ui.hpp"

namespace llmtop {

using namespace ftxui;

namespace {

constexpr int kRows = 5;

// 3-wide block glyphs, 5 rows tall.
const std::array<const char*, kRows>* glyph(char c) {
  static const std::array<const char*, kRows> digits[10] = {
      {"███", "█ █", "█ █", "█ █", "███"},  // 0
      {" █ ", "██ ", " █ ", " █ ", "███"},  // 1
      {"███", "  █", "███", "█  ", "███"},  // 2
      {"███", "  █", "███", "  █", "███"},  // 3
      {"█ █", "█ █", "███", "  █", "  █"},  // 4
      {"███", "█  ", "███", "  █", "███"},  // 5
      {"███", "█  ", "███", "█ █", "███"},  // 6
      {"███", "  █", "  █", "  █", "  █"},  // 7
      {"███", "█ █", "███", "█ █", "███"},  // 8
      {"███", "█ █", "███", "  █", "███"},  // 9
  };
  static const std::array<const char*, kRows> dot = {"  ", "  ", "  ", "  ",
                                                     "█ "};
  static const std::array<const char*, kRows> dash = {"   ", "   ", "███",
                                                      "   ", "   "};
  static const std::array<const char*, kRows> space = {" ", " ", " ", " ", " "};
  if (c >= '0' && c <= '9')
    return &digits[c - '0'];
  if (c == '.')
    return &dot;
  if (c == '-')
    return &dash;
  if (c == ' ')
    return &space;
  return nullptr;
}

}  // namespace

Element big_number(const std::string& s, Color c) {
  std::array<std::string, kRows> rows;
  for (char ch : s) {
    const auto* g = glyph(ch);
    if (!g)
      continue;
    for (int r = 0; r < kRows; ++r) {
      rows[static_cast<std::size_t>(r)] += (*g)[static_cast<std::size_t>(r)];
      rows[static_cast<std::size_t>(r)] += ' ';
    }
  }
  Elements lines;
  for (auto& row : rows)
    lines.push_back(text(std::move(row)) | bold | color(c));
  return vbox(std::move(lines));
}

}  // namespace llmtop
