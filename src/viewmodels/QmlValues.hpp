#pragma once

#include "core/domain/Enums.hpp"

#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

namespace fdss::viewmodels {

// [{ value, text }] for every value of E, in the order of core::allValues(): the database text and
// what textOf() makes of it. The form a SelectField or a SegmentedControl takes.
template <class E, class TextOf> [[nodiscard]] QVariantList optionsOf(TextOf textOf) {
    QVariantList options;
    for (const E value : core::allValues<E>()) {
        options.append(
            QVariantMap{{QStringLiteral("value"), QString::fromUtf8(core::toDbText(value))},
                        {QStringLiteral("text"), textOf(value)}});
    }
    return options;
}

// JavaScript sends null, undefined or a number, whose exact type does not matter here.
[[nodiscard]] inline QVariant numberOrNull(const QVariant& value) {
    return value.isValid() && !value.isNull() ? QVariant(value.toInt()) : QVariant{};
}

} // namespace fdss::viewmodels
