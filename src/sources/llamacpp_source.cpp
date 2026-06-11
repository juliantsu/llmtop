#include "sources/llamacpp_source.hpp"

#include <cpr/cpr.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

#include "sources/http.hpp"
#include "sources/prometheus.hpp"

namespace llmtop {

namespace {

constexpr long kTimeoutMs = 500;

double metric(const std::unordered_map<std::string, double>& m,
              const char* name, double fallback) {
  auto it = m.find(name);
  return it == m.end() ? fallback : it->second;
}

std::int64_t get_int(const nlohmann::json& j, const char* key,
                     std::int64_t fallback) {
  auto it = j.find(key);
  return it != j.end() && it->is_number() ? it->get<std::int64_t>() : fallback;
}

struct SlotData {
  LlamaSlot ui;
  std::int64_t id_task = -1;
  std::int64_t n_decoded = -1;  // -1 = not reported by this server build
  std::int64_t n_prompt = 0;
  std::int64_t n_prompt_cache = 0;
  std::int64_t n_prompt_processed = 0;
};

// Parse a /slots response defensively: field names and nesting have shifted
// across llama.cpp versions, so probe the known spellings and default to
// "unknown" rather than failing.
std::vector<SlotData> parse_slots(const nlohmann::json& j) {
  std::vector<SlotData> slots;
  if (!j.is_array())
    return slots;
  for (const auto& js : j) {
    if (!js.is_object())
      continue;
    SlotData d;
    d.ui.id = static_cast<int>(get_int(js, "id", static_cast<std::int64_t>(slots.size())));
    if (js.contains("is_processing") && js["is_processing"].is_boolean())
      d.ui.processing = js["is_processing"].get<bool>();
    else if (js.contains("state") && js["state"].is_number())
      d.ui.processing = js["state"].get<int>() != 0;
    d.ui.n_ctx = get_int(js, "n_ctx", 0);
    d.id_task = get_int(js, "id_task", -1);
    d.n_prompt = get_int(js, "n_prompt_tokens", 0);
    d.n_prompt_cache = get_int(js, "n_prompt_tokens_cache", 0);
    d.n_prompt_processed = get_int(js, "n_prompt_tokens_processed", 0);
    d.n_decoded = get_int(js, "n_decoded", -1);

    // newer builds nest the generation progress under "next_token"
    // (object in some versions, single-element array in others)
    const nlohmann::json* nt = nullptr;
    if (auto it = js.find("next_token"); it != js.end()) {
      if (it->is_object())
        nt = &*it;
      else if (it->is_array() && !it->empty() && (*it)[0].is_object())
        nt = &(*it)[0];
    }
    if (nt)
      d.n_decoded = std::max(d.n_decoded, get_int(*nt, "n_decoded", -1));

    // context used: explicit n_past when available, otherwise prompt + decoded
    d.ui.n_past = get_int(js, "n_past", 0);
    if (d.ui.n_past == 0 && d.n_decoded >= 0 && (d.ui.processing || d.n_decoded > 0))
      d.ui.n_past = d.n_prompt + d.n_decoded;
    slots.push_back(d);
  }
  return slots;
}

// Reads `general.name` from a local GGUF file's metadata — the human-readable
// model name ("Llama 3.2 3B Instruct"). Ollama stores models as opaque
// "sha256-…" blobs, so the file name alone is useless there. Returns "" if
// the file is remote, unreadable, or not GGUF.
std::string gguf_general_name(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    return "";
  auto read_u32 = [&]() { std::uint32_t v = 0; f.read(reinterpret_cast<char*>(&v), 4); return v; };
  auto read_u64 = [&]() { std::uint64_t v = 0; f.read(reinterpret_cast<char*>(&v), 8); return v; };
  auto read_str = [&]() -> std::string {
    std::uint64_t len = read_u64();
    if (!f || len > 1 << 20) { f.setstate(std::ios::failbit); return ""; }
    std::string s(static_cast<std::size_t>(len), '\0');
    f.read(s.data(), static_cast<std::streamsize>(len));
    return s;
  };
  // value sizes per GGUF type id; 8=string, 9=array (variable)
  static constexpr int kSize[] = {1, 1, 2, 2, 4, 4, 4, 1, -1, -2, 8, 8, 8};
  auto skip_value = [&](std::uint32_t type, auto&& self) -> void {
    if (type > 12) { f.setstate(std::ios::failbit); return; }
    if (kSize[type] > 0) { f.seekg(kSize[type], std::ios::cur); return; }
    if (type == 8) { read_str(); return; }
    std::uint32_t elem = read_u32();   // array
    std::uint64_t count = read_u64();
    if (!f || count > 1 << 26) { f.setstate(std::ios::failbit); return; }
    if (elem <= 12 && kSize[elem] > 0) {
      f.seekg(static_cast<std::streamoff>(count * static_cast<std::uint64_t>(kSize[elem])), std::ios::cur);
    } else {
      for (std::uint64_t i = 0; i < count && f; ++i)
        self(elem, self);
    }
  };

  if (read_u32() != 0x46554747)  // "GGUF"
    return "";
  read_u32();  // version
  read_u64();  // tensor count
  std::uint64_t n_kv = read_u64();
  if (!f || n_kv > 100000)
    return "";
  for (std::uint64_t i = 0; i < n_kv && f; ++i) {
    std::string key = read_str();
    std::uint32_t type = read_u32();
    if (key == "general.name" && type == 8) {
      std::string name = read_str();
      return f ? name : "";
    }
    skip_value(type, skip_value);
  }
  return "";
}

// "/models/llama-3.2-3b-instruct-Q4_K_M.gguf" → "llama-3.2-3b-instruct-Q4_K_M",
// Ollama blob hashes are shortened to "sha256-dde5aa3f…".
std::string pretty_model_name(std::string name) {
  if (auto slash = name.find_last_of('/'); slash != std::string::npos)
    name = name.substr(slash + 1);
  if (name.size() > 5 && name.ends_with(".gguf"))
    name.resize(name.size() - 5);
  if (name.rfind("sha256-", 0) == 0 && name.size() > 7 + 16)
    name = name.substr(0, 7 + 8) + "…";
  return name;
}

}  // namespace

LlamaCppSource::LlamaCppSource(AppState& state, std::string base_url)
    : Source(state), base_(std::move(base_url)) {}

void LlamaCppSource::poll() {
  using clock = std::chrono::steady_clock;
  bool reachable = false;
  std::string error;
  std::string note;

  // --- /slots ---
  bool have_slots = false;
  std::vector<SlotData> slots;
  auto rs = cpr::Get(cpr::Url{base_ + "/slots"}, cpr::Timeout{kTimeoutMs},
                     cpr::ConnectTimeout{kTimeoutMs});
  if (rs.error.code == cpr::ErrorCode::OK && rs.status_code > 0) {
    reachable = true;
    if (rs.status_code == 200) {
      try {
        slots = parse_slots(nlohmann::json::parse(rs.text));
        have_slots = true;
      } catch (const nlohmann::json::exception&) {
        note = "/slots returned malformed JSON";
      }
    } else {
      note = "/slots disabled — start the server with --slots";
    }
  } else {
    error = short_error(rs.error);
  }

  // --- live rates from per-slot token counters ---
  auto now = clock::now();
  double slots_dt =
      have_prev_slots_
          ? std::chrono::duration<double>(now - prev_slots_time_).count()
          : 0.0;
  bool slot_counters = false;
  double slot_gen_rate = 0, slot_prompt_rate = 0;
  if (have_slots) {
    std::int64_t gen_tokens = 0;
    for (const auto& d : slots) {
      if (d.n_decoded < 0)
        continue;
      slot_counters = true;
      SlotTrack& t = track_[d.ui.id];
      if (d.id_task != t.id_task) {
        t.id_task = d.id_task;
        t.task_start = now;
        t.decoding_seen = d.n_decoded > 0;
        if (d.ui.processing && d.n_decoded > 0)
          gen_tokens += d.n_decoded;  // task started and decoded since last poll
        t.n_decoded = std::max<std::int64_t>(d.n_decoded, 0);
      } else {
        if (d.n_decoded > t.n_decoded)
          gen_tokens += d.n_decoded - t.n_decoded;
        if (!t.decoding_seen && d.n_decoded > 0) {
          // first token arrived → prompt processing for this task just
          // finished; credit the evaluated prompt over the prompt phase
          double pdt = std::chrono::duration<double>(now - t.task_start).count();
          pdt = std::max(std::max(slots_dt, 0.1), std::min(pdt, 30.0));
          std::int64_t ptok = std::max<std::int64_t>(d.n_prompt - d.n_prompt_cache, 0);
          if (ptok == 0)
            ptok = d.n_prompt_processed;
          slot_prompt_rate += static_cast<double>(ptok) / pdt;
          t.decoding_seen = true;
        }
        t.n_decoded = std::max(t.n_decoded, d.n_decoded);
      }
    }
    if (slots_dt > 0.05)
      slot_gen_rate = static_cast<double>(gen_tokens) / slots_dt;
    prev_slots_time_ = now;
    have_prev_slots_ = true;
  } else {
    have_prev_slots_ = false;
    track_.clear();
  }

  // --- /metrics ---
  bool have_metrics = false;
  double metrics_gen = 0, metrics_prompt = 0, kv_ratio = -1;
  std::int64_t kv_tokens = -1;
  int processing = 0, deferred = 0;
  auto rm = cpr::Get(cpr::Url{base_ + "/metrics"}, cpr::Timeout{kTimeoutMs},
                     cpr::ConnectTimeout{kTimeoutMs});
  if (rm.error.code == cpr::ErrorCode::OK && rm.status_code > 0) {
    reachable = true;
    if (rm.status_code == 200) {
      have_metrics = true;
      auto m = parse_prometheus_text(rm.text);
      double predicted_total = metric(m, "llamacpp:tokens_predicted_total", -1);
      double prompt_total = metric(m, "llamacpp:prompt_tokens_total", -1);
      auto mnow = clock::now();
      if (have_prev_metrics_ && predicted_total >= 0) {
        double dt = std::chrono::duration<double>(mnow - prev_metrics_time_).count();
        if (dt > 0.05) {
          metrics_gen = std::max(0.0, (predicted_total - prev_predicted_total_) / dt);
          metrics_prompt = std::max(0.0, (prompt_total - prev_prompt_total_) / dt);
        }
      }
      if (predicted_total >= 0) {
        prev_predicted_total_ = predicted_total;
        prev_prompt_total_ = std::max(0.0, prompt_total);
        prev_metrics_time_ = mnow;
        have_prev_metrics_ = true;
      }
      kv_ratio = metric(m, "llamacpp:kv_cache_usage_ratio", -1);
      double kvt = metric(m, "llamacpp:kv_cache_tokens", -1);
      kv_tokens = kvt < 0 ? -1 : static_cast<std::int64_t>(kvt);
      processing = static_cast<int>(metric(m, "llamacpp:requests_processing", 0));
      deferred = static_cast<int>(metric(m, "llamacpp:requests_deferred", 0));
    } else if (note.empty()) {
      note = "/metrics disabled — start the server with --metrics";
    } else {
      note = "/slots and /metrics disabled — start the server with --slots --metrics";
    }
  } else if (!reachable) {
    error = short_error(rm.error);
  }

  // --- /props: which model is loaded ---
  // Retried every poll until it definitively succeeds or the endpoint turns
  // out not to exist — during model load the server is reachable but answers
  // /props with 503, and we must not give up on that.
  if (reachable && !props_tried_) {
    auto rp = cpr::Get(cpr::Url{base_ + "/props"}, cpr::Timeout{kTimeoutMs},
                       cpr::ConnectTimeout{kTimeoutMs});
    if (rp.error.code == cpr::ErrorCode::OK) {
      if (rp.status_code == 200) {
        props_tried_ = true;
        try {
          auto j = nlohmann::json::parse(rp.text);
          std::string path = j.value("model_path", std::string{});
          // prefer the model's own metadata name; works whenever the server
          // runs on this machine (the common case for a local monitor)
          model_ = gguf_general_name(path);
          if (model_.empty()) {
            std::string name = j.value("model_alias", std::string{});
            model_ = pretty_model_name(name.empty() ? path : name);
          }
        } catch (const nlohmann::json::exception&) {
        }
      } else if (rp.status_code == 404 || rp.status_code == 501 ||
                 rp.status_code == 403) {
        props_tried_ = true;  // endpoint disabled on this server build
      }
    }
  }
  if (!reachable) {
    have_prev_metrics_ = false;
    props_tried_ = false;  // re-resolve the model after a reconnect
    model_.clear();
  }

  double gen_tps = slot_counters ? slot_gen_rate : metrics_gen;
  double prompt_tps = slot_counters ? slot_prompt_rate : metrics_prompt;

  // Light smoothing so the big number doesn't jitter at every poll.
  gen_smooth_ = reachable ? 0.6 * gen_tps + 0.4 * gen_smooth_ : 0.0;
  prompt_smooth_ = reachable ? 0.6 * prompt_tps + 0.4 * prompt_smooth_ : 0.0;

  std::vector<LlamaSlot> ui_slots;
  ui_slots.reserve(slots.size());
  for (const auto& d : slots) {
    ui_slots.push_back(d.ui);
    if (!have_metrics && d.ui.processing)
      ++processing;
  }

  std::scoped_lock lock(state_.mutex);
  LlamaState& L = state_.llama;
  L.online = reachable;
  L.model = model_;
  L.error = reachable ? "" : error;
  L.note = note;
  L.have_slots = have_slots;
  L.have_metrics = have_metrics;
  L.slots = std::move(ui_slots);
  L.gen_tps = gen_smooth_;
  L.prompt_tps = prompt_smooth_;
  L.kv_ratio = kv_ratio;
  L.kv_tokens = kv_tokens;
  L.requests_processing = processing;
  L.requests_deferred = deferred;
  L.gen_history.push(static_cast<float>(gen_smooth_));
  L.prompt_history.push(static_cast<float>(prompt_smooth_));
}

}  // namespace llmtop
