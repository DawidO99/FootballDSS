#include "viewmodels/LineupModels.hpp"

#include <QLocale>

#include <algorithm>
#include <array>
#include <optional>

namespace fdss::viewmodels {

TacticsBoardModel::TacticsBoardModel(QObject* parent)
    : RowsModel({"slotIndex", "positionKey", "positionText", "pitchX", "pitchY", "playerId",
                 "shirtNumber", "shortName", "fullName", "severity", "issueText", "selected",
                 "yellowCards", "goals", "hidden", "labelRoom"},
                parent) {}

BenchListModel::BenchListModel(QObject* parent)
    : RowsModel({"benchIndex", "playerId", "shirtNumber", "name", "shortName", "positionsText",
                 "severity", "issueText"},
                parent) {}

LineupPlayersModel::LineupPlayersModel(QObject* parent)
    : RowsModel({"playerId",     "shirtNumber",   "name",         "shortName",     "positionsText",
                 "positionCode", "statusKey",     "statusText",   "loadStatusKey", "loadStatusText",
                 "placement",    "placementText", "severity",     "issueText",     "detailsText",
                 "section",      "sectionText",   "sectionOrder", "lastName",      "firstName",
                 "positionRank", "statusRank",    "loadRank"},
                parent) {}

namespace {

constexpr std::array kSortColumns{"number", "name", "position", "status", "load"};

std::optional<int> optionalInt(const QVariant& value) {
    return value.isValid() && !value.isNull() ? std::optional<int>(value.toInt()) : std::nullopt;
}

} // namespace

LineupPlayersSortModel::LineupPlayersSortModel(LineupPlayersModel* source, QObject* parent)
    : QSortFilterProxyModel(parent), m_source(source),
      // The names are Polish whatever the language of the app, as in Squad.
      m_collator(QLocale(QLocale::Polish, QLocale::Poland)) {
    m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    setSourceModel(source);
    setDynamicSortFilter(true);
    // The direction is in lessThan(): the sections keep their order either way.
    sort(0, Qt::AscendingOrder);
}

QString LineupPlayersSortModel::sortColumn() const { return m_column; }

bool LineupPlayersSortModel::ascending() const { return m_ascending; }

void LineupPlayersSortModel::sortBy(const QString& column) {
    setSort(column, column == m_column ? !m_ascending : true);
}

void LineupPlayersSortModel::setSort(const QString& column, bool ascending) {
    if (std::ranges::none_of(
            kSortColumns, [&](const char* known) { return column == QLatin1StringView(known); })) {
        qWarning("LineupPlayersSortModel: unknown column '%s'", qUtf8Printable(column));
        return;
    }
    if (column == m_column && ascending == m_ascending) {
        return;
    }
    m_column = column;
    m_ascending = ascending;
    invalidate();
    emit sortChanged();
}

QVariantMap LineupPlayersSortModel::get(int row) const {
    if (row < 0 || row >= rowCount()) {
        return {};
    }
    return m_source->get(mapToSource(index(row, 0)).row());
}

int LineupPlayersSortModel::rowOf(qint64 playerId) const {
    for (int row = 0; row < rowCount(); ++row) {
        if (value(row, "playerId").toLongLong() == playerId) {
            return row;
        }
    }
    return -1;
}

QVariant LineupPlayersSortModel::value(int row, const QByteArray& roleName) const {
    if (row < 0 || row >= rowCount()) {
        return {};
    }
    return m_source->value(mapToSource(index(row, 0)).row(), roleName);
}

QVariant LineupPlayersSortModel::sourceValue(const QModelIndex& index,
                                             const QByteArray& roleName) const {
    return m_source->value(index.row(), roleName);
}

bool LineupPlayersSortModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
    const int leftSection = sourceValue(left, "section").toInt();
    const int rightSection = sourceValue(right, "section").toInt();
    if (leftSection != rightSection) {
        return leftSection < rightSection;
    }
    // The XI and the bench keep the order of the board.
    if (leftSection < 2) {
        return sourceValue(left, "sectionOrder").toInt() <
               sourceValue(right, "sectionOrder").toInt();
    }
    // Negative: left first; positive: right first; 0: the next key decides.
    const auto byOptional = [](std::optional<int> a, std::optional<int> b, bool ascending) {
        if (!a || !b) {
            return a.has_value() == b.has_value() ? 0 : (a ? -1 : 1);
        }
        return *a == *b ? 0 : ((*a < *b) == ascending ? -1 : 1);
    };
    const auto byName = [&] {
        const int byLast = m_collator.compare(sourceValue(left, "lastName").toString(),
                                              sourceValue(right, "lastName").toString());
        return byLast != 0 ? byLast
                           : m_collator.compare(sourceValue(left, "firstName").toString(),
                                                sourceValue(right, "firstName").toString());
    };
    const auto number = [&](const QModelIndex& index) {
        return optionalInt(sourceValue(index, "shirtNumber"));
    };
    int order = 0;
    if (m_column == QLatin1StringView("name")) {
        const int byNames = byName();
        order = m_ascending ? byNames : -byNames;
    } else if (m_column != QLatin1StringView("number")) {
        const QByteArray role = m_column == QLatin1StringView("position") ? "positionRank"
                                : m_column == QLatin1StringView("status") ? "statusRank"
                                                                          : "loadRank";
        order = byOptional(optionalInt(sourceValue(left, role)),
                           optionalInt(sourceValue(right, role)), m_ascending);
    }
    if (order == 0) {
        order = byOptional(number(left), number(right),
                           m_column == QLatin1StringView("number") ? m_ascending : true);
    }
    if (order == 0) {
        order = byName();
    }
    return order < 0;
}

SlotPickerModel::SlotPickerModel(QObject* parent)
    : RowsModel({"playerId", "shirtNumber", "name", "positionsText", "group", "groupText",
                 "statusKey", "statusText", "available", "loadStatusKey", "loadStatusText",
                 "placement", "placementText", "current"},
                parent) {}

LineupIssuesModel::LineupIssuesModel(QObject* parent)
    : RowsModel(
          {"severity", "severityText", "ruleKey", "text", "playerId", "slotIndex", "benchIndex"},
          parent) {}

} // namespace fdss::viewmodels
