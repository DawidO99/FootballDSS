#pragma once

#include "core/domain/DateTypes.hpp"

#include <QCoreApplication>
#include <QString>

#include <optional>

namespace fdss::viewmodels {

// Dates between the viewmodels and QML are ISO strings (AGENTS section 6). The helpers of the data
// layer are out of reach here, so this is the one place every viewmodel converts them.
class IsoDates {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::IsoDates)

public:
    [[nodiscard]] static QString toIso(core::Date date);
    // Empty for no date.
    [[nodiscard]] static QString toIso(const std::optional<core::Date>& date);
    // nullopt for anything but a real date in the form yyyy-MM-dd.
    [[nodiscard]] static std::optional<core::Date> parse(const QString& iso);
    // In the format of the app language, the same as DateField. Empty for no date. Follows the
    // language on the next call.
    [[nodiscard]] static QString displayText(const std::optional<core::Date>& date);
};

} // namespace fdss::viewmodels
