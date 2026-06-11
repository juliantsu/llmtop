#include <ctime>
#include <string>

#include "ui/ui.hpp"
#include "util.hpp"

namespace llmtop {

using namespace ftxui;

Element render_header(const Snapshot& s) {
  std::string gpu_name;
  if (!s.gpu.devices.empty())
    gpu_name = s.gpu.devices[0].name;
  else if (s.gpu.disabled)
    gpu_name = "GPU telemetry off";
  else
    gpu_name = "no GPU";

  std::time_t t = std::time(nullptr);
  std::tm tmv{};
  localtime_r(&t, &tmv);
  char clock[16];
  std::strftime(clock, sizeof(clock), "%H:%M:%S", &tmv);

  Elements parts;
  parts.push_back(text(" llmtop ") | bold | color(Color::Cyan));
  parts.push_back(text(LLMTOP_VERSION " ") | dim);
  parts.push_back(text("│ ") | dim);
  parts.push_back(text(gpu_name) | bold);
  if (!s.gpu.driver_version.empty()) {
    parts.push_back(text("  driver ") | dim);
    parts.push_back(text(s.gpu.driver_version));
  }
  parts.push_back(filler());
  if (s.demo)
    parts.push_back(text(" DEMO ") | bold | color(Color::Black) |
                    bgcolor(Color::Yellow));
  parts.push_back(text("  "));
  parts.push_back(text(clock) | bold);
  parts.push_back(text(" "));
  return hbox(std::move(parts));
}

}  // namespace llmtop
