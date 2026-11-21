#include "viewmodels/MatchReportViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/Session.hpp"
#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/MatchDayTexts.hpp"
#include "viewmodels/MatchTimelineRows.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QVariantMap>

#include <algorithm>
#include <ranges>

namespace fdss::viewmodels {

namespace {

constexpr int kMinRating = 1;
constexpr int kMaxRating = 10;
constexpr int kMaxRpe = 10;
// A note in the undo text: its beginning only.
constexpr int kNotePreview = 40;

QVariant optionalInt(const std::optional<int>& value) {
    return value ? QVariant(*value) : QVariant{};
}

QVariantMap choice(const QVariant& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

std::optional<core::MatchEventType> eventTypeOf(const QString& type) {
    if (type == QLatin1StringView("goal")) {
        return core::MatchEventType::Goal;
    }
    if (type == QLatin1StringView("opponentGoal")) {
        return core::MatchEventType::OpponentGoal;
    }
    if (type == QLatin1StringView("yellow")) {
        return core::MatchEventType::Yellow;
    }
    if (type == QLatin1StringView("red")) {
        return core::MatchEventType::Red;
    }
    if (type == QLatin1StringView("sub")) {
        return core::MatchEventType::Sub;
    }
    return std::nullopt;
}

} // namespace

MatchReportViewModel::MatchReportViewModel(QObject* parent)
    : QObject(parent), m_service(ServiceRegistry::instance().matchReport()),
      m_session(ServiceRegistry::instance().session()), m_ratings(new MatchRatingsModel(this)),
      m_timeline(new MatchTimelineModel(this)), m_shapes(new MatchShapesModel(this)) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    m_digitTimer.start();
}

qint64 MatchReportViewModel::matchId() const {
    return m_report ? m_report->match.match.id.value : 0;
}

bool MatchReportViewModel::hasReport() const { return m_report.has_value(); }

QString MatchReportViewModel::opponentName() const {
    return m_report ? QString::fromStdString(m_report->match.opponentName) : QString{};
}

QString MatchReportViewModel::date() const {
    return m_report ? IsoDates::toIso(m_report->match.match.date) : QString{};
}

QString MatchReportViewModel::dateText() const {
    return m_report ? IsoDates::displayText(m_report->match.match.date) : QString{};
}

QString MatchReportViewModel::competition() const {
    return m_report ? QString::fromStdString(m_report->match.match.competition.value_or(""))
                    : QString{};
}

QString MatchReportViewModel::venueText() const {
    return m_report ? CalendarTexts::venueName(m_report->match.match.venue) : QString{};
}

QString MatchReportViewModel::formatText() const {
    return m_report && m_report->fromMatchDay ? MatchDayTexts::formatText(m_report->format)
                                              : QString{};
}

int MatchReportViewModel::goalsFor() const {
    return m_report ? m_report->match.match.goalsFor.value_or(0) : 0;
}

int MatchReportViewModel::goalsAgainst() const {
    return m_report ? m_report->match.match.goalsAgainst.value_or(0) : 0;
}

QString MatchReportViewModel::resultKey() const {
    if (!m_report || notFinished()) {
        return {};
    }
    const int difference = goalsFor() - goalsAgainst();
    return difference > 0   ? QStringLiteral("win")
           : difference < 0 ? QStringLiteral("loss")
                            : QStringLiteral("draw");
}

QString MatchReportViewModel::resultText() const {
    const QString key = resultKey();
    if (key == QLatin1StringView("win")) {
        return tr("Win");
    }
    if (key == QLatin1StringView("loss")) {
        return tr("Loss");
    }
    if (key == QLatin1StringView("draw")) {
        return tr("Draw");
    }
    return {};
}

bool MatchReportViewModel::notFinished() const {
    return m_report && m_report->match.match.status != core::MatchStatus::Finished;
}

bool MatchReportViewModel::readOnly() const {
    return !m_session.isAllowed(core::Permission::EditLineup) || notFinished();
}

QString MatchReportViewModel::readOnlyText() const {
    return tr("Only the coach or an assistant can rate the players and correct the match.");
}

bool MatchReportViewModel::correctable() const {
    return m_report && m_report->correctable && !readOnly();
}

QString MatchReportViewModel::notCorrectableText() const {
    return tr("The stored minutes of this match do not follow from its events, so events cannot "
              "be added or deleted here. Ratings, RPE and notes can still be entered.");
}

MatchRatingsModel* MatchReportViewModel::ratings() const { return m_ratings; }

MatchTimelineModel* MatchReportViewModel::timeline() const { return m_timeline; }

MatchShapesModel* MatchReportViewModel::shapes() const { return m_shapes; }

QString MatchReportViewModel::ratedText() const {
    const auto rated = std::ranges::count_if(
        m_rows, [](const Row& row) { return row.rating && *row.rating >= kMinRating; });
    return tr("%1 of %2 rated").arg(rated).arg(m_rows.size());
}

QString MatchReportViewModel::note() const { return m_note; }

int MatchReportViewModel::noteMaxLength() const {
    return services::MatchReportService::kMaxReportNoteLength;
}

bool MatchReportViewModel::dirty() const { return m_dirty; }

QVariantList MatchReportViewModel::eventTypeOptions() const {
    QVariantList options;
    for (const auto type :
         {core::MatchEventType::Goal, core::MatchEventType::OpponentGoal,
          core::MatchEventType::Yellow, core::MatchEventType::Red, core::MatchEventType::Sub}) {
        options.append(
            choice(MatchDayTexts::eventKey(type, false), MatchDayTexts::eventName(type, false)));
    }
    return options;
}

QVariantList MatchReportViewModel::notePlayerOptions() const {
    QVariantList options{choice(0, tr("No player"))};
    if (!m_report) {
        return options;
    }
    std::vector<core::PlayerId> players;
    for (const core::Appearance& appearance : m_report->appearances) {
        players.push_back(appearance.playerId);
    }
    if (m_report->fromMatchDay) {
        for (const core::PlayerId player : m_report->sheet.bench) {
            if (std::ranges::find(players, player) == players.end()) {
                players.push_back(player);
            }
        }
    }
    for (const core::PlayerId player : players) {
        options.append(choice(QVariant::fromValue(player.value), playerOption(player)));
    }
    return options;
}

QString MatchReportViewModel::lastMinute() const {
    return m_report ? MatchDayTexts::minuteFieldText({std::max(0, m_report->finalMinute - 1)})
                    : QString{};
}

QString MatchReportViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void MatchReportViewModel::openMatch(qint64 matchId) { load(matchId, false); }

void MatchReportViewModel::reload() { load(matchId(), true); }

void MatchReportViewModel::load(qint64 id, bool keepChanges) {
    m_lastOne.reset();
    m_remembered.reset();
    if (id <= 0) {
        m_report.reset();
        m_rows.clear();
        m_note.clear();
        m_savedNote.clear();
        m_loadFailure.reset();
        rebuild();
        return;
    }
    auto read = m_service.report(core::MatchId{id});
    if (!read) {
        qCritical("MatchReportViewModel: %s", read.error().detail.c_str());
        m_report.reset();
        m_rows.clear();
        m_loadFailure = read.error();
        rebuild();
        return;
    }
    const bool sameMatch = keepChanges && m_report && m_report->match.match.id.value == id;
    std::vector<Row> rows;
    for (const core::Appearance& appearance : read->appearances) {
        Row row{appearance.playerId, appearance.rating, appearance.rpe, appearance.rating,
                appearance.rpe};
        if (sameMatch) {
            const auto old = std::ranges::find(m_rows, appearance.playerId, &Row::player);
            if (old != m_rows.end()) {
                // The user's changes stay; the saved values are those read now.
                if (old->rating != old->savedRating) {
                    row.rating = old->rating;
                }
                if (old->rpe != old->savedRpe) {
                    row.rpe = old->rpe;
                }
            }
        }
        rows.push_back(row);
    }
    const QString savedNote = QString::fromStdString(read->reportNote.value_or(""));
    if (!sameMatch || m_note == m_savedNote) {
        m_note = savedNote;
    }
    m_savedNote = savedNote;
    m_rows = std::move(rows);
    m_report = std::move(*read);
    m_loadFailure.reset();
    rebuild();
}

void MatchReportViewModel::rebuild() {
    rebuildRatings();
    QList<QVariantList> timelineRows;
    QList<QVariantList> shapeRows;
    if (m_report) {
        const MatchTimelineInput input{
            m_report->events,
            m_report->notes,
            m_report->tacticChanges,
            m_report->tacticChangeNames,
            [this](std::optional<core::PlayerId> player) { return playerName(player); },
            opponentName()};
        timelineRows = MatchTimelineRows::build(input);
        for (const services::ShapePeriod& period : m_report->shapes) {
            const int to = period.to ? period.to->minute : m_report->finalMinute;
            shapeRows.append(QVariantList{
                QString::fromStdString(period.formation),
                MatchDayTexts::minuteText(period.from.minute, period.from.addedTime),
                period.to ? MatchDayTexts::minuteText(period.to->minute, period.to->addedTime)
                          : tr("the end"),
                std::max(0, to - period.from.minute)});
        }
    }
    m_timeline->setRows(std::move(timelineRows));
    m_shapes->setRows(std::move(shapeRows));
    emit matchChanged();
    emit textsChanged();
    emit noteChanged();
    emit loadErrorChanged();
    updateDirty();
}

void MatchReportViewModel::rebuildRatings() {
    QList<QVariantList> rows;
    if (m_report) {
        for (std::size_t index = 0; index < m_rows.size(); ++index) {
            const Row& row = m_rows[index];
            const core::Appearance& appearance = m_report->appearances[index];
            int goals = 0;
            int assists = 0;
            int yellows = 0;
            int reds = 0;
            for (const core::MatchEvent& event : m_report->events) {
                const bool own = event.playerId == row.player;
                switch (event.type) {
                case core::MatchEventType::Goal:
                    goals += own ? 1 : 0;
                    assists += event.relatedPlayerId == row.player ? 1 : 0;
                    break;
                case core::MatchEventType::Assist:
                    assists += own ? 1 : 0;
                    break;
                case core::MatchEventType::Yellow:
                    yellows += own ? 1 : 0;
                    break;
                case core::MatchEventType::Red:
                    reds += own ? 1 : 0;
                    break;
                case core::MatchEventType::Sub:
                case core::MatchEventType::OpponentGoal:
                    break;
                }
            }
            const int minutes = appearance.minuteOn && appearance.minuteOff
                                    ? std::max(0, *appearance.minuteOff - *appearance.minuteOn)
                                    : 0;
            const auto player = std::ranges::find(m_report->players, row.player, &core::Player::id);
            const bool known = player != m_report->players.end();
            rows.append(QVariantList{
                QVariant::fromValue(row.player.value),
                known ? optionalInt(player->shirtNumber) : QVariant{}, playerName(row.player),
                appearance.position ? SquadTexts::positionCode(*appearance.position) : QString{},
                appearance.started, minutes, tr("%1'").arg(minutes), goals, assists, yellows, reds,
                optionalInt(row.rating), optionalInt(row.rpe), rowError(row),
                row.rating != row.savedRating || row.rpe != row.savedRpe});
        }
    }
    m_ratings->setRows(std::move(rows));
}

void MatchReportViewModel::updateDirty() {
    const bool dirty = m_note != m_savedNote || std::ranges::any_of(m_rows, [](const Row& row) {
                           return row.rating != row.savedRating || row.rpe != row.savedRpe;
                       });
    m_dirty = dirty;
    emit dirtyChanged();
}

bool MatchReportViewModel::isEditable(int row, int column) const {
    return !readOnly() && row >= 0 && row < static_cast<int>(m_rows.size()) &&
           (column == MatchRatingsModel::RatingColumn || column == MatchRatingsModel::RpeColumn);
}

void MatchReportViewModel::setCell(int row, int column, std::optional<int> value) {
    Row& target = m_rows[static_cast<std::size_t>(row)];
    (column == MatchRatingsModel::RatingColumn ? target.rating : target.rpe) = value;
    rebuildRatings();
    updateDirty();
}

void MatchReportViewModel::enterDigit(int row, int column, int digit) {
    enterDigitAt(row, column, digit, m_digitTimer.elapsed());
}

void MatchReportViewModel::enterDigitAt(int row, int column, int digit, qint64 elapsedMs) {
    if (digit < 0 || digit > 9 || !isEditable(row, column)) {
        return;
    }
    const bool makesTen = digit == 0 && m_lastOne && m_lastOne->row == row &&
                          m_lastOne->column == column &&
                          elapsedMs - m_lastOne->elapsedMs <= kTenWindowMs;
    m_lastOne = digit == 1 ? std::optional(LastOne{row, column, elapsedMs}) : std::nullopt;
    setCell(row, column, makesTen ? kMaxRating : digit);
}

void MatchReportViewModel::clearCell(int row, int column) {
    if (!isEditable(row, column)) {
        return;
    }
    m_lastOne.reset();
    setCell(row, column, std::nullopt);
}

void MatchReportViewModel::rememberCell(int row, int column) {
    m_lastOne.reset();
    if (row < 0 || row >= static_cast<int>(m_rows.size())) {
        m_remembered.reset();
        return;
    }
    const Row& target = m_rows[static_cast<std::size_t>(row)];
    m_remembered = RememberedCell{
        row, column, column == MatchRatingsModel::RatingColumn ? target.rating : target.rpe};
}

void MatchReportViewModel::revertCell(int row, int column) {
    m_lastOne.reset();
    if (!isEditable(row, column) || !m_remembered || m_remembered->row != row ||
        m_remembered->column != column) {
        return;
    }
    setCell(row, column, m_remembered->value);
}

void MatchReportViewModel::setNote(const QString& text) {
    if (text == m_note || readOnly()) {
        return;
    }
    m_note = text;
    emit noteChanged();
    updateDirty();
}

QString MatchReportViewModel::rowError(const Row& row) const {
    if (row.rating && (*row.rating < kMinRating || *row.rating > kMaxRating)) {
        return tr("Rate from 1 to 10: type 1 and 0 quickly for 10.");
    }
    if (row.rpe && (*row.rpe < 0 || *row.rpe > kMaxRpe)) {
        return tr("Enter an RPE from 0 to 10.");
    }
    return {};
}

int MatchReportViewModel::firstErrorRow() const {
    for (std::size_t index = 0; index < m_rows.size(); ++index) {
        if (!rowError(m_rows[index]).isEmpty()) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

bool MatchReportViewModel::save() {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    if (firstErrorRow() >= 0) {
        emit errorOccurred(tr("Some ratings are not valid. Correct the marked rows first."));
        return false;
    }
    std::vector<services::RatingEntry> entries;
    entries.reserve(m_rows.size());
    for (const Row& row : m_rows) {
        entries.push_back({row.player, row.rating, row.rpe});
    }
    const auto result = m_service.save(m_report->match.match.id, entries, m_note.toStdString());
    if (!result) {
        report(result.error());
        return false;
    }
    load(matchId(), false);
    emit saved();
    return true;
}

void MatchReportViewModel::discardChanges() { load(matchId(), false); }

std::optional<core::MatchTime> MatchReportViewModel::parseMinute(const QString& minute,
                                                                 QString& error) const {
    if (!m_report) {
        return std::nullopt;
    }
    const auto shown = core::parseDisplayMinute(minute.toStdString());
    if (!shown) {
        error = tr("Enter the minute as 23, or 45+2 in stoppage time.");
        return std::nullopt;
    }
    const auto time = core::matchTimeOf(*shown, m_report->format);
    if (!time || time->minute > m_report->finalMinute) {
        error = tr("This minute is outside the match.");
        return std::nullopt;
    }
    return time;
}

QString MatchReportViewModel::minuteError(const QString& minute) const {
    QString error;
    static_cast<void>(parseMinute(minute, error));
    return error;
}

QVariantList MatchReportViewModel::playerOptions(const QString& type, const QString& minute,
                                                 const QString& role) const {
    QVariantList options;
    const auto eventType = eventTypeOf(type);
    QString error;
    const auto time = parseMinute(minute, error);
    if (!m_report || !eventType || !time || *eventType == core::MatchEventType::OpponentGoal) {
        return options;
    }
    const auto situation = m_service.situationAt(m_report->match.match.id, *time);
    if (!situation) {
        return options;
    }
    const bool second = role == QLatin1StringView("second");
    if (second && *eventType == core::MatchEventType::Goal) {
        options.append(choice(0, tr("No assist")));
    }
    if (second && *eventType != core::MatchEventType::Goal &&
        *eventType != core::MatchEventType::Sub) {
        return options;
    }
    const core::PlayerMatchState wanted = *eventType == core::MatchEventType::Sub && !second
                                              ? core::PlayerMatchState::Bench
                                              : core::PlayerMatchState::OnPitch;
    for (const core::PlayerInMatch& player : situation->players) {
        if (player.state == wanted) {
            options.append(
                choice(QVariant::fromValue(player.player.value), playerOption(player.player)));
        }
    }
    return options;
}

std::optional<core::MatchEventDraft> MatchReportViewModel::draftOf(const QString& type,
                                                                   const QString& minute,
                                                                   qint64 player,
                                                                   qint64 related) const {
    const auto eventType = eventTypeOf(type);
    QString error;
    const auto time = parseMinute(minute, error);
    if (!eventType || !time) {
        return std::nullopt;
    }
    core::MatchEventDraft draft;
    draft.type = *eventType;
    draft.minute = time->minute;
    draft.addedTime = time->addedTime;
    if (*eventType != core::MatchEventType::OpponentGoal && player > 0) {
        draft.playerId = core::PlayerId{player};
    }
    if ((*eventType == core::MatchEventType::Goal || *eventType == core::MatchEventType::Sub) &&
        related > 0) {
        draft.relatedPlayerId = core::PlayerId{related};
    }
    return draft;
}

QString MatchReportViewModel::conflictText(const services::EventConflict& conflict) const {
    if (conflict.laterEvent) {
        return tr("This event conflicts with a later one: %1. Delete or correct that event first.")
            .arg(eventText(*conflict.laterEvent));
    }
    return ErrorPresenter::invalidInputMessage(
        std::string(services::eventProblemField(conflict.problem)));
}

QString MatchReportViewModel::eventError(const QString& type, const QString& minute, qint64 player,
                                         qint64 related) const {
    if (!m_report) {
        return {};
    }
    QString error;
    if (!parseMinute(minute, error)) {
        return error;
    }
    const auto draft = draftOf(type, minute, player, related);
    if (!draft) {
        return tr("Choose the kind of event.");
    }
    const auto found = m_service.checkEvent(m_report->match.match.id, *draft);
    if (!found) {
        return ErrorPresenter::message(found.error());
    }
    return *found ? conflictText(**found) : QString{};
}

bool MatchReportViewModel::addEvent(const QString& type, const QString& minute, qint64 player,
                                    qint64 related) {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    const auto draft = draftOf(type, minute, player, related);
    if (!draft) {
        QString error;
        static_cast<void>(parseMinute(minute, error));
        emit errorOccurred(error.isEmpty() ? tr("Choose the kind of event.") : error);
        return false;
    }
    const core::MatchId match = m_report->match.match.id;
    if (const auto found = m_service.checkEvent(match, *draft); found && *found) {
        emit errorOccurred(conflictText(**found));
        return false;
    }
    const auto added = m_service.addEvent(match, *draft);
    if (!added) {
        report(added.error());
        return false;
    }
    afterChange();
    return true;
}

bool MatchReportViewModel::eventRemovesGoal(qint64 eventId) const {
    if (!m_report) {
        return false;
    }
    const auto found =
        std::ranges::find(m_report->events, core::MatchEventId{eventId}, &core::MatchEvent::id);
    return found != m_report->events.end() && (found->type == core::MatchEventType::Goal ||
                                               found->type == core::MatchEventType::OpponentGoal);
}

bool MatchReportViewModel::deleteEvent(qint64 eventId) {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    const core::MatchId match = m_report->match.match.id;
    const core::MatchEventId id{eventId};
    if (const auto dependent = m_service.checkDeleteEvent(match, id); dependent && *dependent) {
        emit errorOccurred(tr("This event cannot be deleted: the later event %1 needs it. Delete "
                              "that event first.")
                               .arg(eventText(**dependent)));
        return false;
    }
    const auto found = std::ranges::find(m_report->events, id, &core::MatchEvent::id);
    const QString text = found != m_report->events.end() ? eventText(*found) : QString{};
    auto removed = m_service.deleteEvent(match, id);
    if (!removed) {
        report(removed.error());
        return false;
    }
    m_deleted = Deleted{std::move(*removed)};
    afterChange();
    emit deleted(tr("Deleted: %1").arg(text));
    return true;
}

bool MatchReportViewModel::addNote(const QString& minute, qint64 playerId, const QString& text) {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    QString error;
    const auto time = parseMinute(minute, error);
    if (!time) {
        emit errorOccurred(error);
        return false;
    }
    const std::optional<core::PlayerId> player =
        playerId > 0 ? std::optional{core::PlayerId{playerId}} : std::nullopt;
    const auto added =
        m_service.addNote(m_report->match.match.id, *time, player, text.toStdString());
    if (!added) {
        report(added.error());
        return false;
    }
    afterChange();
    return true;
}

bool MatchReportViewModel::updateNote(qint64 noteId, const QString& minute, qint64 playerId,
                                      const QString& text) {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    QString error;
    const auto time = parseMinute(minute, error);
    if (!time) {
        emit errorOccurred(error);
        return false;
    }
    const std::optional<core::PlayerId> player =
        playerId > 0 ? std::optional{core::PlayerId{playerId}} : std::nullopt;
    const auto updated = m_service.updateNote(m_report->match.match.id, core::MatchNoteId{noteId},
                                              *time, player, text.toStdString());
    if (!updated) {
        report(updated.error());
        return false;
    }
    afterChange();
    return true;
}

bool MatchReportViewModel::deleteNote(qint64 noteId) {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    auto removed = m_service.deleteNote(m_report->match.match.id, core::MatchNoteId{noteId});
    if (!removed) {
        report(removed.error());
        return false;
    }
    const QString text = QString::fromStdString(removed->text);
    m_deleted = Deleted{std::move(*removed)};
    afterChange();
    emit deleted(tr("Note deleted: %1")
                     .arg(text.size() > kNotePreview ? text.left(kNotePreview) + u'…' : text));
    return true;
}

bool MatchReportViewModel::deleteTacticChange(qint64 changeId) {
    if (!m_report || !checkAllowed()) {
        return false;
    }
    const auto found = std::ranges::find(
        m_report->tacticChanges, core::MatchTacticChangeId{changeId}, &core::MatchTacticChange::id);
    QString text;
    if (found != m_report->tacticChanges.end()) {
        const MatchTimelineInput input{m_report->events,
                                       m_report->notes,
                                       m_report->tacticChanges,
                                       m_report->tacticChangeNames,
                                       {},
                                       {}};
        text = MatchTimelineRows::changeText(
            input, static_cast<std::size_t>(found - m_report->tacticChanges.begin()));
    }
    auto removed =
        m_service.deleteTacticChange(m_report->match.match.id, core::MatchTacticChangeId{changeId});
    if (!removed) {
        report(removed.error());
        return false;
    }
    m_deleted = Deleted{std::move(*removed)};
    afterChange();
    emit deleted(tr("Change of shape deleted: %1").arg(text));
    return true;
}

void MatchReportViewModel::undoDelete() {
    if (!m_deleted || !m_report) {
        return;
    }
    const Deleted deleted = std::move(*m_deleted);
    m_deleted.reset();
    core::Status restored;
    if (const auto* events = std::get_if<std::vector<core::MatchEvent>>(&deleted)) {
        restored = m_service.restoreEvents(m_report->match.match.id, *events);
    } else if (const auto* note = std::get_if<core::MatchNote>(&deleted)) {
        if (auto id = m_service.restoreNote(*note); !id) {
            restored = std::unexpected(id.error());
        }
    } else if (const auto* change = std::get_if<core::MatchTacticChange>(&deleted)) {
        restored = m_service.restoreTacticChange(*change);
    }
    if (!restored) {
        report(restored.error());
    }
    afterChange();
}

void MatchReportViewModel::afterChange() { load(matchId(), true); }

QString MatchReportViewModel::playerName(std::optional<core::PlayerId> player) const {
    if (!m_report || !player) {
        return {};
    }
    const auto found = std::ranges::find(m_report->players, *player, &core::Player::id);
    return found == m_report->players.end() ? QString{} : SquadTexts::displayName(*found);
}

QString MatchReportViewModel::playerOption(core::PlayerId player) const {
    QString text = playerName(player);
    if (m_report) {
        const auto found = std::ranges::find(m_report->players, player, &core::Player::id);
        if (found != m_report->players.end() && found->shirtNumber) {
            text = QStringLiteral("%1 %2").arg(*found->shirtNumber).arg(text);
        }
    }
    return text;
}

QString MatchReportViewModel::eventText(const core::MatchEvent& event) const {
    bool secondYellow = false;
    if (m_report) {
        const std::vector<core::MatchEventDraft> drafts(m_report->events.begin(),
                                                        m_report->events.end());
        const std::vector<bool> reds = core::secondYellowReds(drafts);
        const auto found = std::ranges::find(m_report->events, event.id, &core::MatchEvent::id);
        if (found != m_report->events.end()) {
            secondYellow = reds[static_cast<std::size_t>(found - m_report->events.begin())];
        }
    }
    QString who;
    if (event.type == core::MatchEventType::Sub) {
        who = tr("%1 for %2").arg(playerName(event.playerId), playerName(event.relatedPlayerId));
    } else if (event.type == core::MatchEventType::OpponentGoal) {
        who = opponentName();
    } else {
        who = playerName(event.playerId);
    }
    return tr("%1 · %2 %3")
        .arg(MatchDayTexts::eventName(event.type, secondYellow), who,
             MatchDayTexts::minuteText(event.minute, event.addedTime));
}

bool MatchReportViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::EditLineup)) {
        return true;
    }
    report(core::Error{
        core::ErrorCode::PermissionDenied, "refused by the viewmodel: EditLineup is needed", {}});
    return false;
}

void MatchReportViewModel::report(const core::Error& error) {
    qCritical("MatchReportViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

bool MatchReportViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        rebuild();
    }
    return QObject::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
