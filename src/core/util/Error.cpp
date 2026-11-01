#include "core/util/Error.hpp"

namespace fdss::core {

std::string_view toString(ErrorCode code) noexcept {
    // No default: the compiler reports a code added to the enum but not here.
    switch (code) {
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::InvalidInput:
        return "InvalidInput";
    case ErrorCode::Conflict:
        return "Conflict";
    case ErrorCode::PermissionDenied:
        return "PermissionDenied";
    case ErrorCode::Database:
        return "Database";
    case ErrorCode::DatabaseLocked:
        return "DatabaseLocked";
    case ErrorCode::NotADatabase:
        return "NotADatabase";
    case ErrorCode::IncompatibleVersion:
        return "IncompatibleVersion";
    case ErrorCode::Io:
        return "Io";
    case ErrorCode::UnsupportedFormat:
        return "UnsupportedFormat";
    case ErrorCode::Internal:
        return "Internal";
    }
    return "Unknown";
}

} // namespace fdss::core
