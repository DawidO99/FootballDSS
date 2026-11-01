#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace fdss::core {

enum class ErrorCode {
    NotFound,
    InvalidInput,
    Conflict,
    PermissionDenied,
    Database,
    DatabaseLocked,
    NotADatabase,
    IncompatibleVersion,
    Io,
    UnsupportedFormat,
    Internal
};

// Core, data and services never produce user-visible text; viewmodels turn code + field into a
// translated message.
struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string detail; // technical, English, for logs only
    std::string field;  // optional: input field / column the error refers to
};

template <class T> using Result = std::expected<T, Error>;
using Status = Result<void>;

// Name of the code, for logs and tests.
[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

} // namespace fdss::core
