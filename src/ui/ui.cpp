#include "ui/ui.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

namespace llmtop {

using namespace ftxui;

namespace {

Snapshot take_snapshot(AppState& state) {
  Snapshot snap;
  {
    std::scoped_lock lock(state.mutex);
    snap.gpu = state.gpu;
    snap.llama = state.llama;
    snap.ollama = state.ollama;
  }
  snap.interval_ms = state.interval_ms.load();
  snap.demo = state.demo_mode;
  return snap;
}

}  // namespace

Element panel_window(const std::string& title, Element body, bool focused) {
  Element chip = text(" " + title + " ") | bold;
  chip = focused ? chip | color(Color::Black) | bgcolor(Color::Cyan)
                 : chip | color(Color::Cyan);
  return window(chip, std::move(body));
}

Color frac_color(double frac, double warn, double crit) {
  if (frac >= crit)
    return Color::Red;
  if (frac >= warn)
    return Color::Yellow;
  return Color::Green;
}

float nice_max(float m) {
  if (m <= 0)
    return 1.0f;
  float p = std::pow(10.0f, std::floor(std::log10(m)));
  for (float k : {1.0f, 2.0f, 5.0f, 10.0f})
    if (m <= k * p)
      return k * p;
  return 10.0f * p;
}

Element history_graph(std::vector<float> values, float vmax, Color c) {
  auto fn = [values = std::move(values), vmax](int width, int height) {
    std::vector<int> out(static_cast<std::size_t>(std::max(0, width)), 0);
    int n = static_cast<int>(values.size());
    for (int x = 0; x < width; ++x) {
      int idx = n - (width - x);  // right-align: newest sample at right edge
      if (idx < 0)
        continue;
      float f = vmax > 0 ? values[static_cast<std::size_t>(idx)] / vmax : 0.0f;
      out[static_cast<std::size_t>(x)] =
          std::clamp(static_cast<int>(std::lround(f * static_cast<float>(height))),
                     0, height);
    }
    return out;
  };
  return graph(fn) | color(c);
}

int run_ui(AppState& state, const Options& opts) {
  (void)opts;
  auto screen = ScreenInteractive::Fullscreen();
  int focused = 0;

  auto renderer = Renderer([&] {
    Snapshot snap = take_snapshot(state);
    snap.focused = focused;
    return vbox({
        render_header(snap),
        render_gpu_panel(snap),
        render_throughput_panel(snap) | flex,
        render_slots_panel(snap),
        render_footer(snap),
    });
  });

  auto component = CatchEvent(renderer, [&](Event e) {
    if (e == Event::Character('q') || e == Event::Character('Q')) {
      screen.Exit();
      return true;
    }
    for (int k = 1; k <= 3; ++k) {
      if (e == Event::Character(static_cast<char>('0' + k))) {
        focused = focused == k ? 0 : k;
        return true;
      }
    }
    if (e == Event::Character('+') || e == Event::Character('=')) {
      state.interval_ms = std::min(5000, state.interval_ms.load() + 100);
      return true;
    }
    if (e == Event::Character('-') || e == Event::Character('_')) {
      state.interval_ms = std::max(100, state.interval_ms.load() - 100);
      return true;
    }
    return false;
  });

  // ~10 FPS redraw; data freshness is governed by the poller threads.
  std::jthread refresher([&screen](std::stop_token st) {
    int auto_exit_ms = 0;
    if (const char* env = std::getenv("LLMTOP_AUTO_EXIT_MS"))
      auto_exit_ms = std::atoi(env);
    auto start = std::chrono::steady_clock::now();
    while (!st.stop_requested()) {
      screen.PostEvent(Event::Custom);
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      if (auto_exit_ms > 0 &&
          std::chrono::steady_clock::now() - start >
              std::chrono::milliseconds(auto_exit_ms)) {
        screen.Exit();
        break;
      }
    }
  });

  screen.Loop(component);
  refresher.request_stop();
  return 0;
}

}  // namespace llmtop
