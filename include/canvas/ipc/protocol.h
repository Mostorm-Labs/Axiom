#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace canvas::ipc {

// Version 1 is deliberately a small, newline-delimited control protocol. It
// is not an input transport: high-frequency samples stay in the native app.
struct Message {
  int protocolVersion = 1;
  std::string type;
  std::string requestId;
  nlohmann::json payload;
};

struct DecodeMessageResult {
  std::optional<Message> message;
  std::string error;
};

struct EncodeMessageResult {
  std::optional<std::string> encoded;
  std::string error;
};

DecodeMessageResult decode(const std::string& line);
std::string encode(const Message& message);
// Validates direction and envelope shape, then applies bounded structural and
// byte budgets before JSON serialization. maximumBytes excludes a line-ending.
EncodeMessageResult tryEncodeNativeEvent(const Message& message,
                                         std::size_t maximumBytes);
bool isAuthenticatedHello(const Message& message,
                          const std::string& expectedToken);
bool isLauncherCommand(std::string_view type);
bool isNativeEvent(std::string_view type);

}  // namespace canvas::ipc
