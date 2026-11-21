#include "viewmodels/ImportTexts.hpp"

#include "core/domain/Validation.hpp"

#include <QStringList>
#include <QVariantMap>

namespace fdss::viewmodels {

namespace {

QString fromStd(const std::string& text) { return QString::fromStdString(text); }

QVariantMap option(const QString& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

QString rowList(const std::vector<int>& rows) {
    QStringList numbers;
    numbers.reserve(static_cast<qsizetype>(rows.size()));
    for (const int row : rows) {
        numbers.append(QString::number(row));
    }
    return numbers.join(QStringLiteral(", "));
}

QString playerList(const std::vector<core::PlayerRef>& players) {
    QStringList names;
    for (const core::PlayerRef& player : players) {
        QString name = fromStd(player.firstName) + QLatin1Char(' ') + fromStd(player.lastName);
        if (player.shirtNumber) {
            name += QStringLiteral(" (%1)").arg(*player.shirtNumber);
        }
        names.append(name);
    }
    return names.join(QStringLiteral(", "));
}

} // namespace

QString ImportTexts::fieldName(core::SquadField field) {
    switch (field) {
    case core::SquadField::FirstName:
        return tr("First name");
    case core::SquadField::LastName:
        return tr("Last name");
    case core::SquadField::FullName:
        return tr("First and last name");
    case core::SquadField::ShirtNumber:
        return tr("Shirt number");
    case core::SquadField::BirthYear:
        return tr("Birth year");
    case core::SquadField::PrimaryPosition:
        return tr("Position");
    case core::SquadField::SecondaryPositions:
        return tr("Secondary positions");
    case core::SquadField::Foot:
        return tr("Foot");
    }
    return {};
}

QVariantList ImportTexts::fieldOptions() {
    QVariantList options{option(QString(), tr("Ignore column"))};
    for (const core::SquadField field : core::kSquadFields) {
        options.append(option(QString::fromUtf8(core::toKey(field)), fieldName(field)));
    }
    return options;
}

QVariantList ImportTexts::nameOrderOptions() {
    return {option(QString::fromUtf8(core::toKey(core::NameOrder::FirstLast)),
                   tr("First name Last name")),
            option(QString::fromUtf8(core::toKey(core::NameOrder::LastFirst)),
                   tr("Last name First name"))};
}

QString ImportTexts::encodingName(core::TextEncoding encoding) {
    switch (encoding) {
    case core::TextEncoding::Utf8:
        return tr("UTF-8");
    case core::TextEncoding::Utf8Bom:
        return tr("UTF-8 with BOM");
    case core::TextEncoding::Utf16:
        return tr("UTF-16 (Unicode text)");
    case core::TextEncoding::Cp1250:
        return tr("Windows-1250 (Polish Excel)");
    }
    return {};
}

QString ImportTexts::delimiterName(char delimiter) {
    switch (delimiter) {
    case ';':
        return tr("semicolon");
    case ',':
        return tr("comma");
    case '\t':
        return tr("tab");
    default:
        return QString(QLatin1Char(delimiter));
    }
}

QString ImportTexts::mappingIssue(core::MappingIssue issue) {
    switch (issue) {
    case core::MappingIssue::MissingName:
        return tr("Choose the columns with the first and the last name, or one column with both.");
    case core::MappingIssue::FullNameWithParts:
        return tr("Use either one column with the first and last name, or separate first-name and "
                  "last-name columns, not both.");
    case core::MappingIssue::DuplicateField:
        return tr("Two columns have the same meaning. Set one of them to \"Ignore column\".");
    case core::MappingIssue::MissingDefaultPosition:
        return tr("Some players have no position in the file. Choose the default position.");
    }
    return {};
}

QString ImportTexts::issue(const core::RowIssue& issue, int currentYear) {
    const QString value = fromStd(issue.value);
    const bool number = issue.field == core::SquadField::ShirtNumber;
    switch (issue.kind) {
    case core::RowIssueKind::MissingValue:
        switch (issue.field) {
        case core::SquadField::FirstName:
            return tr("the first name is empty");
        case core::SquadField::LastName:
            return tr("the last name is empty");
        case core::SquadField::PrimaryPosition:
            return tr("the position is empty, choose a default position");
        default:
            return tr("the name is empty");
        }
    case core::RowIssueKind::SingleWordName:
        return tr("name '%1' has one word, expected the first and the last name").arg(value);
    case core::RowIssueKind::TooLong:
        return tr("name '%1' is longer than 50 characters").arg(value);
    case core::RowIssueKind::NotANumber:
        return number ? tr("shirt number '%1', expected a number").arg(value)
                      : tr("birth year '%1', expected a year such as 2001 or a date").arg(value);
    case core::RowIssueKind::OutOfRange:
        return number ? tr("shirt number '%1', expected a number from %2 to %3")
                            .arg(value)
                            .arg(core::kMinShirtNumber)
                            .arg(core::kMaxShirtNumber)
                      : tr("birth year '%1', expected a year from %2 to %3")
                            .arg(value)
                            .arg(core::kMinBirthYear)
                            .arg(currentYear - core::kYoungestPlayerAge);
    case core::RowIssueKind::UnknownPosition:
        if (issue.field == core::SquadField::SecondaryPositions) {
            return tr("secondary position '%1' is unknown and was left out").arg(value);
        }
        return tr("position '%1' is unknown, expected a code such as BR, ŚO, N or GK, CB, ST")
            .arg(value);
    case core::RowIssueKind::DefaultPosition:
        return tr("position set by default — correct it in Squad");
    case core::RowIssueKind::UnknownFoot:
        return tr("foot '%1' is unknown, saved as unknown").arg(value);
    case core::RowIssueKind::DuplicateInFile:
        return number ? tr("the same shirt number is in row %1").arg(rowList(issue.otherRows))
                      : tr("the same player is in row %1").arg(rowList(issue.otherRows));
    case core::RowIssueKind::AmbiguousMatch:
        if (number && issue.candidates.size() == 1) {
            return tr("the shirt number belongs to %1, skipped").arg(playerList(issue.candidates));
        }
        return tr("fits several players (%1), skipped").arg(playerList(issue.candidates));
    }
    return {};
}

QString ImportTexts::details(const core::SquadRow& row, int currentYear) {
    QStringList parts;
    for (const core::RowIssue& rowIssue : row.issues) {
        parts.append(issue(rowIssue, currentYear));
    }
    return parts.join(QStringLiteral("; "));
}

QString ImportTexts::rowMessage(const core::SquadRow& row, int currentYear) {
    if (row.issues.empty()) {
        return {};
    }
    return tr("Row %1: %2.").arg(row.sourceRow).arg(details(row, currentYear));
}

QString ImportTexts::statusName(core::RowStatus status) {
    switch (status) {
    case core::RowStatus::Ok:
        return tr("OK");
    case core::RowStatus::Warning:
        return tr("Warning");
    case core::RowStatus::Error:
        return tr("Error");
    }
    return {};
}

QString ImportTexts::statusKey(core::RowStatus status) {
    switch (status) {
    case core::RowStatus::Ok:
        return QStringLiteral("ok");
    case core::RowStatus::Warning:
        return QStringLiteral("warning");
    case core::RowStatus::Error:
        return QStringLiteral("error");
    }
    return {};
}

QString ImportTexts::actionName(core::RowAction action) {
    switch (action) {
    case core::RowAction::Create:
        return tr("New player");
    case core::RowAction::Update:
        return tr("Update");
    case core::RowAction::SkipExisting:
        return tr("Skip: already in the squad");
    case core::RowAction::SkipAmbiguous:
        return tr("Skip: unclear player");
    case core::RowAction::Invalid:
        return tr("Not imported");
    }
    return {};
}

} // namespace fdss::viewmodels
