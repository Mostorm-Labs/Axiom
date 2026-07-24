#include "canvas/ipc/client_session.h"

#include <utility>

namespace canvas::ipc {

LineFramer::Result LineFramer::append(std::string_view bytes) {
  Result result;
  // Do not append an arbitrary read buffer wholesale: a malicious peer can
  // otherwise make a single read allocate far beyond the line limit.
  for (const char byte : bytes) {
    if (byte == '\n') {
      if (!pending_.empty() && pending_.back() == '\r') pending_.pop_back();
      if (pending_.size() > kMaximumLineBytes) {
        pending_.clear();
        result.lines.clear();
        result.error = "IPC line exceeds 1 MiB";
        return result;
      }
      result.lines.push_back(std::move(pending_));
      pending_.clear();
      continue;
    }
    // One additional byte is allowed only for the CR of a possible CRLF.
    if (pending_.size() == kMaximumLineBytes && byte != '\r') {
      pending_.clear();
      result.lines.clear();
      result.error = "IPC line exceeds 1 MiB";
      return result;
    }
    if (pending_.size() >= kMaximumLineBytes + 1U) {
      pending_.clear();
      result.lines.clear();
      result.error = "IPC line exceeds 1 MiB";
      return result;
    }
    pending_.push_back(byte);
  }
  return result;
}

void LineFramer::reset() { pending_.clear(); }

ClientSession::ClientSession(std::string expectedToken)
    : expectedToken_(std::move(expectedToken)) {}

ClientSession::Result ClientSession::accept(const std::string& line) {
  const DecodeMessageResult decoded = decode(line);
  if (!decoded.message) return {std::nullopt, decoded.error};

  if (!authenticated_) {
    if (!isAuthenticatedHello(*decoded.message, expectedToken_)) {
      return {std::nullopt, "first IPC message must be an authenticated hello"};
    }
    authenticated_ = true;
    return {std::move(decoded.message), {}};
  }
  if (!isLauncherCommand(decoded.message->type)) {
    return {std::nullopt, "IPC client message is not a launcher command"};
  }
  return {std::move(decoded.message), {}};
}

}  // namespace canvas::ipc
