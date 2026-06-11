#include <string>
#include <utility>

#include "ui/ui.hpp"
#include "util.hpp"

namespace llmtop {

using namespace ftxui;

namespace {

// Right-align to `width` display columns (counts UTF-8 code points, so "∞"
// pads correctly — printf's %6s would pad by bytes).
std::string pad_left(const std::string& s, std::size_t width) {
  std::size_t cols = 0;
  for (char c : s)
    if ((c & 0xC0) != 0x80)
      ++cols;
  return std::string(cols < width ? width - cols : 0, ' ') + s;
}

Element offline_note(const std::string& what, const std::string& url,
                     const std::string& error) {
  return vbox({
             filler(),
             text(what + " offline") | dim | center,
             text(url + (error.empty() ? "" : " — " + error)) | dim | center,
             filler(),
         }) |
         flex;
}

Element llama_slots(const LlamaState& L) {
  if (!L.online)
    return offline_note("llama.cpp", L.url, L.error);
  if (!L.have_slots)
    return vbox({
               filler(),
               text(L.note.empty() ? "no slot data" : L.note) | dim | center,
               filler(),
           }) |
           flex;

  Elements rows;
  rows.push_back(hbox({
      text(" SLOT ") | dim,
      text("STATE   ") | dim,
      text("CONTEXT") | dim,
  }));
  for (const auto& slot : L.slots) {
    double frac = slot.n_ctx > 0 ? static_cast<double>(slot.n_past) /
                                       static_cast<double>(slot.n_ctx)
                                 : 0.0;
    Element state_el = slot.processing
                           ? text("● busy ") | color(Color::Green) | bold
                           : text("○ idle ") | dim;
    rows.push_back(hbox({
        text(strf(" #%-3d ", slot.id)),
        state_el,
        text(" "),
        gauge(static_cast<float>(frac)) | color(frac_color(frac)) |
            size(WIDTH, EQUAL, 14),
        text(strf(" %5lld/%-5lld ", static_cast<long long>(slot.n_past),
                  static_cast<long long>(slot.n_ctx))) |
            dim,
        text(strf("%3.0f%%", frac * 100)) | color(frac_color(frac)),
        filler(),
    }));
  }
  if (L.slots.empty())
    rows.push_back(text(" no slots reported") | dim);
  return vbox(std::move(rows));
}

Element ollama_models(const OllamaState& O) {
  if (!O.online)
    return offline_note("ollama", O.url, O.error);

  Elements rows;
  rows.push_back(hbox({
      text(" MODEL") | dim,
      filler(),
      text("VRAM      UNLOAD ") | dim,
  }));
  for (const auto& m : O.models) {
    std::string detail;
    if (!m.params.empty())
      detail += m.params;
    if (!m.quant.empty())
      detail += (detail.empty() ? "" : " · ") + m.quant;
    rows.push_back(hbox({
        text(" " + m.name) | bold,
        detail.empty() ? text("") : text("  " + detail) | dim,
        filler(),
        text(human_bytes(static_cast<double>(m.size_vram))) |
            color(Color::Green),
        text("  " + pad_left(format_eta(m.expires_in_s), 6) + " ") |
            color(Color::Yellow),
    }));
  }
  if (O.models.empty())
    rows.push_back(text(" no models loaded") | dim);
  return vbox(std::move(rows));
}

}  // namespace

Element render_slots_panel(const Snapshot& s) {
  Elements left_title;
  left_title.push_back(text(" llama.cpp slots ") | bold | color(Color::Cyan));
  if (!s.llama.model.empty())
    left_title.push_back(text("· " + s.llama.model + " ") | dim);
  Element left = vbox({
                     hbox(std::move(left_title)),
                     llama_slots(s.llama),
                 }) |
                 flex;
  Element right = vbox({
                      text(" ollama models ") | bold | color(Color::Magenta),
                      ollama_models(s.ollama),
                  }) |
                  flex;
  Element body = hbox({left, separator() | dim, right});
  return panel_window("3 · SLOTS / MODELS", std::move(body), s.focused == 3);
}

}  // namespace llmtop
