#pragma once

#include <cpr/cpr.h>

#include <string>

namespace llmtop {

// Compact human-readable form of a cpr/curl transport error, suitable for
// a one-line "offline" panel message.
inline std::string short_error(const cpr::Error& error) {
  switch (error.code) {
    case cpr::ErrorCode::COULDNT_CONNECT:
      return "connection refused";
    case cpr::ErrorCode::OPERATION_TIMEDOUT:
      return "timeout";
    case cpr::ErrorCode::COULDNT_RESOLVE_HOST:
      return "cannot resolve host";
    case cpr::ErrorCode::URL_MALFORMAT:
    case cpr::ErrorCode::UNSUPPORTED_PROTOCOL:
      return "invalid URL";
    default:
      return error.message.empty() ? "unreachable" : error.message;
  }
}

}  // namespace llmtop
