#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>

namespace llmtop {

struct Options {
  std::string llamacpp_url = "http://127.0.0.1:8080";
  std::string ollama_url = "http://127.0.0.1:11434";
  bool no_nvml = false;
  bool demo = false;
  bool version = false;
  bool help = false;
  int interval_ms = 500;
  std::string error;  // non-empty on parse failure
};

inline std::string usage() {
  return
      "llmtop " LLMTOP_VERSION " — top for local LLM inference (GPU + llama.cpp + Ollama)\n"
      "\n"
      "Usage: llmtop [options]\n"
      "\n"
      "  --llamacpp-url <url>  llama.cpp server base URL (default http://127.0.0.1:8080)\n"
      "  --ollama-url <url>    Ollama base URL            (default http://127.0.0.1:11434)\n"
      "  --no-nvml             disable GPU telemetry\n"
      "  --demo                run with a fake data generator (no GPU/servers needed)\n"
      "  --interval <ms>       poll interval, 100..5000 (default 500)\n"
      "  --version             print version and exit\n"
      "  --help                show this help\n"
      "\n"
      "Keys: q quit · 1/2/3 focus panels · +/- poll interval\n";
}

inline Options parse_cli(int argc, const char* const* argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto value = [&]() -> const char* {
      if (i + 1 >= argc) {
        o.error = "missing value for " + a;
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "--llamacpp-url") {
      const char* v = value();
      if (!v) return o;
      o.llamacpp_url = v;
    } else if (a == "--ollama-url") {
      const char* v = value();
      if (!v) return o;
      o.ollama_url = v;
    } else if (a == "--interval") {
      const char* v = value();
      if (!v) return o;
      int ms = std::atoi(v);
      if (ms <= 0) {
        o.error = "invalid --interval value: " + std::string(v);
        return o;
      }
      o.interval_ms = std::clamp(ms, 100, 5000);
    } else if (a == "--no-nvml") {
      o.no_nvml = true;
    } else if (a == "--demo") {
      o.demo = true;
    } else if (a == "--version" || a == "-v") {
      o.version = true;
    } else if (a == "--help" || a == "-h") {
      o.help = true;
    } else {
      o.error = "unknown option: " + a;
      return o;
    }
    // strip trailing '/' from URLs so we can append paths blindly
    while (!o.llamacpp_url.empty() && o.llamacpp_url.back() == '/')
      o.llamacpp_url.pop_back();
    while (!o.ollama_url.empty() && o.ollama_url.back() == '/')
      o.ollama_url.pop_back();
  }
  return o;
}

}  // namespace llmtop
