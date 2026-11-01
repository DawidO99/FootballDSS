#pragma once

#include "core/domain/Enums.hpp"
#include "core/util/Error.hpp"

#include <array>
#include <string_view>

// The role matrix of SPEC section 5.

namespace fdss::core {

enum class Permission {
    ManageStaff,
    ManageSettings,
    EditSquad,
    EditAvailability,
    ViewAvailabilityDetails,
    EditCalendar,
    EditTraining,
    EditLineup,
    RunMatchDay,
    Import,
    ManageMetrics,
    ViewAnalysis
};

template <> struct EnumValues<Permission> {
    static constexpr std::array values{Permission::ManageStaff,
                                       Permission::ManageSettings,
                                       Permission::EditSquad,
                                       Permission::EditAvailability,
                                       Permission::ViewAvailabilityDetails,
                                       Permission::EditCalendar,
                                       Permission::EditTraining,
                                       Permission::EditLineup,
                                       Permission::RunMatchDay,
                                       Permission::Import,
                                       Permission::ManageMetrics,
                                       Permission::ViewAnalysis};
};

[[nodiscard]] bool isAllowed(Role role, Permission permission) noexcept;

// The name of the enumerator, e.g. "EditSquad". QML names a permission this way.
[[nodiscard]] std::string_view toString(Permission permission) noexcept;
[[nodiscard]] Result<Permission> permissionFromName(std::string_view name);

} // namespace fdss::core
