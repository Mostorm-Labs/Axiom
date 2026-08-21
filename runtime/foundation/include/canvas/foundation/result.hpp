#pragma once

#include <optional>
#include <string>
#include <utility>

namespace canvas::foundation {

enum class ErrorCode {
    kInvalidArgument,
    kInvalidRevision,
    kInvalidRecord,
    kBeforeImageMismatch,
    kDuplicateObject,
    kMissingObject,
    kInvalidReference,
    kParticipantRejected,
    kRequiresFullRebuild,
    kOutOfMemory,
    kInternalError,
};

struct Error {
    ErrorCode code = ErrorCode::kInternalError;
    std::string message;
};

template <typename TValue> class Result final {
  public:
    static Result success(TValue value) {
        return Result(std::move(value), std::nullopt);
    }

    static Result failure(Error error) {
        return Result(std::nullopt, std::move(error));
    }

    [[nodiscard]] bool hasValue() const {
        return _value.has_value();
    }
    [[nodiscard]] explicit operator bool() const {
        return hasValue();
    }

    [[nodiscard]] const TValue& value() const {
        return *_value;
    }
    [[nodiscard]] TValue& value() {
        return *_value;
    }
    [[nodiscard]] const Error& error() const {
        return *_error;
    }

  private:
    Result(std::optional<TValue> value, std::optional<Error> error)
        : _value(std::move(value)), _error(std::move(error)) {}

    std::optional<TValue> _value;
    std::optional<Error> _error;
};

} // namespace canvas::foundation
