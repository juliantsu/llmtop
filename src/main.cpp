#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cli.hpp"
#include "jthread_compat.hpp"
#include "sources/demo_source.hpp"
#include "sources/llamacpp_source.hpp"
#include "sources/nvml_source.hpp"
#include "sources/ollama_source.hpp"
#include "sources/source.hpp"
#include "state.hpp"
#include "ui/ui.hpp"

namespace llmtop {
namespace {

void run_source(Source& source, AppState& state, StopToken st) {
  using namespace std::chrono;
  while (!st.stop_requested()) {
    auto started = steady_clock::now();
    source.poll();
    auto interval = milliseconds(state.interval_ms.load());
    while (!st.stop_requested() && steady_clock::now() - started < interval)
      std::this_thread::sleep_for(milliseconds(25));
  }
}

}  // namespace
}  // namespace llmtop

int main(int argc, char** argv) {
  using namespace llmtop;

  Options opts = parse_cli(argc, argv);
  if (!opts.error.empty()) {
    std::fprintf(stderr, "llmtop: %s\n\n%s", opts.error.c_str(),
                 usage().c_str());
    return 2;
  }
  if (opts.help) {
    std::fputs(usage().c_str(), stdout);
    return 0;
  }
  if (opts.version) {
    std::puts("llmtop " LLMTOP_VERSION);
    return 0;
  }

  AppState state;
  state.interval_ms = opts.interval_ms;
  state.demo_mode = opts.demo;
  {
    std::scoped_lock lock(state.mutex);
    state.llama.url = opts.llamacpp_url;
    state.ollama.url = opts.ollama_url;
    state.gpu.disabled = opts.no_nvml && !opts.demo;
  }

  std::vector<std::unique_ptr<Source>> sources;
  if (opts.demo) {
    sources.push_back(std::make_unique<DemoSource>(state));
  } else {
    if (!opts.no_nvml)
      sources.push_back(std::make_unique<NvmlSource>(state));
    sources.push_back(std::make_unique<LlamaCppSource>(state, opts.llamacpp_url));
    sources.push_back(std::make_unique<OllamaSource>(state, opts.ollama_url));
  }

  // `threads` is declared after `sources`, so on scope exit each thread is
  // stopped and joined before the Source objects it uses are destroyed.
  std::vector<Jthread> threads;
  threads.reserve(sources.size());
  for (auto& source : sources) {
    threads.emplace_back([&state, src = source.get()](StopToken st) {
      run_source(*src, state, st);
    });
  }

  int rc = run_ui(state, opts);
  for (auto& t : threads)
    t.request_stop();
  return rc;
}
