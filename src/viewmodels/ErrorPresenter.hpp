#pragma once

#include "core/util/Error.hpp"

#include <QCoreApplication>
#include <QString>

#include <string>

namespace fdss::services {
struct PinCheck;
} // namespace fdss::services

namespace fdss::viewmodels {

// The one place that turns an Error into text for the user (AGENTS section 5). Never shows
// Error::detail, nor a column name. A packet that adds a field the user can get wrong adds its text
// here.
class ErrorPresenter {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::ErrorPresenter)

public:
    // What went wrong while the app was working, with a next step.
    [[nodiscard]] static QString message(const core::Error& error);
    // Why the database could not be opened at startup, with a next step. Separate from message():
    // these texts speak of opening the file and of restarting, which would mislead after a failed
    // save.
    [[nodiscard]] static QString startupMessage(core::ErrorCode code);
    // What the user got wrong in one input, for a viewmodel that checks it before calling a
    // service.
    [[nodiscard]] static QString invalidInputMessage(const std::string& field);
    // A wrong PIN and a lockout are answers, not errors, but the user reads them the same way.
    // Empty for an accepted PIN.
    [[nodiscard]] static QString pinCheckMessage(const services::PinCheck& check);

private:
    [[nodiscard]] static QString conflictMessage(const std::string& field);
};

} // namespace fdss::viewmodels
