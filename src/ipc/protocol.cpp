#include "canvas/ipc/protocol.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace canvas::ipc {
namespace {

constexpr std::array<std::string_view, 18> kAllowedTypes{
    "hello",          "open-document",       "save-document",
    "set-tool",       "set-mode",            "create-embedded",
    "set-embedded-bounds", "delete-node",     "enter-interaction",
    "leave-interaction", "shutdown",          "ready",
    "response",       "document-state",      "selection-changed",
    "embedded-state", "diagnostics",         "fatal-error"};

bool isAllowedType(std::string_view type) {
  for (const auto allowed : kAllowedTypes) {
    if (type == allowed) return true;
  }
  return false;
}

constexpr std::array<std::string_view, 10> kLauncherCommands{
    "open-document", "save-document", "set-tool", "set-mode",
    "create-embedded", "set-embedded-bounds", "delete-node",
    "enter-interaction", "leave-interaction", "shutdown"};

constexpr std::array<std::string_view, 7> kNativeEvents{
    "ready", "response", "document-state", "selection-changed",
    "embedded-state", "diagnostics", "fatal-error"};

template <std::size_t N>
bool contains(const std::array<std::string_view, N>& values,
              std::string_view value) {
  for (const auto candidate : values) {
    if (candidate == value) return true;
  }
  return false;
}

DecodeMessageResult fail(std::string error) {
  return DecodeMessageResult{std::nullopt, std::move(error)};
}

EncodeMessageResult encodeFail(std::string error) {
  return EncodeMessageResult{std::nullopt, std::move(error)};
}

constexpr std::size_t kMaximumRequestIdBytes = 256U;
constexpr std::size_t kMaximumJsonDepth = 32U;
constexpr std::size_t kMaximumJsonNodes = 16384U;
constexpr std::string_view kEnvelopeSkeleton =
    R"({"payload":,"protocolVersion":1,"requestId":,"type":})";

struct JsonBudget {
  std::size_t remainingBytes;
  std::size_t remainingNodes = kMaximumJsonNodes;

  bool consume(std::size_t bytes) {
    if (bytes > remainingBytes) return false;
    remainingBytes -= bytes;
    return true;
  }

  bool consumeEscapedString(std::string_view value) {
    // Reject an obviously oversized string in O(1), then scan at most the
    // bounded wire budget to account for JSON escapes before dump().
    if (remainingBytes < 2U || value.size() > remainingBytes - 2U) return false;
    if (!consume(2U)) return false;
    for (const unsigned char byte : value) {
      std::size_t encodedBytes = 1U;
      if (byte == '"' || byte == '\\' || byte == '\b' || byte == '\t' ||
          byte == '\n' || byte == '\f' || byte == '\r') {
        encodedBytes = 2U;
      } else if (byte < 0x20U) {
        encodedBytes = 6U;
      }
      if (!consume(encodedBytes)) return false;
    }
    return true;
  }
};

bool consumeJsonBudget(const nlohmann::json& value, std::size_t depth,
                       JsonBudget& budget) {
  if (depth > kMaximumJsonDepth || budget.remainingNodes == 0U) return false;
  --budget.remainingNodes;
  if (value.is_null()) return budget.consume(4U);
  if (value.is_boolean()) return budget.consume(5U);
  if (value.is_number_integer() || value.is_number_unsigned()) {
    return budget.consume(24U);
  }
  if (value.is_number_float()) {
    return std::isfinite(value.get<double>()) && budget.consume(32U);
  }
  if (value.is_string()) {
    return budget.consumeEscapedString(
        value.get_ref<const std::string&>());
  }
  if (value.is_array()) {
    if (!budget.consume(2U)) return false;
    bool first = true;
    for (const auto& child : value) {
      if ((!first && !budget.consume(1U)) ||
          !consumeJsonBudget(child, depth + 1U, budget)) {
        return false;
      }
      first = false;
    }
    return true;
  }
  if (value.is_object()) {
    if (!budget.consume(2U)) return false;
    bool first = true;
    for (const auto& [key, child] : value.items()) {
      if ((!first && !budget.consume(1U)) ||
          !budget.consumeEscapedString(key) || !budget.consume(1U) ||
          !consumeJsonBudget(child, depth + 1U, budget)) {
        return false;
      }
      first = false;
    }
    return true;
  }
  return false;
}

}  // namespace

DecodeMessageResult decode(const std::string& line) {
  try {
    const nlohmann::json value = nlohmann::json::parse(line);
    if (!value.is_object()) return fail("IPC envelope must be an object");
    constexpr std::array<std::string_view, 4> kEnvelopeFields{
        "protocolVersion", "type", "requestId", "payload"};
    if (value.size() != kEnvelopeFields.size()) {
      return fail("IPC envelope has unexpected fields");
    }
    for (const auto& [key, ignored] : value.items()) {
      (void)ignored;
      if (!contains(kEnvelopeFields, key)) {
        return fail("IPC envelope has unexpected fields");
      }
    }
    if (!value.contains("protocolVersion") ||
        !value.at("protocolVersion").is_number_integer() ||
        value.at("protocolVersion").get<int>() != 1) {
      return fail("unsupported IPC protocol version");
    }
    if (!value.contains("type") || !value.at("type").is_string()) {
      return fail("IPC envelope requires a type");
    }
    const std::string type = value.at("type").get<std::string>();
    if (type.empty()) return fail("IPC type must not be empty");
    if (!isAllowedType(type)) return fail("IPC type is not allowed: " + type);
    if (!value.contains("requestId") || !value.at("requestId").is_string()) {
      return fail("IPC envelope requires a requestId");
    }
    const std::string requestId = value.at("requestId").get<std::string>();
    if (requestId.empty()) return fail("IPC requestId must not be empty");
    if (requestId.size() > kMaximumRequestIdBytes) {
      return fail("IPC requestId exceeds 256 UTF-8 bytes");
    }
    if (!value.contains("payload") || !value.at("payload").is_object()) {
      return fail("IPC payload must be an object");
    }
    return DecodeMessageResult{
        Message{1, type, requestId, value.at("payload")}, {}};
  } catch (const nlohmann::json::exception& error) {
    return fail(std::string("invalid IPC JSON: ") + error.what());
  }
}

std::string encode(const Message& message) {
  return nlohmann::json{{"protocolVersion", message.protocolVersion},
                        {"type", message.type},
                        {"requestId", message.requestId},
                        {"payload", message.payload}}
      .dump();
}

EncodeMessageResult tryEncodeNativeEvent(const Message& message,
                                         std::size_t maximumBytes) {
  if (message.protocolVersion != 1) {
    return encodeFail("unsupported IPC protocol version");
  }
  if (!isNativeEvent(message.type)) {
    return encodeFail("outbound IPC message is not a native event");
  }
  if (message.requestId.empty() ||
      message.requestId.size() > kMaximumRequestIdBytes) {
    return encodeFail("IPC requestId is empty or too large");
  }
  if (!message.payload.is_object()) {
    return encodeFail("IPC payload must be an object");
  }
  // The skeleton contains all fixed syntax, excluding payload, type, and ID.
  JsonBudget budget{maximumBytes};
  if (!budget.consume(kEnvelopeSkeleton.size()) ||
      !budget.consumeEscapedString(message.type) ||
      !budget.consumeEscapedString(message.requestId) ||
      !consumeJsonBudget(message.payload, 0U, budget)) {
    return encodeFail("outbound IPC message exceeds structural or byte budget");
  }
  try {
    std::string encoded = encode(message);
    if (encoded.size() > maximumBytes) {
      return encodeFail("outbound IPC message exceeds byte budget");
    }
    return EncodeMessageResult{std::move(encoded), {}};
  } catch (const nlohmann::json::exception& error) {
    return encodeFail(std::string("could not encode IPC message: ") + error.what());
  }
}

bool isAuthenticatedHello(const Message& message,
                          const std::string& expectedToken) {
  if (expectedToken.empty() || message.protocolVersion != 1 ||
      message.type != "hello" || !message.payload.is_object()) {
    return false;
  }
  const auto token = message.payload.find("token");
  return token != message.payload.end() && token->is_string() &&
         token->get<std::string>() == expectedToken;
}

bool isLauncherCommand(std::string_view type) {
  return contains(kLauncherCommands, type);
}

bool isNativeEvent(std::string_view type) { return contains(kNativeEvents, type); }

}  // namespace canvas::ipc
