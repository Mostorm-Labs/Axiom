#pragma once

#include "canvas/ipc/protocol.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace canvas::windows {

// Owns one authenticated, newline-delimited named-pipe client at a time.
// Message handlers run on the pipe reader thread; UI work must be marshalled
// to the window thread by the caller. A handler may call stop() to request
// cancellation, but it must not destroy the server from that callback.
class NamedPipeServer {
 public:
  using ConnectionId = std::uint64_t;
  static constexpr ConnectionId kInvalidConnectionId = 0;
  using MessageHandler =
      std::function<void(const ipc::Message&, ConnectionId)>;

  NamedPipeServer();
  ~NamedPipeServer();
  NamedPipeServer(const NamedPipeServer&) = delete;
  NamedPipeServer& operator=(const NamedPipeServer&) = delete;

  bool start(std::wstring pipeName, std::string sessionToken,
             MessageHandler handler, std::string& error);
  // Sends only when connectionId still names the authenticated connection that
  // produced the command. The call performs bounded in-memory enqueueing only.
  void send(const ipc::Message& message, ConnectionId connectionId);
  bool isCurrentConnection(ConnectionId connectionId) const;
  void stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace canvas::windows
