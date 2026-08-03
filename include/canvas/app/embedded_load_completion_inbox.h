#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace canvas::app {

class EmbeddedLoadCompletionInbox final {
 public:
  using Token = std::uint64_t;
  using DocumentGeneration = std::uint64_t;
  static constexpr std::size_t capacity = 256U;
  enum class Outcome : std::uint8_t { Ready, Failed };
  enum class EnqueueStatus : std::uint8_t { Accepted, Invalid, Full };
  struct Event {
    Token token = 0U;
    DocumentGeneration documentGeneration = 0U;
    Outcome outcome = Outcome::Ready;
    std::int32_t failureCode = 0;
  };
  struct EnqueueResult {
    EnqueueStatus status = EnqueueStatus::Invalid;
    bool shouldPostNotification = false;
  };

  EmbeddedLoadCompletionInbox() noexcept = default;
  EmbeddedLoadCompletionInbox(const EmbeddedLoadCompletionInbox&) = delete;
  EmbeddedLoadCompletionInbox& operator=(const EmbeddedLoadCompletionInbox&) = delete;
  EmbeddedLoadCompletionInbox(EmbeddedLoadCompletionInbox&&) = delete;
  EmbeddedLoadCompletionInbox& operator=(EmbeddedLoadCompletionInbox&&) = delete;
  [[nodiscard]] EnqueueResult enqueue(Event event) noexcept;
  [[nodiscard]] bool pop(Event& event) noexcept;
  [[nodiscard]] std::size_t cancelGeneration(DocumentGeneration generation) noexcept;
  [[nodiscard]] std::size_t clear() noexcept;
  [[nodiscard]] bool consumeNotification() noexcept;
  [[nodiscard]] bool notificationPostFailed() noexcept;
  [[nodiscard]] bool requestNotificationIfNeeded() noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
  [[nodiscard]] bool full() const noexcept { return size_ == capacity; }
  [[nodiscard]] bool notificationPending() const noexcept { return notificationPending_; }
  [[nodiscard]] std::uint64_t overflowCount() const noexcept { return overflowCount_; }

 private:
  [[nodiscard]] static bool isValid(Event event) noexcept;
  [[nodiscard]] std::size_t physicalIndex(std::size_t offset) const noexcept;
  std::array<Event, capacity> events_{};
  std::size_t head_ = 0U;
  std::size_t size_ = 0U;
  std::uint64_t overflowCount_ = 0U;
  bool notificationPending_ = false;
};

}  // namespace canvas::app
