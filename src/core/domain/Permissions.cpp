#include "core/domain/Permissions.hpp"

#include <format>

namespace fdss::core {

bool isAllowed(Role role, Permission permission) noexcept {
    // Lists who may, not who may not, so a role added later starts without rights.
    switch (permission) {
    case Permission::ManageStaff:
    case Permission::ManageSettings:
        return role == Role::Coach;
    case Permission::EditSquad:
    case Permission::EditCalendar:
    case Permission::EditLineup:
    case Permission::RunMatchDay:
        return role == Role::Coach || role == Role::Assistant;
    case Permission::EditAvailability:
    case Permission::EditTraining:
        return role == Role::Coach || role == Role::Assistant || role == Role::Physio;
    case Permission::ViewAvailabilityDetails:
        return role == Role::Coach || role == Role::Physio;
    case Permission::Import:
    case Permission::ManageMetrics:
        return role == Role::Coach || role == Role::Assistant || role == Role::Analyst;
    case Permission::ViewAnalysis:
        return role == Role::Coach || role == Role::Assistant || role == Role::Physio ||
               role == Role::Analyst;
    }
    return false;
}

std::string_view toString(Permission permission) noexcept {
    switch (permission) {
    case Permission::ManageStaff:
        return "ManageStaff";
    case Permission::ManageSettings:
        return "ManageSettings";
    case Permission::EditSquad:
        return "EditSquad";
    case Permission::EditAvailability:
        return "EditAvailability";
    case Permission::ViewAvailabilityDetails:
        return "ViewAvailabilityDetails";
    case Permission::EditCalendar:
        return "EditCalendar";
    case Permission::EditTraining:
        return "EditTraining";
    case Permission::EditLineup:
        return "EditLineup";
    case Permission::RunMatchDay:
        return "RunMatchDay";
    case Permission::Import:
        return "Import";
    case Permission::ManageMetrics:
        return "ManageMetrics";
    case Permission::ViewAnalysis:
        return "ViewAnalysis";
    }
    return {};
}

Result<Permission> permissionFromName(std::string_view name) {
    for (const Permission permission : allValues<Permission>()) {
        if (toString(permission) == name) {
            return permission;
        }
    }
    return std::unexpected(
        Error{ErrorCode::InvalidInput, std::format("unknown permission '{}'", name), {}});
}

} // namespace fdss::core
