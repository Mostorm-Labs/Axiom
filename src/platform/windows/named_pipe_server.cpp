#include "platform/windows/named_pipe_server.h"

#include "canvas/ipc/client_session.h"

#include <windows.h>
#include <sddl.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace canvas::windows {
namespace {

constexpr std::size_t kMaximumQueuedWrites = 256U;
constexpr std::size_t kMaximumQueuedWriteBytes = 4U * 1024U * 1024U;

std::string win32Error(DWORD error) {
  return "Win32 error " + std::to_string(error);
}

bool currentUserSecurityAttributes(SECURITY_ATTRIBUTES& attributes,
                                   PSECURITY_DESCRIPTOR& descriptor,
                                   std::string& error) {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    error = "could not query IPC process token: " + win32Error(GetLastError());
    return false;
  }
  DWORD size = 0;
  (void)GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    error = "could not size IPC user token: " + win32Error(GetLastError());
    CloseHandle(token);
    return false;
  }
  std::vector<BYTE> tokenUser(size);
  if (!GetTokenInformation(token, TokenUser, tokenUser.data(), size, &size)) {
    error = "could not read IPC user token: " + win32Error(GetLastError());
    CloseHandle(token);
    return false;
  }
  CloseHandle(token);
  const auto* user = reinterpret_cast<const TOKEN_USER*>(tokenUser.data());
  LPWSTR sidText = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &sidText)) {
    error = "could not format IPC user SID: " + win32Error(GetLastError());
    return false;
  }
  const std::wstring sddl = L"D:P(A;;GA;;;SY)(A;;GA;;;" +
                           std::wstring(sidText) + L")";
  LocalFree(sidText);
  if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
          sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
    error = "could not create IPC access control list: " +
            win32Error(GetLastError());
    return false;
  }
  attributes.nLength = sizeof(attributes);
  attributes.lpSecurityDescriptor = descriptor;
  attributes.bInheritHandle = FALSE;
  return true;
}

}  // namespace

class NamedPipeServer::Impl {
 public:
  bool start(std::wstring pipeName, std::string sessionToken,
             MessageHandler handler, std::string& error) {
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    if (pipeName.empty() || sessionToken.empty() || !handler) {
      error = "IPC pipe name, session token, and handler are required";
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      if (running_) {
        error = "IPC server is already running";
        return false;
      }
    }

    SECURITY_ATTRIBUTES security{};
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!currentUserSecurityAttributes(security, descriptor, error)) return false;
    // Creating the sole instance here (rather than in the worker) makes a
    // duplicate server or ACL failure observable by start().  Reusing this
    // handle across DisconnectNamedPipe calls preserves first-instance
    // protection for the server lifetime.
    const HANDLE pipe = CreateNamedPipeW(
        pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                              FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
            PIPE_REJECT_REMOTE_CLIENTS,
        1, static_cast<DWORD>(ipc::LineFramer::kMaximumLineBytes),
        static_cast<DWORD>(ipc::LineFramer::kMaximumLineBytes), 0, &security);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE) {
      error = "could not create IPC named pipe: " + win32Error(GetLastError());
      return false;
    }

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr) {
      error = "could not create IPC stop event: " + win32Error(GetLastError());
      CloseHandle(pipe);
      return false;
    }
    HANDLE readCompletion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE writeCompletion = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (readCompletion == nullptr || writeCompletion == nullptr) {
      error = "could not create IPC completion event: " +
              win32Error(GetLastError());
      if (readCompletion != nullptr) CloseHandle(readCompletion);
      if (writeCompletion != nullptr) CloseHandle(writeCompletion);
      CloseHandle(stopEvent);
      CloseHandle(pipe);
      return false;
    }
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      pipe_ = pipe;
      stopEvent_ = stopEvent;
      readCompletion_ = readCompletion;
      writeCompletion_ = writeCompletion;
      pipeName_ = std::move(pipeName);
      sessionToken_ = std::move(sessionToken);
      handler_ = std::move(handler);
      stopRequested_.store(false);
      running_ = true;
    }
    try {
      writer_ = std::thread(&Impl::writeLoop, this);
      reader_ = std::thread(&Impl::readLoop, this);
    } catch (const std::system_error& exception) {
      error = exception.what();
      stopLocked();
      return false;
    }
    return true;
  }

  void send(const ipc::Message& message, ConnectionId connectionId) {
    // This is intentionally only a bounded in-memory enqueue.  In particular,
    // it never waits on a pipe write while called from the UI thread.
    if (connectionId == kInvalidConnectionId) return;
    auto encoded = ipc::tryEncodeNativeEvent(
        message, ipc::LineFramer::kMaximumLineBytes);
    if (!encoded.encoded) return;
    std::string line = std::move(*encoded.encoded) + "\n";
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!running_ || !authenticated_ || stopRequested_.load() ||
        connectionId != connectionGeneration_ ||
        outbound_.size() >= kMaximumQueuedWrites ||
        queuedWriteBytes_ + line.size() > kMaximumQueuedWriteBytes) {
      return;
    }
    queuedWriteBytes_ += line.size();
    outbound_.push_back(
        QueuedWrite{connectionId, std::move(line)});
    writeCondition_.notify_one();
  }

  bool isCurrentConnection(ConnectionId connectionId) const {
    if (connectionId == kInvalidConnectionId) return false;
    std::lock_guard<std::mutex> lock(stateMutex_);
    return running_ && authenticated_ && !stopRequested_.load() &&
           connectionId == connectionGeneration_;
  }

  void stop() {
    if (isReaderThread()) {
      // A handler executes on reader_. Joining from that callback would
      // deadlock. Request cancellation now; destruction or a stop() call from
      // any other thread performs the joins and closes the handles.
      requestStop();
      return;
    }
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
    stopLocked();
  }

  bool isReaderThread() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return readerThreadId_ == std::this_thread::get_id();
  }

  void stopForDestruction() noexcept {
    // Destroying the server from inside its own handler cannot be made safe:
    // the reader stack is still executing methods on this Impl. Make that API
    // contract explicit instead of reaching std::thread's implicit terminate
    // with a joinable member.
    if (isReaderThread()) {
      requestStop();
      std::terminate();
    }
    try {
      std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
      stopLocked();
    } catch (...) {
      // A destructor must never expose an exception or retain joinable thread
      // members. Join failures are unrecoverable because resources are live.
      std::terminate();
    }
  }

 private:
  void requestStop() {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE stopEvent = nullptr;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      if (!running_) return;
      if (!stopRequested_.exchange(true)) ++connectionGeneration_;
      authenticated_ = false;
      outbound_.clear();
      queuedWriteBytes_ = 0;
      pipe = pipe_;
      stopEvent = stopEvent_;
    }
    if (stopEvent != nullptr) SetEvent(stopEvent);
    writeCondition_.notify_all();
    if (pipe != INVALID_HANDLE_VALUE) (void)CancelIoEx(pipe, nullptr);
  }

  // lifecycleMutex_ must be held. This split lets start() roll back a partial
  // thread launch without recursively acquiring the lifecycle lock.
  void stopLocked() {
    requestStop();
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      if (!running_) return;
    }
    if (reader_.joinable()) reader_.join();
    if (writer_.joinable()) writer_.join();

    std::lock_guard<std::mutex> lock(stateMutex_);
    // Both I/O owners have exited, so this is the only close of the pipe.
    if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_);
    if (stopEvent_ != nullptr) CloseHandle(stopEvent_);
    if (readCompletion_ != nullptr) CloseHandle(readCompletion_);
    if (writeCompletion_ != nullptr) CloseHandle(writeCompletion_);
    pipe_ = INVALID_HANDLE_VALUE;
    stopEvent_ = nullptr;
    readCompletion_ = nullptr;
    writeCompletion_ = nullptr;
    running_ = false;
    handler_ = {};
    sessionToken_.clear();
    pipeName_.clear();
    readerThreadId_ = {};
  }
  bool waitForOverlapped(HANDLE pipe, OVERLAPPED& operation, DWORD& bytes) {
    const HANDLE events[]{operation.hEvent, stopEvent_};
    const DWORD waited = WaitForMultipleObjects(2, events, FALSE, INFINITE);
    if (waited == WAIT_OBJECT_0) {
      return GetOverlappedResult(pipe, &operation, &bytes, FALSE) != FALSE;
    }
    if (waited == WAIT_OBJECT_0 + 1U) {
      (void)CancelIoEx(pipe, &operation);
      (void)WaitForSingleObject(operation.hEvent, INFINITE);
    }
    return false;
  }

  bool connectClient(HANDLE pipe, HANDLE completion) {
    OVERLAPPED operation{};
    operation.hEvent = completion;
    ResetEvent(completion);
    if (ConnectNamedPipe(pipe, &operation)) return true;
    const DWORD connectError = GetLastError();
    if (connectError == ERROR_PIPE_CONNECTED) return true;
    if (connectError != ERROR_IO_PENDING) return false;
    DWORD ignored = 0;
    return waitForOverlapped(pipe, operation, ignored);
  }

  bool readChunk(HANDLE pipe, HANDLE completion, char* bytes, DWORD capacity,
                 DWORD& bytesRead) {
    OVERLAPPED operation{};
    operation.hEvent = completion;
    ResetEvent(completion);
    if (ReadFile(pipe, bytes, capacity, &bytesRead, &operation)) return bytesRead != 0;
    if (GetLastError() != ERROR_IO_PENDING) return false;
    return waitForOverlapped(pipe, operation, bytesRead) && bytesRead != 0;
  }

  bool writeLine(HANDLE pipe, HANDLE completion, const std::string& line) {
    OVERLAPPED operation{};
    operation.hEvent = completion;
    ResetEvent(completion);
    DWORD bytesWritten = 0;
    if (WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()),
                  &bytesWritten, &operation)) {
      return bytesWritten == static_cast<DWORD>(line.size());
    }
    if (GetLastError() != ERROR_IO_PENDING) return false;
    return waitForOverlapped(pipe, operation, bytesWritten) &&
           bytesWritten == static_cast<DWORD>(line.size());
  }

  void setAuthenticated() {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!stopRequested_.load()) {
      authenticated_ = true;
      ++connectionGeneration_;
      writeCondition_.notify_one();
    }
  }

  void closeClient(HANDLE pipe) {
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      authenticated_ = false;
      ++connectionGeneration_;
      outbound_.clear();
      queuedWriteBytes_ = 0;
    }
    // A dequeued write may still be outstanding.  It must finish/cancel
    // before DisconnectNamedPipe permits a new client on this same handle.
    (void)CancelIoEx(pipe, nullptr);
    {
      std::unique_lock<std::mutex> lock(stateMutex_);
      writerIdleCondition_.wait(lock, [this] { return !writerActive_; });
    }
    writeCondition_.notify_all();
    (void)DisconnectNamedPipe(pipe);
  }

  void readLoop() {
    HANDLE completion = nullptr;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      readerThreadId_ = std::this_thread::get_id();
      pipe = pipe_;
      completion = readCompletion_;
    }
    std::array<char, 4096> bytes{};
    while (!stopRequested_.load() && pipe != INVALID_HANDLE_VALUE) {
      if (!connectClient(pipe, completion)) {
        if (stopRequested_.load()) break;
        (void)DisconnectNamedPipe(pipe);
        continue;
      }
      ipc::LineFramer framer;
      ipc::ClientSession session(sessionToken_);
      bool rejectClient = false;
      while (!stopRequested_.load() && !rejectClient) {
        DWORD bytesRead = 0;
        if (!readChunk(pipe, completion, bytes.data(),
                       static_cast<DWORD>(bytes.size()), bytesRead)) {
          break;
        }
        const auto framed = framer.append(std::string_view(bytes.data(), bytesRead));
        if (!framed) break;
        for (const auto& line : framed.lines) {
          const auto accepted = session.accept(line);
          if (!accepted) {
            rejectClient = true;
            break;
          }
          if (accepted.message->type == "hello") setAuthenticated();
          MessageHandler handler;
          ConnectionId connectionId = kInvalidConnectionId;
          {
            std::lock_guard<std::mutex> lock(stateMutex_);
            handler = handler_;
            if (authenticated_) connectionId = connectionGeneration_;
          }
          try {
            if (handler && connectionId != kInvalidConnectionId) {
              handler(*accepted.message, connectionId);
            }
          } catch (...) {
            rejectClient = true;
            break;
          }
        }
      }
      closeClient(pipe);
    }
    std::lock_guard<std::mutex> lock(stateMutex_);
    readerThreadId_ = {};
  }

  void writeLoop() {
    for (;;) {
      std::string line;
      HANDLE pipe = INVALID_HANDLE_VALUE;
      HANDLE completion = nullptr;
      {
        std::unique_lock<std::mutex> lock(stateMutex_);
        writeCondition_.wait(lock, [this] {
          return stopRequested_.load() || (authenticated_ && !outbound_.empty());
        });
        if (stopRequested_.load()) break;
        QueuedWrite queued = std::move(outbound_.front());
        queuedWriteBytes_ -= queued.line.size();
        outbound_.pop_front();
        // closeClient advances the generation while holding this same lock.
        // Thus it either observes writerActive_ below and cancels it, or the
        // stale queued line is discarded before it can start I/O.
        if (!authenticated_ || queued.generation != connectionGeneration_) continue;
        line = std::move(queued.line);
        pipe = pipe_;
        completion = writeCompletion_;
        writerActive_ = true;
      }
      (void)writeLine(pipe, completion, line);
      {
        std::lock_guard<std::mutex> lock(stateMutex_);
        writerActive_ = false;
      }
      writerIdleCondition_.notify_all();
    }
    writerIdleCondition_.notify_all();
  }

  mutable std::mutex stateMutex_;
  std::mutex lifecycleMutex_;
  std::condition_variable writeCondition_;
  std::condition_variable writerIdleCondition_;
  std::thread reader_;
  std::thread writer_;
  std::thread::id readerThreadId_{};
  std::atomic<bool> stopRequested_{false};
  HANDLE pipe_ = INVALID_HANDLE_VALUE;
  HANDLE stopEvent_ = nullptr;
  HANDLE readCompletion_ = nullptr;
  HANDLE writeCompletion_ = nullptr;
  bool running_ = false;
  bool authenticated_ = false;
  bool writerActive_ = false;
  ConnectionId connectionGeneration_ = kInvalidConnectionId;
  std::wstring pipeName_;
  std::string sessionToken_;
  MessageHandler handler_;
  struct QueuedWrite {
    ConnectionId generation;
    std::string line;
  };
  std::deque<QueuedWrite> outbound_;
  std::size_t queuedWriteBytes_ = 0;
};

NamedPipeServer::NamedPipeServer() : impl_(std::make_unique<Impl>()) {}
NamedPipeServer::~NamedPipeServer() { impl_->stopForDestruction(); }

bool NamedPipeServer::start(std::wstring pipeName, std::string sessionToken,
                            MessageHandler handler, std::string& error) {
  return impl_->start(std::move(pipeName), std::move(sessionToken),
                      std::move(handler), error);
}

void NamedPipeServer::send(const ipc::Message& message,
                           ConnectionId connectionId) {
  impl_->send(message, connectionId);
}
bool NamedPipeServer::isCurrentConnection(ConnectionId connectionId) const {
  return impl_->isCurrentConnection(connectionId);
}
void NamedPipeServer::stop() { impl_->stop(); }

}  // namespace canvas::windows
