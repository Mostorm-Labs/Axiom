#pragma once

#include "canvas/ipc/protocol.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace canvas::ipc {

// Newline framing is deliberately byte based: JSON itself is UTF-8 and a
// complete line is validated by decode().  The limit excludes LF and a
// possible CR in CRLF.
class LineFramer {
 public:
  static constexpr std::size_t kMaximumLineBytes = 1024U * 1024U;

  struct Result {
    std::vector<std::string> lines;
    std::string error;
    explicit operator bool() const { return error.empty(); }
  };

  Result append(std::string_view bytes);
  void reset();

 private:
  std::string pending_;
};

// Enforces the one-way launcher -> native command stream.  A connection must
// begin with exactly one authenticated hello; native events never enter this
// state machine.
class ClientSession {
 public:
  explicit ClientSession(std::string expectedToken);

  struct Result {
    std::optional<Message> message;
    std::string error;
    explicit operator bool() const { return error.empty(); }
  };

  Result accept(const std::string& line);
  bool authenticated() const { return authenticated_; }

 private:
  std::string expectedToken_;
  bool authenticated_ = false;
};

}  // namespace canvas::ipc
