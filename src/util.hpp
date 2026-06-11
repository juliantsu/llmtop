#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

namespace llmtop {

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
inline std::string strf(const char* format, ...) {
  char buf[256];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buf, sizeof(buf), format, args);
  va_end(args);
  return buf;
}

inline std::string human_bytes(double bytes) {
  static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
  int i = 0;
  while (bytes >= 1024.0 && i < 4) {
    bytes /= 1024.0;
    ++i;
  }
  return strf(bytes >= 10 || i == 0 ? "%.0f %s" : "%.1f %s", bytes, units[i]);
}

// "4:32", "1:02:08"; absurdly large values render as "∞".
inline std::string format_eta(double seconds) {
  if (seconds < 0)
    return "?";
  if (seconds > 86400.0 * 365)
    return "∞";
  long s = static_cast<long>(seconds);
  long h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
  if (h > 0)
    return strf("%ld:%02ld:%02ld", h, m, sec);
  return strf("%ld:%02ld", m, sec);
}

}  // namespace llmtop
