#include "canvas/app/embedded_load_completion_inbox.h"

#include <limits>

namespace canvas::app {

EmbeddedLoadCompletionInbox::EnqueueResult EmbeddedLoadCompletionInbox::enqueue(
    Event event) noexcept {
  if (!isValid(event)) return {EnqueueStatus::Invalid, false};
  if (full()) {
    if (overflowCount_ != (std::numeric_limits<std::uint64_t>::max)()) ++overflowCount_;
    return {EnqueueStatus::Full, requestNotificationIfNeeded()};
  }
  events_[physicalIndex(size_)] = event;
  ++size_;
  return {EnqueueStatus::Accepted, requestNotificationIfNeeded()};
}

bool EmbeddedLoadCompletionInbox::pop(Event& event) noexcept {
  if (empty()) return false;
  event = events_[head_];
  head_ = (head_ + 1U) % capacity;
  --size_;
  if (empty()) head_ = 0U;
  return true;
}

std::size_t EmbeddedLoadCompletionInbox::cancelGeneration(
    DocumentGeneration generation) noexcept {
  if (generation == 0U || empty()) return 0U;
  const std::size_t previousSize = size_;
  std::size_t kept = 0U;
  for (std::size_t read = 0U; read < previousSize; ++read) {
    const Event event = events_[physicalIndex(read)];
    if (event.documentGeneration == generation) continue;
    events_[physicalIndex(kept++)] = event;
  }
  size_ = kept;
  if (empty()) head_ = 0U;
  return previousSize - kept;
}

std::size_t EmbeddedLoadCompletionInbox::clear() noexcept {
  const auto removed = size_;
  head_ = 0U;
  size_ = 0U;
  return removed;
}

bool EmbeddedLoadCompletionInbox::consumeNotification() noexcept {
  if (!notificationPending_) return false;
  notificationPending_ = false;
  return true;
}
bool EmbeddedLoadCompletionInbox::notificationPostFailed() noexcept {
  return consumeNotification();
}
bool EmbeddedLoadCompletionInbox::requestNotificationIfNeeded() noexcept {
  if (empty() || notificationPending_) return false;
  notificationPending_ = true;
  return true;
}
bool EmbeddedLoadCompletionInbox::isValid(Event event) noexcept {
  if (event.token == 0U || event.documentGeneration == 0U) return false;
  if (event.outcome == Outcome::Ready) return event.failureCode == 0;
  if (event.outcome == Outcome::Failed) return event.failureCode < 0;
  return false;
}
std::size_t EmbeddedLoadCompletionInbox::physicalIndex(std::size_t offset) const noexcept {
  return (head_ + offset) % capacity;
}

}  // namespace canvas::app
