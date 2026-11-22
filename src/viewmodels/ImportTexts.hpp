#pragma once

#include "core/parsing/SquadImport.hpp"
#include "core/parsing/TextEncoding.hpp"

#include <QCoreApplication>
#include <QString>
#include <QVariantList>

namespace fdss::viewmodels {

// What the import wizard shows for the core types (packet P16). Every text follows the language on
// the next call, so the viewmodel asks again on QEvent::LanguageChange.
class ImportTexts {
    Q_DECLARE_TR_FUNCTIONS(fdss::viewmodels::ImportTexts)

public:
    // "First name", "Shirt number"… for the mapping choices.
    [[nodiscard]] static QString fieldName(core::SquadField field);
    // [{ value, text }]: "" = ignore the column, then every field key.
    [[nodiscard]] static QVariantList fieldOptions();
    // [{ value, text }] of the order of a full-name column.
    [[nodiscard]] static QVariantList nameOrderOptions();
    [[nodiscard]] static QString encodingName(core::TextEncoding encoding);
    [[nodiscard]] static QString delimiterName(char delimiter);
    [[nodiscard]] static QString mappingIssue(core::MappingIssue issue);

    // One issue, e.g. "shirt number 'abc', expected a number".
    [[nodiscard]] static QString issue(const core::RowIssue& issue, int currentYear);
    // The warning of a new player's position, in every import (P19 decision 7): set by the
    // default, or not given ("—").
    [[nodiscard]] static QString positionWarning(bool positionGiven);
    // All issues of a row, joined; empty for a row without any.
    [[nodiscard]] static QString details(const core::SquadRow& row, int currentYear);
    // "Row 14: shirt number 'abc', expected a number."; empty for a row without issues.
    [[nodiscard]] static QString rowMessage(const core::SquadRow& row, int currentYear);
    [[nodiscard]] static QString statusName(core::RowStatus status);
    // "ok", "warning" or "error".
    [[nodiscard]] static QString statusKey(core::RowStatus status);
    [[nodiscard]] static QString actionName(core::RowAction action);
};

} // namespace fdss::viewmodels
