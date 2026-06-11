#pragma once

#include <ftxui/dom/elements.hpp>
#include <string>
#include <vector>

#include "cli.hpp"
#include "state.hpp"

namespace llmtop {

// Consistent copy of the shared state, taken once per frame under the lock.
struct Snapshot {
  GpuState gpu;
  LlamaState llama;
  OllamaState ollama;
  int interval_ms = 500;
  bool demo = false;
  int focused = 0;  // 0 = none, 1..3 = panel index
};

int run_ui(AppState& state, const Options& opts);

ftxui::Element render_header(const Snapshot& s);
ftxui::Element render_gpu_panel(const Snapshot& s);
ftxui::Element render_throughput_panel(const Snapshot& s);
ftxui::Element render_slots_panel(const Snapshot& s);
ftxui::Element render_footer(const Snapshot& s);

// --- shared helpers (ui.cpp) ---

// Bordered panel with a title chip; the chip is highlighted when focused.
ftxui::Element panel_window(const std::string& title, ftxui::Element body,
                            bool focused);

// green below `warn`, yellow below `crit`, red above (fractions 0..1)
ftxui::Color frac_color(double frac, double warn = 0.70, double crit = 0.90);

// Scrolling time-series graph; newest sample at the right edge.
ftxui::Element history_graph(std::vector<float> values, float vmax,
                             ftxui::Color c);

// Smallest "nice" axis maximum (1/2/5 * 10^k) above m.
float nice_max(float m);

// 5-row block-character rendering of a number string (digits, '.', '-').
ftxui::Element big_number(const std::string& s, ftxui::Color c);

}  // namespace llmtop
