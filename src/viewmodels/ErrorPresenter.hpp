#pragma once

#include "core/util/Error.hpp"

#include <QCoreApplication>
#include <QString>

#include <string>

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

private:
    [[nodiscard]] static QString invalidInputMessage(const std::string& field);
    [[nodiscard]] static QString conflictMessage(const std::string& field);
};

} // namespace fdss::viewmodels
