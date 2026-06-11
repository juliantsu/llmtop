#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <unordered_map>

namespace llmtop {

// Minimal Prometheus text-exposition parser: `name{labels} value [timestamp]`.
// Labels are ignored; for duplicate names the last sample wins. Good enough
// for llama.cpp's /metrics, which exposes plain unlabelled gauges/counters.
inline std::unordered_map<std::string, double> parse_prometheus_text(
    const std::string& body) {
  std::unordered_map<std::string, double> out;
  std::size_t pos = 0;
  while (pos < body.size()) {
    std::size_t eol = body.find('\n', pos);
    if (eol == std::string::npos)
      eol = body.size();
    std::string_view line(body.data() + pos, eol - pos);
    pos = eol + 1;

    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
      line.remove_suffix(1);
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
      line.remove_prefix(1);
    if (line.empty() || line.front() == '#')
      continue;

    std::size_t name_end = line.find_first_of("{ \t");
    if (name_end == 0 || name_end == std::string_view::npos)
      continue;
    std::string name(line.substr(0, name_end));

    std::size_t value_begin = name_end;
    if (line[name_end] == '{') {
      std::size_t close = line.find('}', name_end);
      if (close == std::string_view::npos)
        continue;
      value_begin = close + 1;
    }
    while (value_begin < line.size() &&
           (line[value_begin] == ' ' || line[value_begin] == '\t'))
      ++value_begin;
    if (value_begin >= line.size())
      continue;

    std::size_t value_end = line.find_first_of(" \t", value_begin);
    std::string value(line.substr(
        value_begin, value_end == std::string_view::npos ? std::string_view::npos
                                                         : value_end - value_begin));
    char* parse_end = nullptr;
    double d = std::strtod(value.c_str(), &parse_end);
    if (parse_end != value.c_str())
      out[name] = d;
  }
  return out;
}

}  // namespace llmtop
