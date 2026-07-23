#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace canvas::windows::detail {

class WebView2MessageLog final {
 public:
  static constexpr std::size_t maxSize() noexcept { return 256U; }

  void push(std::wstring message) {
    if (values_.size() >= maxSize()) {
      values_.erase(
          values_.begin(),
          values_.begin() + static_cast<std::ptrdiff_t>(maxSize() / 2U));
    }
    values_.push_back(std::move(message));
  }

  const std::vector<std::wstring>& values() const noexcept { return values_; }

 private:
  std::vector<std::wstring> values_;
};

}  // namespace canvas::windows::detail
