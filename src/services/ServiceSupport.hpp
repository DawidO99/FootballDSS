#pragma once

#include "core/domain/Validation.hpp"
#include "core/util/Error.hpp"

#include <format>

namespace fdss::services {

// The first finding of a core::validate() as a Status. Repositories do not validate, so a service
// runs this before every create() and update().
[[nodiscard]] inline core::Status firstError(const core::ValidationErrors& errors) {
    if (errors.empty()) {
        return {};
    }
    const core::ValidationError& error = errors.front();
    return std::unexpected(
        core::Error{error.code, std::format("invalid value of '{}'", error.field), error.field});
}

} // namespace fdss::services
