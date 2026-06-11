#include <string>
#include <utility>

#include "ui/ui.hpp"
#include "util.hpp"

namespace llmtop {

using namespace ftxui;

namespace {

std::string format_tps(double v) {
  if (v >= 1000)
    return strf("%.0f", v);
  if (v >= 100)
    return strf("%.0f", v);
  return strf("%.1f", v);
}

Element offline_body(const LlamaState& L) {
  return vbox({
             filler(),
             text("llama.cpp unreachable") | bold | color(Color::Red) | center,
             text(L.url + (L.error.empty() ? "" : " — " + L.error)) | dim |
                 center,
             filler(),
         }) |
         flex;
}

}  // namespace

Element render_throughput_panel(const Snapshot& s) {
  const LlamaState& L = s.llama;

  if (!L.online) {
    return panel_window("2 · THROUGHPUT", offline_body(L), s.focused == 2);
  }

  // Left column: headline numbers + KV cache.
  Elements left_rows;
  left_rows.push_back(text("GENERATION") | dim);
  left_rows.push_back(big_number(format_tps(L.gen_tps), Color::Cyan));
  left_rows.push_back(text("tok/s") | dim);
  left_rows.push_back(text(""));
  left_rows.push_back(hbox({
      text("PROMPT ") | dim,
      text(strf("%6.0f", L.prompt_tps)) | bold | color(Color::Magenta),
      text(" tok/s") | dim,
  }));
  if (L.kv_ratio >= 0) {
    std::string kv_label = strf(" %3.0f%%", L.kv_ratio * 100);
    if (L.kv_tokens >= 0) {
      auto tokens = static_cast<double>(L.kv_tokens);
      kv_label += tokens >= 1000 ? strf(" %.1fk", tokens / 1000.0)
                                 : strf(" %lld", static_cast<long long>(L.kv_tokens));
    }
    left_rows.push_back(hbox({
        text("KV     ") | dim,
        gauge(static_cast<float>(L.kv_ratio)) | color(frac_color(L.kv_ratio)) |
            size(WIDTH, EQUAL, 8),
        text(kv_label) | dim,
    }));
  }
  if (L.requests_processing > 0 || L.requests_deferred > 0) {
    left_rows.push_back(hbox({
        text("REQ    ") | dim,
        text(strf("%d active", L.requests_processing)) | color(Color::Green),
        L.requests_deferred > 0
            ? text(strf("  %d queued", L.requests_deferred)) | color(Color::Yellow)
            : text(""),
    }));
  }
  Element left = vbox(std::move(left_rows)) | size(WIDTH, EQUAL, 28);

  // Right column: scrolling graphs, generation on top, prompt below.
  // Floor the axis so a decaying tail doesn't get rescaled to full height
  // once the peak that set the scale ages out of the buffer.
  float gen_max = nice_max(std::max(L.gen_history.max(), 10.0f));
  float prompt_max = nice_max(std::max(L.prompt_history.max(), 100.0f));
  Element graphs =
      vbox({
          hbox({text("generation") | color(Color::Cyan), filler(),
                text(strf("0..%.0f t/s ", gen_max)) | dim}),
          history_graph(L.gen_history.values(), gen_max, Color::Cyan) |
              flex_grow,
          separator() | dim,
          hbox({text("prompt processing") | color(Color::Magenta), filler(),
                text(strf("0..%.0f t/s ", prompt_max)) | dim}),
          history_graph(L.prompt_history.values(), prompt_max, Color::Magenta) |
              flex_grow,
      }) |
      flex;

  Elements body_rows;
  body_rows.push_back(hbox({left, separator() | dim, graphs}) | flex);
  if (!L.note.empty())
    body_rows.push_back(text(L.note) | color(Color::Yellow) | dim);
  return panel_window("2 · THROUGHPUT", vbox(std::move(body_rows)) | flex,
                      s.focused == 2);
}

}  // namespace llmtop
