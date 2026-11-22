#include "viewmodels/ImportActivityRows.hpp"

#include "core/algorithms/MatchMinutes.hpp"
#include "core/domain/Validation.hpp"
#include "viewmodels/ImportTexts.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/MetricTexts.hpp"

#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

namespace {

QString fromStd(const std::string& text) { return QString::fromStdString(text); }

QVariantMap option(const QString& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

QString rowList(const std::vector<int>& rows) {
    QStringList numbers;
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

std::optional<int> fieldOf(services::ImportKind kind, const std::string& key) {
    if (kind == services::ImportKind::Matches) {
        const auto field = core::matchFieldFromKey(key);
        return field ? std::optional(static_cast<int>(*field)) : std::nullopt;
    }
    const auto field = core::trainingFieldFromKey(key);
    return field ? std::optional(static_cast<int>(*field)) : std::nullopt;
}

const core::MetricDefinition* metricOf(const std::string& field,
                                       std::span<const core::MetricDefinition> metrics) {
    constexpr std::string_view kPrefix = kMetricChoicePrefix;
    if (!field.starts_with(kPrefix)) {
        return nullptr;
    }
    const std::string key = field.substr(kPrefix.size());
    const auto metric = std::ranges::find(metrics, key, &core::MetricDefinition::key);
    return metric == metrics.end() ? nullptr : &*metric;
}

// The name of the column of an issue, for "<name> '<value>', expected …".
QString columnName(services::ImportKind kind, const std::string& field,
                   std::span<const core::MetricDefinition> metrics) {
    if (const core::MetricDefinition* metric = metricOf(field, metrics)) {
        return MetricTexts::name(*metric);
    }
    if (const auto known = fieldOf(kind, field)) {
        return ImportActivityTexts::fieldName(kind, *known);
    }
    return fromStd(field);
}

// The limits of a whole-number column (SPEC section 8 step 4).
std::pair<int, int> rangeOf(services::ImportKind kind, const std::string& field) {
    if (field == "rpe") {
        return {0, core::kMaxRpe};
    }
    if (field == "rating") {
        return {core::kMinRating, core::kMaxRating};
    }
    if (field == "duration") {
        return {core::kMinSessionDuration, core::kMaxSessionDuration};
    }
    if (field == "shirt_number") {
        return {core::kMinShirtNumber, core::kMaxShirtNumber};
    }
    if (field == "minutes") {
        return kind == services::ImportKind::Matches ? std::pair{0, core::kMaxMatchMinute}
                                                     : std::pair{0, core::kMaxAttendanceMinutes};
    }
    return {0, 99};
}

QString contextOfSession(const core::TrainingPlan& plan, std::optional<std::size_t> session) {
    if (!session || *session >= plan.sessions.size()) {
        return {};
    }
    const core::PlannedSession& planned = plan.sessions[*session];
    QString when = IsoDates::displayText(planned.draft.date);
    if (planned.draft.startTime) {
        when += QLatin1Char(' ') + IsoDates::timeToIso(planned.draft.startTime);
    }
    return planned.existing ? QCoreApplication::translate("fdss::viewmodels::ImportActivityTexts",
                                                          "session %1 (in the app)")
                                  .arg(when)
                            : QCoreApplication::translate("fdss::viewmodels::ImportActivityTexts",
                                                          "session %1 (new)")
                                  .arg(when);
}

QString contextOfMatch(const core::MatchImport& import, const core::MatchPlan& plan,
                       std::optional<std::size_t> match) {
    if (!match || *match >= plan.matches.size() || *match >= import.matches.size()) {
        return {};
    }
    const core::PlannedMatch& planned = plan.matches[*match];
    const QString when = IsoDates::displayText(planned.draft.date);
    const QString opponent = fromStd(import.matches[*match].opponent);
    return planned.existing ? QCoreApplication::translate("fdss::viewmodels::ImportActivityTexts",
                                                          "match %1 against %2 (in the app)")
                                  .arg(when, opponent)
                            : QCoreApplication::translate("fdss::viewmodels::ImportActivityTexts",
                                                          "match %1 against %2 (new)")
                                  .arg(when, opponent);
}

QString playerName(const core::RowPlayer& player) {
    if (!player.text.empty()) {
        return fromStd(player.text);
    }
    return player.shirtNumber ? QStringLiteral("#%1").arg(*player.shirtNumber) : QString();
}

template <class Row, class PlannedRow>
ActivityReportRow reportRow(services::ImportKind kind, const Row& row, const PlannedRow& planned,
                            const QString& context,
                            std::span<const core::MetricDefinition> metrics) {
    std::vector<core::ActivityIssue> issues = row.issues;
    issues.insert(issues.end(), planned.issues.begin(), planned.issues.end());
    ActivityReportRow out;
    out.rowNumber = row.sourceRow;
    out.status = core::statusOf(issues);
    out.name = playerName(row.player);
    out.action = ImportActivityTexts::actionName(planned.action);
    QStringList parts;
    if (!context.isEmpty()) {
        parts.append(context);
    }
    QStringList problems;
    for (const core::ActivityIssue& issue : issues) {
        problems.append(ImportActivityTexts::issue(kind, issue, metrics));
    }
    parts.append(problems);
    out.details = parts.join(QStringLiteral("; "));
    if (!problems.isEmpty()) {
        out.message =
            QCoreApplication::translate("fdss::viewmodels::ImportActivityTexts", "Row %1: %2.")
                .arg(row.sourceRow)
                .arg(problems.join(QStringLiteral("; ")));
    }
    return out;
}

} // namespace

QString ImportActivityTexts::fieldName(services::ImportKind kind, int field) {
    if (kind == services::ImportKind::Matches) {
        switch (static_cast<core::MatchField>(field)) {
        case core::MatchField::Date:
            return tr("Date");
        case core::MatchField::Opponent:
            return tr("Opponent");
        case core::MatchField::Venue:
            return tr("Venue");
        case core::MatchField::Competition:
            return tr("Competition");
        case core::MatchField::Score:
            return tr("Score");
        case core::MatchField::GoalsFor:
            return tr("Goals for");
        case core::MatchField::GoalsAgainst:
            return tr("Goals against");
        case core::MatchField::Player:
            return tr("Player");
        case core::MatchField::ShirtNumber:
            return tr("Shirt number");
        case core::MatchField::Started:
            return tr("Started");
        case core::MatchField::Minutes:
            return tr("Minutes");
        case core::MatchField::Goals:
            return tr("Goals (not imported)");
        case core::MatchField::Assists:
            return tr("Assists (not imported)");
        case core::MatchField::YellowCards:
            return tr("Yellow cards (not imported)");
        case core::MatchField::RedCards:
            return tr("Red cards (not imported)");
        case core::MatchField::Rating:
            return tr("Rating");
        case core::MatchField::Rpe:
            return tr("RPE");
        }
        return {};
    }
    switch (static_cast<core::TrainingField>(field)) {
    case core::TrainingField::Date:
        return tr("Date");
    case core::TrainingField::StartTime:
        return tr("Start time");
    case core::TrainingField::Duration:
        return tr("Duration");
    case core::TrainingField::Type:
        return tr("Session type");
    case core::TrainingField::Player:
        return tr("Player");
    case core::TrainingField::ShirtNumber:
        return tr("Shirt number");
    case core::TrainingField::Status:
        return tr("Attendance");
    case core::TrainingField::Rpe:
        return tr("RPE");
    case core::TrainingField::Minutes:
        return tr("Minutes");
    }
    return {};
}

QVariantList ImportActivityTexts::fieldOptions(services::ImportKind kind,
                                               std::span<const core::MetricDefinition> metrics,
                                               bool canCreateMetric) {
    QVariantList options{option(QString(), tr("Ignore column"))};
    if (kind == services::ImportKind::Matches) {
        for (const core::MatchField field : core::kMatchFields) {
            options.append(option(QString::fromUtf8(core::toKey(field)),
                                  fieldName(kind, static_cast<int>(field))));
        }
    } else {
        for (const core::TrainingField field : core::kTrainingFields) {
            options.append(option(QString::fromUtf8(core::toKey(field)),
                                  fieldName(kind, static_cast<int>(field))));
        }
    }
    for (const core::MetricDefinition& metric : metrics) {
        options.append(option(QString::fromUtf8(kMetricChoicePrefix) + fromStd(metric.key),
                              tr("Metric: %1").arg(MetricTexts::name(metric))));
    }
    if (canCreateMetric) {
        options.append(option(QString::fromUtf8(kCreateMetricChoice), tr("Create metric…")));
    }
    return options;
}

QString ImportActivityTexts::mappingIssue(core::ActivityMappingIssue issue) {
    switch (issue) {
    case core::ActivityMappingIssue::MissingDate:
        return tr("Choose the column with the date.");
    case core::ActivityMappingIssue::MissingPlayer:
        return tr("Choose the column with the player (a name or a shirt number).");
    case core::ActivityMappingIssue::MissingOpponent:
        return tr("Choose the column with the opponent.");
    case core::ActivityMappingIssue::MissingValues:
        return tr("Choose at least one column to import, such as attendance, RPE, minutes or a "
                  "metric.");
    case core::ActivityMappingIssue::DuplicateField:
        return tr("Two columns have the same meaning. Set one of them to \"Ignore column\".");
    case core::ActivityMappingIssue::DuplicateMetric:
        return tr("Two columns go to the same metric. Set one of them to \"Ignore column\".");
    }
    return {};
}

QString ImportActivityTexts::issue(services::ImportKind kind, const core::ActivityIssue& issue,
                                   std::span<const core::MetricDefinition> metrics) {
    const QString value = fromStd(issue.value);
    const QString column = columnName(kind, issue.field, metrics);
    const bool matches = kind == services::ImportKind::Matches;
    const auto [min, max] = rangeOf(kind, issue.field);
    const core::MetricDefinition* metric = metricOf(issue.field, metrics);
    switch (issue.kind) {
    case core::ActivityIssueKind::MissingValue:
        if (issue.field == "player") {
            return tr("the player is empty, expected a name or a shirt number");
        }
        return tr("%1 is empty").arg(column);
    case core::ActivityIssueKind::NotADate:
        return tr("%1 '%2', expected a date such as 05.01.2026, 2026-01-05 or 05/01/2026")
            .arg(column, value);
    case core::ActivityIssueKind::NotATime:
        return tr("%1 '%2', expected a time such as 18:00").arg(column, value);
    case core::ActivityIssueKind::NotANumber:
        if (metric != nullptr) {
            return tr("%1 '%2', expected a value of the metric").arg(column, value);
        }
        if (issue.field == "score") {
            return tr("%1 '%2', expected goals for and against such as 2:1").arg(column, value);
        }
        if (issue.severity == core::IssueSeverity::Warning) {
            return tr("%1 '%2' is not a number and was left out").arg(column, value);
        }
        return tr("%1 '%2', expected a number").arg(column, value);
    case core::ActivityIssueKind::NotAWholeNumber:
        return tr("%1 '%2', expected a whole number from %3 to %4")
            .arg(column, value)
            .arg(min)
            .arg(max);
    case core::ActivityIssueKind::OutOfRange:
        if (metric != nullptr) {
            return tr("%1 '%2' is outside the limits of the metric").arg(column, value);
        }
        return tr("%1 '%2', expected a number from %3 to %4").arg(column, value).arg(min).arg(max);
    case core::ActivityIssueKind::UnknownValue:
        if (issue.field == "status") {
            return tr("%1 '%2', expected present, absent, excused or injured").arg(column, value);
        }
        if (issue.field == "venue") {
            return tr("%1 '%2', expected home, away or neutral").arg(column, value);
        }
        if (issue.field == "started") {
            return tr("%1 '%2', expected yes or no").arg(column, value);
        }
        return tr("%1 '%2' is unknown, saved as training").arg(column, value);
    case core::ActivityIssueKind::TooLong:
        return tr("%1 '%2' is longer than 100 characters").arg(column, value);
    case core::ActivityIssueKind::DuplicateInFile:
        return matches
                   ? tr("the same player is in row %1 of this match").arg(rowList(issue.otherRows))
                   : tr("the same player is in row %1 of this session")
                         .arg(rowList(issue.otherRows));
    case core::ActivityIssueKind::Inconsistent:
        return matches ? tr("%1 differs from row %2 of the same match")
                             .arg(column, rowList(issue.otherRows))
                       : tr("%1 differs from row %2 of the same session")
                             .arg(column, rowList(issue.otherRows));
    case core::ActivityIssueKind::ValuesDropped:
        return matches ? tr("rating and RPE left out: the player did not play")
                       : tr("RPE and minutes left out: the player was not present");
    case core::ActivityIssueKind::NothingToImport:
        return tr("no attendance, RPE, minutes or metric value, skipped");
    case core::ActivityIssueKind::TemplateExample:
        return tr("skipped the example row of the template");
    case core::ActivityIssueKind::StartedWithoutMinutes:
        return tr("started without minutes, expected the minutes played");
    case core::ActivityIssueKind::EventsNotImported:
        return tr("not imported: %1 goal(s), %2 assist(s), %3 card(s) — no minute; enter them in "
                  "the match report")
            .arg(issue.count)
            .arg(issue.count2)
            .arg(issue.count3);
    case core::ActivityIssueKind::VenueDefault:
        return tr("no venue for the match, saved as home");
    case core::ActivityIssueKind::UnknownPlayer:
        return tr("'%1' is not in the squad, skipped").arg(value);
    case core::ActivityIssueKind::NewPlayer:
        return tr("'%1' is not in the squad and is added to it").arg(value);
    case core::ActivityIssueKind::CannotCreatePlayer:
        return tr("'%1' is not in the squad; a new player needs the first and the last name")
            .arg(value);
    case core::ActivityIssueKind::AmbiguousPlayer:
        if (issue.candidates.size() == 1) {
            return tr("'%1' does not fit %2, skipped").arg(value, playerList(issue.candidates));
        }
        return tr("fits several players (%1), skipped").arg(playerList(issue.candidates));
    case core::ActivityIssueKind::SeveralSessions:
        return tr("the app has %n session(s) on this day; add a start time column", nullptr,
                  issue.count);
    case core::ActivityIssueKind::SeveralMatches:
        return tr("the app has %n match(es) against %1 on this day", nullptr, issue.count)
            .arg(value);
    case core::ActivityIssueKind::MatchRecorded:
        return tr("the match against %1 was entered in Match Day; it is not changed").arg(value);
    case core::ActivityIssueKind::MatchPlanned:
        return tr("the match against %1 is still planned; finish it in Match Day without the "
                  "clock or delete it")
            .arg(value);
    case core::ActivityIssueKind::ScoreDiffers:
        return value.isEmpty() ? tr("the match in the app has no score; it stays so")
                               : tr("the score in the app (%1) stays").arg(value);
    case core::ActivityIssueKind::ExistingKept:
        return tr("the app holds other values; kept (turn on updating to overwrite them)");
    case core::ActivityIssueKind::PlanChanged:
        return tr("'%1' fits another player than at the check (the squad changed)").arg(value);
    case core::ActivityIssueKind::MetricChanged:
        return tr("%1 '%2': the metric was changed or deleted since the check — check the rows "
                  "again")
            .arg(metric != nullptr ? column : tr("Metric"), value);
    }
    return {};
}

QString ImportActivityTexts::actionName(core::ActivityAction action) {
    switch (action) {
    case core::ActivityAction::Create:
        return tr("Add");
    case core::ActivityAction::Update:
        return tr("Update");
    case core::ActivityAction::Unchanged:
        return tr("No change");
    case core::ActivityAction::KeepExisting:
        return tr("Keep the app's values");
    case core::ActivityAction::Skip:
        return tr("Skip");
    case core::ActivityAction::Invalid:
        return tr("Not imported");
    }
    return {};
}

QString ImportActivityTexts::notImported(const core::NotImportedEvents& events) {
    if (events.goals + events.assists + events.cards == 0) {
        return {};
    }
    return tr("Not imported: %1 goal(s), %2 assist(s), %3 card(s) — no minute; enter them in the "
              "match report.")
        .arg(events.goals)
        .arg(events.assists)
        .arg(events.cards);
}

QString ImportActivityTexts::suspensionNote(const core::NotImportedEvents& events) {
    if (events.yellowCards == 0) {
        return {};
    }
    return tr("Yellow cards from the import do not count toward a suspension.");
}

QString ImportActivityTexts::undoQuestion(const services::ImportUndoPreview& preview) {
    QStringList parts;
    if (preview.notes > 0) {
        parts.append(tr("%n note(s)", nullptr, preview.notes));
    }
    if (preview.events > 0) {
        parts.append(tr("%n match event(s)", nullptr, preview.events));
    }
    if (preview.values > 0) {
        parts.append(tr("%n metric value(s)", nullptr, preview.values));
    }
    if (preview.entries > 0) {
        parts.append(tr("%n other entry(ies)", nullptr, preview.entries));
    }
    return tr("Undo also deletes %1 added or changed after the import. Continue?")
        .arg(parts.join(QStringLiteral(", ")));
}

std::vector<ActivityReportRow> trainingReport(const core::TrainingImport& import,
                                              const core::TrainingPlan& plan,
                                              std::span<const core::MetricDefinition> metrics) {
    std::vector<ActivityReportRow> rows;
    for (std::size_t index = 0; index < import.rows.size() && index < plan.rows.size(); ++index) {
        rows.push_back(reportRow(services::ImportKind::Training, import.rows[index],
                                 plan.rows[index], contextOfSession(plan, plan.rows[index].session),
                                 metrics));
    }
    return rows;
}

std::vector<ActivityReportRow> matchReport(const core::MatchImport& import,
                                           const core::MatchPlan& plan,
                                           std::span<const core::MetricDefinition> metrics) {
    std::vector<ActivityReportRow> rows;
    for (std::size_t index = 0; index < import.rows.size() && index < plan.rows.size(); ++index) {
        rows.push_back(reportRow(services::ImportKind::Matches, import.rows[index],
                                 plan.rows[index],
                                 contextOfMatch(import, plan, plan.rows[index].match), metrics));
    }
    return rows;
}

} // namespace fdss::viewmodels
