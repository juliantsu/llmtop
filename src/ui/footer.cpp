#include <string>
#include <utility>

#include "ui/ui.hpp"
#include "util.hpp"

namespace llmtop {

using namespace ftxui;

namespace {

Element key_hint(const std::string& key, const std::string& label) {
  return hbox({
      text(" " + key + " ") | bold | color(Color::Black) |
          bgcolor(Color::GrayLight),
      text(" " + label + "  ") | dim,
  });
}

Element status_dot(const std::string& name, bool online, bool not_applicable) {
  Color c = not_applicable ? Color::GrayDark : online ? Color::Green : Color::Red;
  return hbox({text("● ") | color(c), text(name + "  ") | dim});
}

}  // namespace

Element render_footer(const Snapshot& s) {
  Elements parts;
  parts.push_back(key_hint("q", "quit"));
  parts.push_back(key_hint("1·2·3", "focus"));
  parts.push_back(key_hint("+/-", strf("poll %dms", s.interval_ms)));
  parts.push_back(filler());
  if (s.demo) {
    parts.push_back(text("● demo data  ") | color(Color::Yellow));
  } else {
    parts.push_back(status_dot("gpu", s.gpu.available, s.gpu.disabled));
    parts.push_back(status_dot("llama.cpp", s.llama.online, false));
    parts.push_back(status_dot("ollama", s.ollama.online, false));
  }
  return hbox(std::move(parts));
}

}  // namespace llmtop
