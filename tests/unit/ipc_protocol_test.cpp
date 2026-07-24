#include "canvas/ipc/protocol.h"
#include "canvas/ipc/client_session.h"

#include <gtest/gtest.h>

using namespace canvas;

TEST(IpcProtocolTest, AcceptsAuthenticatedHello) {
  const auto decoded = ipc::decode(
      R"({"protocolVersion":1,"type":"hello","requestId":"1","payload":{"token":"secret"}})");
  ASSERT_TRUE(decoded.message.has_value()) << decoded.error;
  EXPECT_TRUE(ipc::isAuthenticatedHello(*decoded.message, "secret"));
}

TEST(IpcProtocolTest, RejectsUnknownProtocolVersion) {
  const auto decoded = ipc::decode(
      R"({"protocolVersion":2,"type":"hello","requestId":"1","payload":{"token":"secret"}})");
  EXPECT_FALSE(decoded.message.has_value());
}

TEST(IpcProtocolTest, RejectsRawPointerMessages) {
  const auto decoded = ipc::decode(
      R"({"protocolVersion":1,"type":"pointer-sample","requestId":"1","payload":{}})");
  EXPECT_FALSE(decoded.message.has_value());
  EXPECT_NE(decoded.error.find("not allowed"), std::string::npos);
}

TEST(IpcProtocolTest, RejectsStrokeAndVideoFrames) {
  for (const char* type : {"stroke-point", "video-frame"}) {
    const auto decoded = ipc::decode(
        std::string(R"({"protocolVersion":1,"type":")") + type +
        R"(","requestId":"1","payload":{}})");
    EXPECT_FALSE(decoded.message.has_value()) << type;
    EXPECT_NE(decoded.error.find("not allowed"), std::string::npos);
  }
}

TEST(IpcProtocolTest, RejectsInvalidEnvelopeFields) {
  const auto decoded = ipc::decode(
      R"({"protocolVersion":1,"type":"hello","requestId":"1","payload":{},"extra":true})");
  EXPECT_FALSE(decoded.message.has_value());
  EXPECT_NE(decoded.error.find("unexpected"), std::string::npos);
}

TEST(IpcProtocolTest, EnforcesRequestIdUtf8ByteLimit) {
  std::string exact(254U, 'x');
  exact += "\xC2\xA2";
  ASSERT_EQ(exact.size(), 256U);
  const auto accepted = ipc::decode(
      nlohmann::json{{"protocolVersion", 1},
                     {"type", "set-mode"},
                     {"requestId", exact},
                     {"payload", nlohmann::json::object()}}
          .dump());
  ASSERT_TRUE(accepted.message.has_value()) << accepted.error;
  EXPECT_EQ(accepted.message->requestId, exact);

  const std::string oversized = exact + "x";
  ASSERT_EQ(oversized.size(), 257U);
  const auto rejected = ipc::decode(
      nlohmann::json{{"protocolVersion", 1},
                     {"type", "set-mode"},
                     {"requestId", oversized},
                     {"payload", nlohmann::json::object()}}
          .dump());
  EXPECT_FALSE(rejected.message.has_value());
  EXPECT_NE(rejected.error.find("256 UTF-8 bytes"), std::string::npos);
}

TEST(IpcProtocolTest, AuthenticatesExactlyOneHelloThenOnlyCommands) {
  ipc::ClientSession session("secret");
  const auto hello = session.accept(
      R"({"protocolVersion":1,"type":"hello","requestId":"1","payload":{"token":"secret"}})");
  ASSERT_TRUE(hello) << hello.error;
  EXPECT_TRUE(session.authenticated());

  const auto command = session.accept(
      R"({"protocolVersion":1,"type":"set-mode","requestId":"2","payload":{"mode":"draw"}})");
  EXPECT_TRUE(command) << command.error;

  for (const char* type : {"hello", "ready", "document-state"}) {
    const auto rejected = session.accept(
        std::string(R"({"protocolVersion":1,"type":")") + type +
        R"(","requestId":"3","payload":{}})");
    EXPECT_FALSE(rejected) << type;
    EXPECT_NE(rejected.error.find("launcher command"), std::string::npos);
  }
}

TEST(IpcProtocolTest, RejectsCommandBeforeAuthenticatedHello) {
  ipc::ClientSession session("secret");
  const auto result = session.accept(
      R"({"protocolVersion":1,"type":"set-mode","requestId":"1","payload":{}})");
  EXPECT_FALSE(result);
  EXPECT_FALSE(session.authenticated());
}

TEST(IpcProtocolTest, SeparatesLauncherCommandsFromNativeEvents) {
  EXPECT_TRUE(ipc::isLauncherCommand("set-mode"));
  EXPECT_FALSE(ipc::isNativeEvent("set-mode"));
  EXPECT_TRUE(ipc::isNativeEvent("response"));
  EXPECT_FALSE(ipc::isLauncherCommand("response"));
}

TEST(IpcProtocolTest, EncodesOnlyValidBoundedNativeEvents) {
  const ipc::Message valid{1, "response", "request-1",
                           nlohmann::json{{"accepted", true}}};
  const auto encoded = ipc::tryEncodeNativeEvent(valid, 1024U);
  ASSERT_TRUE(encoded.encoded.has_value()) << encoded.error;
  const auto roundTrip = ipc::decode(*encoded.encoded);
  ASSERT_TRUE(roundTrip.message.has_value()) << roundTrip.error;
  EXPECT_EQ(roundTrip.message->type, "response");

  for (const ipc::Message& invalid : {
           ipc::Message{2, "response", "request-1", nlohmann::json::object()},
           ipc::Message{1, "set-mode", "request-1", nlohmann::json::object()},
           ipc::Message{1, "response", "", nlohmann::json::object()},
           ipc::Message{1, "response", std::string(257U, 'x'),
                        nlohmann::json::object()},
           ipc::Message{1, "response", "request-1", nlohmann::json::array()},
       }) {
    EXPECT_FALSE(ipc::tryEncodeNativeEvent(invalid, 1024U).encoded.has_value());
  }
}

TEST(IpcProtocolTest, RejectsLargeOutboundPayloadBeforeSerialization) {
  const ipc::Message message{
      1, "document-state", "request-1",
      nlohmann::json{{"content", std::string(1024U * 1024U, 'x')}}};
  const auto encoded = ipc::tryEncodeNativeEvent(
      message, ipc::LineFramer::kMaximumLineBytes);
  EXPECT_FALSE(encoded.encoded.has_value());
  EXPECT_NE(encoded.error.find("budget"), std::string::npos);
}

TEST(IpcProtocolTest, RejectsExcessivelyDeepOutboundPayload) {
  nlohmann::json payload = nlohmann::json::object();
  nlohmann::json* cursor = &payload;
  for (std::size_t depth = 0; depth < 34U; ++depth) {
    (*cursor)["child"] = nlohmann::json::object();
    cursor = &(*cursor)["child"];
  }
  const ipc::Message message{1, "diagnostics", "request-1", std::move(payload)};
  EXPECT_FALSE(ipc::tryEncodeNativeEvent(
                   message, ipc::LineFramer::kMaximumLineBytes)
                   .encoded.has_value());
}

TEST(IpcProtocolTest, FramesFragmentedCrLfLines) {
  ipc::LineFramer framer;
  EXPECT_TRUE(framer.append("first\r"));
  const auto result = framer.append("\nsecond\nthird");
  ASSERT_TRUE(result) << result.error;
  ASSERT_EQ(result.lines.size(), 2U);
  EXPECT_EQ(result.lines[0], "first");
  EXPECT_EQ(result.lines[1], "second");
  const auto final = framer.append("\n");
  ASSERT_TRUE(final) << final.error;
  ASSERT_EQ(final.lines.size(), 1U);
  EXPECT_EQ(final.lines[0], "third");
}

TEST(IpcProtocolTest, EnforcesOneMiBLineBoundary) {
  ipc::LineFramer exact;
  const std::string maximum(ipc::LineFramer::kMaximumLineBytes, 'x');
  const auto accepted = exact.append(maximum + "\n");
  ASSERT_TRUE(accepted) << accepted.error;
  ASSERT_EQ(accepted.lines.size(), 1U);
  EXPECT_EQ(accepted.lines[0].size(), ipc::LineFramer::kMaximumLineBytes);

  ipc::LineFramer exactCrLf;
  const auto acceptedCrLf = exactCrLf.append(maximum + "\r\n");
  ASSERT_TRUE(acceptedCrLf) << acceptedCrLf.error;
  ASSERT_EQ(acceptedCrLf.lines.size(), 1U);
  EXPECT_EQ(acceptedCrLf.lines[0].size(), ipc::LineFramer::kMaximumLineBytes);

  ipc::LineFramer tooLarge;
  const std::string oversized(ipc::LineFramer::kMaximumLineBytes + 1U, 'x');
  const auto rejected = tooLarge.append(oversized + "\n");
  EXPECT_FALSE(rejected);
  EXPECT_NE(rejected.error.find("1 MiB"), std::string::npos);

  ipc::LineFramer unterminated;
  const auto rejectedBeforeNewline = unterminated.append(oversized);
  EXPECT_FALSE(rejectedBeforeNewline);
}

TEST(IpcProtocolTest, DoesNotAccumulateAnUnboundedReadChunk) {
  ipc::LineFramer framer;
  const std::string huge(ipc::LineFramer::kMaximumLineBytes + 2U, 'x');
  const auto result = framer.append(huge);
  EXPECT_FALSE(result);
  EXPECT_NE(result.error.find("1 MiB"), std::string::npos);
}
