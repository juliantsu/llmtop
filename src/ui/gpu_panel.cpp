#include <string>
#include <utility>

#include "ui/ui.hpp"
#include "util.hpp"

namespace llmtop {

using namespace ftxui;

namespace {

Color temp_color(int temp_c) {
  if (temp_c >= 85)
    return Color::Red;
  if (temp_c >= 70)
    return Color::Yellow;
  return Color::Green;
}

Element device_block(const GpuDevice& d, bool show_name) {
  double vram_frac =
      d.mem_total ? static_cast<double>(d.mem_used) / static_cast<double>(d.mem_total)
                  : 0.0;
  Color vram_c = frac_color(vram_frac);
  Color util_c = frac_color(d.util_pct / 100.0, 0.75, 0.95);

  Elements rows;
  if (show_name)
    rows.push_back(text(d.name) | bold);

  rows.push_back(hbox({
      text("VRAM ") | dim,
      gauge(static_cast<float>(vram_frac)) | color(vram_c) | flex,
      text(" " + human_bytes(static_cast<double>(d.mem_used)) + " / " +
           human_bytes(static_cast<double>(d.mem_total)) + " "),
      text(strf("%3.0f%%", vram_frac * 100)) | color(vram_c) | bold,
  }));

  std::string power = strf("PWR %3.0fW", d.power_w);
  if (d.power_limit_w > 0)
    power += strf(" / %.0fW", d.power_limit_w);
  rows.push_back(hbox({
      text(strf("UTIL %3d%%", d.util_pct)) | color(util_c) | bold,
      text("   "),
      text(strf("TEMP %2d°C", d.temp_c)) | color(temp_color(d.temp_c)) | bold,
      text("   "),
      text(power) | bold,
      filler(),
      text(strf("SM %4d MHz · MEM %5d MHz", d.sm_clock_mhz, d.mem_clock_mhz)) |
          dim,
  }));

  Element util_graph = history_graph(d.util_history.values(), 100.0f, util_c);
  rows.push_back(dbox({
                     util_graph,
                     hbox({filler(),
                           text(strf(" util %d%% ", d.util_pct)) | dim}),
                 }) |
                 size(HEIGHT, EQUAL, 5));
  return vbox(std::move(rows));
}

}  // namespace

Element render_gpu_panel(const Snapshot& s) {
  Element body;
  if (s.gpu.disabled) {
    body = text("GPU telemetry disabled (--no-nvml)") | dim | center |
           size(HEIGHT, EQUAL, 3);
  } else if (!s.gpu.available || s.gpu.devices.empty()) {
    std::string status =
        s.gpu.status.empty() ? "waiting for NVML…" : s.gpu.status;
    body = vbox({
               text("no GPU") | bold | center,
               text(status) | dim | center,
           }) |
           center | size(HEIGHT, EQUAL, 3);
  } else {
    Elements blocks;
    bool multi = s.gpu.devices.size() > 1;
    for (std::size_t i = 0; i < s.gpu.devices.size(); ++i) {
      if (i > 0)
        blocks.push_back(separator());
      blocks.push_back(device_block(s.gpu.devices[i], multi));
    }
    body = vbox(std::move(blocks));
  }
  return panel_window("1 · GPU", std::move(body), s.focused == 1);
}

}  // namespace llmtop
