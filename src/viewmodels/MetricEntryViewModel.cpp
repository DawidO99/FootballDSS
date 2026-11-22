#include "viewmodels/MetricEntryViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/CalendarService.hpp"
#include "services/Clock.hpp"
#include "services/MetricService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/MetricTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCollator>
#include <QCoreApplication>
#include <QEvent>
#include <QLocale>

#include <algorithm>
#include <limits>
#include <tuple>
#include <utility>
#include <variant>

namespace fdss::viewmodels {

namespace {

const QList<int> kEditedRoles{
    MetricEntryViewModel::ValueTextRole, MetricEntryViewModel::CheckedRole,
    MetricEntryViewModel::HasValueRole,  MetricEntryViewModel::StateTextRole,
    MetricEntryViewModel::RowErrorRole,  MetricEntryViewModel::HasErrorRole};

bool lessByShirtAndName(const core::Player& a, const core::Player& b, const QCollator& collator) {
    constexpr int kNoNumber = std::numeric_limits<int>::max();
    const int numberA = a.shirtNumber.value_or(kNoNumber);
    const int numberB = b.shirtNumber.value_or(kNoNumber);
    if (numberA != numberB) {
        return numberA < numberB;
    }
    const int surnames =
        collator.compare(QString::fromStdString(a.lastName), QString::fromStdString(b.lastName));
    if (surnames != 0) {
        return surnames < 0;
    }
    return collator.compare(QString::fromStdString(a.firstName),
                            QString::fromStdString(b.firstName)) < 0;
}

} // namespace

MetricEntryViewModel::MetricEntryViewModel(QObject* parent)
    : QAbstractListModel(parent), m_metricService(ServiceRegistry::instance().metrics()),
      m_calendar(ServiceRegistry::instance().calendar()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_metrics(new MetricDefinitionListModel(this)),
      m_sessions(new SessionPickerModel(this)), m_matches(new ChoiceListModel(this)) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int MetricEntryViewModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant MetricEntryViewModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Row& row = m_rows[static_cast<std::size_t>(index.row())];
    bool ok = true;
    const std::optional<double> value = parsed(row, ok);
    switch (role) {
    case PlayerIdRole:
        return QVariant::fromValue<qint64>(row.playerId.value);
    case ShirtNumberRole:
        return row.shirtNumber ? QVariant(*row.shirtNumber) : QVariant{};
    case PlayerNameRole:
        return row.name;
    case ValueTextRole:
        return row.text;
    case CheckedRole:
        return ok && value && *value != 0.0;
    case HasValueRole:
        return !row.text.trimmed().isEmpty();
    case StateTextRole:
        if (!m_metric || m_metric->valueType != core::MetricValueType::Boolean) {
            return QString{};
        }
        return ok && value ? MetricTexts::value(*m_metric, *value) : tr("Not entered");
    case RowErrorRole:
        return rowError(row);
    case HasErrorRole:
        return !rowError(row).isEmpty();
    default:
        return {};
    }
}

QHash<int, QByteArray> MetricEntryViewModel::roleNames() const {
    return {{PlayerIdRole, "playerId"},     {ShirtNumberRole, "shirtNumber"},
            {PlayerNameRole, "playerName"}, {ValueTextRole, "valueText"},
            {CheckedRole, "checked"},       {HasValueRole, "hasValue"},
            {StateTextRole, "stateText"},   {RowErrorRole, "rowError"},
            {HasErrorRole, "hasError"}};
}

qint64 MetricEntryViewModel::teamId() const { return m_teamId; }

void MetricEntryViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    m_target.reset();
    m_rows.clear();
    m_dirty = false;
    reload();
}

MetricDefinitionListModel* MetricEntryViewModel::metrics() const { return m_metrics; }

SessionPickerModel* MetricEntryViewModel::sessions() const { return m_sessions; }

ChoiceListModel* MetricEntryViewModel::matches() const { return m_matches; }

qint64 MetricEntryViewModel::metricId() const { return m_metricId; }

QString MetricEntryViewModel::scope() const {
    const core::MetricDefinition* open = metric();
    return open ? QString::fromUtf8(core::toDbText(open->scope)) : QString{};
}

QString MetricEntryViewModel::valueType() const {
    const core::MetricDefinition* open = metric();
    return open ? QString::fromUtf8(core::toDbText(open->valueType)) : QString{};
}

QString MetricEntryViewModel::metricHint() const {
    const core::MetricDefinition* open = metric();
    if (!open) {
        return {};
    }
    QStringList parts{MetricTexts::valueTypeName(open->valueType)};
    if (const QString range = MetricTexts::range(*open); !range.isEmpty()) {
        parts.append(tr("Allowed: %1").arg(range));
    } else if (open->unit && !QString::fromStdString(*open->unit).trimmed().isEmpty()) {
        parts.append(tr("Unit: %1").arg(QString::fromStdString(*open->unit).trimmed()));
    }
    if (open->valueType != core::MetricValueType::Boolean) {
        parts.append(open->higherIsBetter ? tr("Higher is better") : tr("Lower is better"));
    }
    return parts.join(QStringLiteral(" · "));
}

qint64 MetricEntryViewModel::targetId() const {
    if (!m_target) {
        return 0;
    }
    if (const auto* match = std::get_if<core::MatchId>(&*m_target)) {
        return match->value;
    }
    if (const auto* session = std::get_if<core::TrainingSessionId>(&*m_target)) {
        return session->value;
    }
    return 0;
}

QString MetricEntryViewModel::date() const {
    if (!m_target) {
        return {};
    }
    if (const auto* day = std::get_if<core::Date>(&*m_target)) {
        return IsoDates::toIso(*day);
    }
    return {};
}

bool MetricEntryViewModel::hasTarget() const { return m_target.has_value(); }

int MetricEntryViewModel::count() const { return rowCount(); }

bool MetricEntryViewModel::readOnly() const {
    return !m_session.isAllowed(core::Permission::ManageMetrics);
}

bool MetricEntryViewModel::dirty() const { return m_dirty; }

bool MetricEntryViewModel::canSave() const {
    return !readOnly() && m_dirty && m_target.has_value() && !m_loadFailure;
}

QString MetricEntryViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void MetricEntryViewModel::reload() {
    setLoadFailure(std::nullopt);
    refreshLists();
    if (m_loadFailure) {
        return;
    }
    if (m_metricId == 0 || !m_metrics->find(m_metricId)) {
        const QVariantMap first = m_metrics->rowAt(0);
        m_metricId = first.value(QStringLiteral("metricId")).toLongLong();
        m_metric.reset();
        m_target.reset();
        m_dirty = false;
        emit metricChanged();
    }
    if (m_metricId == 0 || m_teamId == 0) {
        clearSheet();
        return;
    }
    // The rows of the user stay; a dirty grid is only reloaded by leaving it, which asks first.
    if (m_dirty) {
        return;
    }
    const core::MetricScope scope = m_metrics->find(m_metricId)->definition.scope;
    if (m_target && core::scopeOf(*m_target) == scope) {
        openTarget(*m_target);
        return;
    }
    openDefaultTarget();
}

void MetricEntryViewModel::openMetric(qint64 metricId) {
    if (!m_metrics->find(metricId)) {
        report(core::Error{core::ErrorCode::NotFound, "unknown metric", {}});
        return;
    }
    m_metricId = metricId;
    m_metric.reset();
    m_target.reset();
    m_dirty = false;
    emit metricChanged();
    openDefaultTarget();
}

void MetricEntryViewModel::openSession(qint64 sessionId) {
    openTarget(core::TrainingSessionId{sessionId});
}

void MetricEntryViewModel::openMatch(qint64 matchId) { openTarget(core::MatchId{matchId}); }

void MetricEntryViewModel::openDate(const QString& isoDate) {
    const std::optional<core::Date> day = IsoDates::parse(isoDate);
    if (!day || !core::isSupportedDate(*day)) {
        report(core::Error{core::ErrorCode::InvalidInput, "not an ISO date", "date"});
        return;
    }
    openTarget(*day);
}

void MetricEntryViewModel::setValueText(int row, const QString& text) {
    if (!isEditable(row)) {
        return;
    }
    setText(row, text);
}

void MetricEntryViewModel::setChecked(int row, bool checked) {
    if (!isEditable(row)) {
        return;
    }
    setText(row, checked ? QStringLiteral("1") : QStringLiteral("0"));
}

void MetricEntryViewModel::clearValue(int row) {
    if (!isEditable(row)) {
        return;
    }
    setText(row, QString{});
}

void MetricEntryViewModel::rememberCell(int row) {
    if (row < 0 || row >= rowCount()) {
        return;
    }
    m_remembered = std::pair{row, m_rows[static_cast<std::size_t>(row)].text};
}

void MetricEntryViewModel::revertCell(int row) {
    if (!m_remembered || m_remembered->first != row || !isEditable(row)) {
        return;
    }
    setText(row, m_remembered->second);
}

bool MetricEntryViewModel::save() {
    if (!checkAllowed() || !m_target || !m_metric || m_loadFailure) {
        return false;
    }
    std::vector<core::MetricValueDraft> drafts;
    drafts.reserve(m_rows.size());
    for (Row& row : m_rows) {
        row.saveError.reset();
        bool ok = true;
        const std::optional<double> value = parsed(row, ok);
        drafts.push_back({row.playerId, value});
    }
    if (firstErrorRow() >= 0) {
        emit dataChanged(index(0), index(rowCount() - 1), kEditedRoles);
        emit errorOccurred(
            tr("Some values are not valid. Correct the marked rows and save again."));
        return false;
    }
    const auto result = m_metricService.saveValues(core::MetricDefinitionId{m_metricId},
                                                   core::TeamId{m_teamId}, *m_target, drafts);
    if (!result) {
        report(result.error());
        return false;
    }
    if (!result->empty()) {
        for (const services::MetricRowError& error : *result) {
            const auto found = std::ranges::find_if(
                m_rows, [&](const Row& row) { return row.playerId == error.player; });
            if (found != m_rows.end()) {
                found->saveError = error.error;
            }
        }
        emit dataChanged(index(0), index(rowCount() - 1), kEditedRoles);
        emit errorOccurred(
            tr("Some values are not valid. Correct the marked rows and save again."));
        return false;
    }
    // What the database holds now. updateDirty() emits dirtyChanged, so the badge, Save and the
    // leave guard follow at once, also when reading the grid again fails below.
    for (Row& row : m_rows) {
        bool ok = true;
        row.stored = parsed(row, ok);
    }
    updateDirty();
    // The cell that has the focus keeps Esc: it now goes back to the saved text.
    const std::optional<int> rememberedRow =
        m_remembered ? std::optional(m_remembered->first) : std::nullopt;
    core::Error failure;
    if (!loadSheet(*m_target, failure)) {
        // Saved, but the grid cannot show it: the view offers to read again.
        setLoadFailure(failure);
    }
    if (rememberedRow && *rememberedRow < rowCount()) {
        rememberCell(*rememberedRow);
    }
    // The value counts of the definitions changed.
    refreshLists();
    emit saved();
    return true;
}

void MetricEntryViewModel::discardChanges() {
    m_dirty = false;
    emit dirtyChanged();
    if (m_target) {
        openTarget(*m_target);
    }
}

int MetricEntryViewModel::firstErrorRow() const {
    const auto found =
        std::ranges::find_if(m_rows, [this](const Row& row) { return !rowError(row).isEmpty(); });
    return found == m_rows.end() ? -1 : static_cast<int>(std::distance(m_rows.begin(), found));
}

bool MetricEntryViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit metricChanged();
        emit loadErrorChanged();
        rebuildMatchRows();
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1), {StateTextRole, RowErrorRole});
        }
    }
    return QAbstractListModel::eventFilter(watched, event);
}

const core::MetricDefinition* MetricEntryViewModel::metric() const {
    if (m_metric && m_metric->id.value == m_metricId) {
        return &*m_metric;
    }
    const services::MetricDefinitionInfo* info = m_metrics->find(m_metricId);
    return info ? &info->definition : nullptr;
}

void MetricEntryViewModel::refreshLists() {
    if (!m_session.user()) {
        m_metrics->setDefinitions({});
        m_sessions->setSessions({});
        m_matchItems.clear();
        rebuildMatchRows();
        return;
    }
    auto definitions = m_metricService.listDefinitions();
    if (!definitions) {
        qCritical("MetricEntryViewModel: %s", definitions.error().detail.c_str());
        setLoadFailure(definitions.error());
        return;
    }
    m_metrics->setDefinitions(std::move(*definitions));
    if (m_teamId == 0) {
        m_sessions->setSessions({});
        m_matchItems.clear();
        rebuildMatchRows();
        return;
    }
    auto items = m_calendar.listItems(core::TeamId{m_teamId});
    if (!items) {
        qCritical("MetricEntryViewModel: %s", items.error().detail.c_str());
        setLoadFailure(items.error());
        return;
    }
    // Newest first, as the pickers show them.
    std::ranges::sort(
        items->sessions, [](const core::TrainingSession& a, const core::TrainingSession& b) {
            return std::tie(a.date, a.startTime, a.id) > std::tie(b.date, b.startTime, b.id);
        });
    std::ranges::sort(items->matches,
                      [](const services::CalendarMatch& a, const services::CalendarMatch& b) {
                          return std::tie(a.match.date, a.match.kickoffTime, a.match.id) >
                                 std::tie(b.match.date, b.match.kickoffTime, b.match.id);
                      });
    m_sessions->setSessions(std::move(items->sessions));
    m_matchItems = std::move(items->matches);
    rebuildMatchRows();
}

void MetricEntryViewModel::rebuildMatchRows() {
    QList<QVariantList> rows;
    for (const services::CalendarMatch& item : m_matchItems) {
        //: Match picker of the Metrics section: the date and the opponent.
        rows.append(QVariantList{QVariant::fromValue<qint64>(item.match.id.value),
                                 tr("%1 · %2").arg(IsoDates::displayText(item.match.date),
                                                   QString::fromStdString(item.opponentName))});
    }
    m_matches->setRows(std::move(rows));
}

void MetricEntryViewModel::openTarget(const core::MetricTarget& target) {
    core::Error failure;
    if (loadSheet(target, failure)) {
        setLoadFailure(std::nullopt);
        return;
    }
    qCritical("MetricEntryViewModel: %s", failure.detail.c_str());
    if (m_target && !m_loadFailure && failure.code != core::ErrorCode::Database) {
        // The open grid stays on the screen; the view shows its entry in the picker again.
        emit targetChanged();
        report(failure);
        return;
    }
    setLoadFailure(failure);
}

void MetricEntryViewModel::openDefaultTarget() {
    const core::MetricDefinition* open = metric();
    if (!open || m_teamId == 0) {
        clearSheet();
        return;
    }
    const core::Date today = m_clock.today();
    switch (open->scope) {
    case core::MetricScope::Training: {
        const qint64 id = m_sessions->defaultSessionId(today);
        if (id == 0) {
            clearSheet();
            return;
        }
        openTarget(core::TrainingSessionId{id});
        return;
    }
    case core::MetricScope::Match: {
        // Newest first: the first on or before today, otherwise the nearest future one.
        const auto past =
            std::ranges::find_if(m_matchItems, [&](const services::CalendarMatch& item) {
                return item.match.date <= today;
            });
        if (past != m_matchItems.end()) {
            openTarget(past->match.id);
        } else if (!m_matchItems.empty()) {
            openTarget(m_matchItems.back().match.id);
        } else {
            clearSheet();
        }
        return;
    }
    case core::MetricScope::Player:
        openTarget(today);
        return;
    }
}

bool MetricEntryViewModel::loadSheet(const core::MetricTarget& target, core::Error& failure) {
    auto sheet =
        m_metricService.sheet(core::MetricDefinitionId{m_metricId}, core::TeamId{m_teamId}, target);
    if (!sheet) {
        failure = sheet.error();
        return false;
    }
    applySheet(std::move(*sheet));
    return true;
}

void MetricEntryViewModel::applySheet(services::MetricSheet sheet) {
    // The names are Polish whatever the language of the app.
    QCollator collator(QLocale(QLocale::Polish, QLocale::Poland));
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::ranges::stable_sort(
        sheet.rows, [&](const services::MetricSheetRow& a, const services::MetricSheetRow& b) {
            return lessByShirtAndName(a.player, b.player, collator);
        });
    std::vector<Row> rows;
    rows.reserve(sheet.rows.size());
    for (const services::MetricSheetRow& source : sheet.rows) {
        Row row;
        row.playerId = source.player.id;
        row.shirtNumber = source.player.shirtNumber;
        row.name = SquadTexts::displayName(source.player);
        row.stored = source.value;
        row.text = source.value ? MetricTexts::editText(sheet.metric, *source.value) : QString{};
        rows.push_back(std::move(row));
    }

    // Everything the rows read comes first: a reset creates the delegates while it is emitted, and
    // they read the model right then.
    m_metric = std::move(sheet.metric);
    m_target = sheet.target;
    m_remembered.reset();
    const bool samePlayers = std::ranges::equal(
        rows, m_rows, [](const Row& a, const Row& b) { return a.playerId == b.playerId; });
    if (samePlayers) {
        m_rows = std::move(rows);
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
    } else {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }
    emit metricChanged();
    emit targetChanged();
    updateDirty();
}

void MetricEntryViewModel::clearSheet() {
    m_target.reset();
    m_remembered.reset();
    beginResetModel();
    m_rows.clear();
    endResetModel();
    emit targetChanged();
    updateDirty();
}

void MetricEntryViewModel::setText(int row, const QString& text) {
    Row& edited = m_rows[static_cast<std::size_t>(row)];
    if (edited.text == text) {
        return;
    }
    edited.text = text;
    edited.saveError.reset();
    emit dataChanged(index(row), index(row), kEditedRoles);
    updateDirty();
}

void MetricEntryViewModel::updateDirty() {
    const bool dirty = std::ranges::any_of(m_rows, [this](const Row& row) {
        bool ok = true;
        const std::optional<double> value = parsed(row, ok);
        return !ok || value != row.stored;
    });
    if (dirty != m_dirty) {
        m_dirty = dirty;
        emit dirtyChanged();
    }
}

void MetricEntryViewModel::setLoadFailure(std::optional<core::Error> failure) {
    const bool changed = failure.has_value() != m_loadFailure.has_value();
    m_loadFailure = std::move(failure);
    if (changed) {
        emit loadErrorChanged();
        emit dirtyChanged();
    }
}

std::optional<double> MetricEntryViewModel::parsed(const Row& row, bool& ok) const {
    ok = true;
    const QString text = row.text.trimmed();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    const core::MetricDefinition* open = metric();
    if (!open) {
        ok = false;
        return std::nullopt;
    }
    const std::optional<double> value = core::parseMetricValue(text.toStdString(), open->valueType);
    ok = value.has_value();
    return value;
}

QString MetricEntryViewModel::rowError(const Row& row) const {
    const core::MetricDefinition* open = metric();
    if (!open) {
        return {};
    }
    bool ok = true;
    const std::optional<double> value = parsed(row, ok);
    if (!ok) {
        return MetricTexts::valueError(*open, "value");
    }
    if (value) {
        const core::ValidationErrors errors = core::validateValue(*open, *value);
        if (!errors.empty()) {
            return MetricTexts::valueError(*open, errors.front().field);
        }
    }
    if (row.saveError) {
        return ErrorPresenter::message(*row.saveError);
    }
    return {};
}

bool MetricEntryViewModel::isEditable(int row) const {
    return row >= 0 && row < rowCount() && !readOnly() && !m_loadFailure;
}

bool MetricEntryViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::ManageMetrics)) {
        return true;
    }
    report(core::Error{core::ErrorCode::PermissionDenied,
                       "refused by the viewmodel: ManageMetrics is needed",
                       {}});
    return false;
}

void MetricEntryViewModel::report(const core::Error& error) {
    qCritical("MetricEntryViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
