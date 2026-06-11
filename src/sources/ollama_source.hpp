#pragma once

#include <string>

#include "sources/source.hpp"

namespace llmtop {

// Polls Ollama's GET /api/ps for loaded models, VRAM footprint and
// time-until-unload.
class OllamaSource final : public Source {
 public:
  OllamaSource(AppState& state, std::string base_url);

  void poll() override;

 private:
  std::string base_;
};

}  // namespace llmtop
