#include "platform/windows/named_pipe_server.h"

#include "canvas/ipc/client_session.h"

#include <windows.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace canvas::windows {
namespace {

using namespace std::chrono_literals;

class PipeHandle {
 public:
  explicit PipeHandle(HANDLE handle = INVALID_HANDLE_VALUE) : handle_(handle) {}
  ~PipeHandle() {
    if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
  }
  PipeHandle(const PipeHandle&) = delete;
  PipeHandle& operator=(const PipeHandle&) = delete;
  HANDLE get() const { return handle_; }

 private:
  HANDLE handle_;
};

PipeHandle connectClient(const std::wstring& pipeName) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  for (;;) {
    const HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE,
                                    0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) return PipeHandle(pipe);
    if (std::chrono::steady_clock::now() >= deadline) return PipeHandle();
    (void)WaitNamedPipeW(pipeName.c_str(), 50);
  }
}

bool writeLine(HANDLE pipe, const std::string& line) {
  DWORD written = 0;
  return WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), &written,
                   nullptr) != FALSE &&
         written == static_cast<DWORD>(line.size());
}

std::string envelope(std::string type, std::string requestId,
                     nlohmann::json payload) {
  return ipc::encode(
             ipc::Message{1, std::move(type), std::move(requestId),
                          std::move(payload)}) +
         "\n";
}

std::string readLine(HANDLE pipe) {
  ipc::LineFramer framer;
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (std::chrono::steady_clock::now() < deadline) {
    DWORD available = 0;
    if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) return {};
    if (available == 0) {
      std::this_thread::sleep_for(5ms);
      continue;
    }
    char bytes[4096]{};
    DWORD read = 0;
    const DWORD capacity =
        (std::min)(available, static_cast<DWORD>(sizeof(bytes)));
    if (!ReadFile(pipe, bytes, capacity, &read, nullptr) || read == 0) return {};
    const auto result = framer.append(std::string_view(bytes, read));
    if (!result || result.lines.empty()) continue;
    return result.lines.front();
  }
  return {};
}

TEST(NamedPipeServerTest, StaleResponseDoesNotCrossIntoNewConnection) {
  const std::wstring pipeName =
      L"\\\\.\\pipe\\mostorm-canvas-generation-test-" +
      std::to_wstring(GetCurrentProcessId()) + L"-" +
      std::to_wstring(GetTickCount64());
  std::mutex observedMutex;
  std::condition_variable observedCondition;
  NamedPipeServer::ConnectionId commandA =
      NamedPipeServer::kInvalidConnectionId;
  NamedPipeServer::ConnectionId helloB =
      NamedPipeServer::kInvalidConnectionId;
  std::string error;
  // Stop and join the handler thread before destroying its captured state,
  // including when a fatal ASSERT returns from this test early.
  NamedPipeServer server;
  ASSERT_TRUE(server.start(
      pipeName, "secret",
      [&](const ipc::Message& message, NamedPipeServer::ConnectionId id) {
        std::lock_guard<std::mutex> lock(observedMutex);
        if (message.requestId == "command-a") commandA = id;
        if (message.requestId == "hello-b") helloB = id;
        observedCondition.notify_all();
      },
      error))
      << error;

  NamedPipeServer::ConnectionId staleId =
      NamedPipeServer::kInvalidConnectionId;
  {
    PipeHandle clientA = connectClient(pipeName);
    ASSERT_NE(clientA.get(), INVALID_HANDLE_VALUE);
    ASSERT_TRUE(writeLine(
        clientA.get(),
        envelope("hello", "hello-a", {{"token", "secret"}}) +
            envelope("set-mode", "command-a", {{"mode", "draw"}})));
    std::unique_lock<std::mutex> lock(observedMutex);
    ASSERT_TRUE(observedCondition.wait_for(lock, 5s, [&] {
      return commandA != NamedPipeServer::kInvalidConnectionId;
    }));
    staleId = commandA;
  }

  const auto disconnectDeadline = std::chrono::steady_clock::now() + 5s;
  while (server.isCurrentConnection(staleId) &&
         std::chrono::steady_clock::now() < disconnectDeadline) {
    std::this_thread::sleep_for(5ms);
  }
  ASSERT_FALSE(server.isCurrentConnection(staleId));

  PipeHandle clientB = connectClient(pipeName);
  ASSERT_NE(clientB.get(), INVALID_HANDLE_VALUE);
  ASSERT_TRUE(writeLine(clientB.get(),
                        envelope("hello", "hello-b", {{"token", "secret"}})));
  NamedPipeServer::ConnectionId currentId =
      NamedPipeServer::kInvalidConnectionId;
  {
    std::unique_lock<std::mutex> lock(observedMutex);
    ASSERT_TRUE(observedCondition.wait_for(lock, 5s, [&] {
      return helloB != NamedPipeServer::kInvalidConnectionId;
    }));
    currentId = helloB;
  }
  ASSERT_NE(currentId, staleId);
  ASSERT_TRUE(server.isCurrentConnection(currentId));

  server.send(ipc::Message{1, "response", "command-a",
                           nlohmann::json{{"accepted", true}}},
              staleId);
  server.send(ipc::Message{1, "response", "command-b",
                           nlohmann::json{{"accepted", true}}},
              currentId);

  const std::string received = readLine(clientB.get());
  ASSERT_FALSE(received.empty());
  const auto decoded = ipc::decode(received);
  ASSERT_TRUE(decoded.message.has_value()) << decoded.error;
  EXPECT_EQ(decoded.message->requestId, "command-b");
}

}  // namespace
}  // namespace canvas::windows
