#include "sources/ollama_source.hpp"

#include <cpr/cpr.h>
#include <time.h>

#include <cmath>
#include <cstdio>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <utility>
#include <vector>

#include "sources/http.hpp"

namespace llmtop {

namespace {

constexpr long kTimeoutMs = 500;

// Seconds from now until an RFC 3339 timestamp like
// "2026-06-10T11:29:12.541-07:00" or "...Z". nullopt if unparseable.
std::optional<double> seconds_until(const std::string& ts) {
  int y = 0, mo = 0, d = 0, h = 0, mi = 0;
  double sec = 0;
  if (std::sscanf(ts.c_str(), "%d-%d-%dT%d:%d:%lf", &y, &mo, &d, &h, &mi,
                  &sec) != 6)
    return std::nullopt;
  if (y < 1970)  // Ollama uses year 1 as "no expiry set"
    return std::nullopt;

  int offset_min = 0;
  std::size_t tz = ts.find_first_of("Z+-", 19);
  if (tz != std::string::npos && ts[tz] != 'Z') {
    int oh = 0, om = 0;
    if (std::sscanf(ts.c_str() + tz + 1, "%d:%d", &oh, &om) >= 1)
      offset_min = oh * 60 + om;
    if (ts[tz] == '-')
      offset_min = -offset_min;
  }

  std::tm tm{};
  tm.tm_year = y - 1900;
  tm.tm_mon = mo - 1;
  tm.tm_mday = d;
  tm.tm_hour = h;
  tm.tm_min = mi;
  tm.tm_sec = static_cast<int>(sec);
  time_t utc = timegm(&tm) - offset_min * 60;
  return difftime(utc, time(nullptr)) + (sec - std::floor(sec));
}

}  // namespace

OllamaSource::OllamaSource(AppState& state, std::string base_url)
    : Source(state), base_(std::move(base_url)) {}

void OllamaSource::poll() {
  bool online = false;
  std::string error;
  std::vector<OllamaModel> models;

  auto r = cpr::Get(cpr::Url{base_ + "/api/ps"}, cpr::Timeout{kTimeoutMs},
                    cpr::ConnectTimeout{kTimeoutMs});
  if (r.error.code == cpr::ErrorCode::OK && r.status_code == 200) {
    try {
      auto j = nlohmann::json::parse(r.text);
      online = true;
      for (const auto& jm : j.value("models", nlohmann::json::array())) {
        if (!jm.is_object())
          continue;
        OllamaModel m;
        m.name = jm.value("name", jm.value("model", std::string{"?"}));
        m.size_bytes = jm.value("size", std::uint64_t{0});
        m.size_vram = jm.value("size_vram", std::uint64_t{0});
        if (jm.contains("details") && jm["details"].is_object()) {
          m.params = jm["details"].value("parameter_size", std::string{});
          m.quant = jm["details"].value("quantization_level", std::string{});
        }
        if (auto eta = seconds_until(jm.value("expires_at", std::string{})))
          m.expires_in_s = std::max(0.0, *eta);
        models.push_back(std::move(m));
      }
    } catch (const nlohmann::json::exception&) {
      error = "malformed JSON from /api/ps";
    }
  } else if (r.error.code == cpr::ErrorCode::OK && r.status_code > 0) {
    error = "HTTP " + std::to_string(r.status_code) + " from /api/ps";
  } else {
    error = short_error(r.error);
  }

  std::scoped_lock lock(state_.mutex);
  state_.ollama.online = online;
  state_.ollama.error = online ? "" : error;
  state_.ollama.models = std::move(models);
}

}  // namespace llmtop
