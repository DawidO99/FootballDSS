#include "viewmodels/LoadViewModel.hpp"

#include "core/domain/Ids.hpp"
#include "core/domain/Permissions.hpp"
#include "services/LoadService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/LoadListModel.hpp"
#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QFuture>

#include <algorithm>
#include <array>
#include <map>
#include <utility>

namespace fdss::viewmodels {

namespace {

const auto kAll = QStringLiteral("all");

constexpr std::array kStatuses{core::LoadStatus::High, core::LoadStatus::Elevated,
                               core::LoadStatus::Normal, core::LoadStatus::Underloaded,
                               core::LoadStatus::NotEnoughData};

QVariantMap option(const QString& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

QString statusFilterName(core::LoadStatus status) {
    // The general word for grey: the filter holds both kinds of it.
    const std::vector reasons{core::LoadReason::NoRpe};
    return LoadTexts::statusName(core::LoadVerdict{status, reasons});
}

} // namespace

LoadViewModel::LoadViewModel(QObject* parent)
    : QObject(parent), m_load(ServiceRegistry::instance().load()),
      m_session(ServiceRegistry::instance().session()), m_source(new LoadListModel(this)),
      m_filter(new LoadFilterModel(m_source, this)), m_statusFilter(kAll),
      m_sortRole(QStringLiteral("statusKey")) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 LoadViewModel::teamId() const { return m_teamId; }

void LoadViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    reload();
}

LoadFilterModel* LoadViewModel::players() const { return m_filter; }

bool LoadViewModel::busy() const { return m_busy; }

QString LoadViewModel::statusFilter() const { return m_statusFilter; }

void LoadViewModel::setStatusFilter(const QString& filter) {
    if (filter == m_statusFilter) {
        return;
    }
    if (filter == kAll) {
        m_filter->setStatusFilter(std::nullopt);
    } else {
        const auto found = std::ranges::find_if(kStatuses, [&filter](core::LoadStatus status) {
            return LoadTexts::statusKey(status) == filter;
        });
        if (found == kStatuses.end()) {
            qWarning("LoadViewModel: unknown status filter '%s'", qUtf8Printable(filter));
            return;
        }
        m_filter->setStatusFilter(*found);
    }
    m_statusFilter = filter;
    emit filtersChanged();
}

QVariantList LoadViewModel::statusFilterOptions() const {
    const auto& rows = m_source->rows();
    QVariantList options{
        option(kAll, tr("%1 (%2)").arg(tr("All")).arg(static_cast<qsizetype>(rows.size())))};
    for (const core::LoadStatus status : kStatuses) {
        const auto count =
            std::ranges::count_if(rows, [status](const services::PlayerLoadRow& row) {
                return row.assessment.verdict.status == status;
            });
        options.append(option(LoadTexts::statusKey(status),
                              tr("%1 (%2)").arg(statusFilterName(status)).arg(count)));
    }
    return options;
}

bool LoadViewModel::filtersActive() const { return m_statusFilter != kAll; }

QString LoadViewModel::sortRole() const { return m_sortRole; }

bool LoadViewModel::sortAscending() const { return m_sortAscending; }

int LoadViewModel::playerCount() const { return m_source->rowCount(); }

QString LoadViewModel::teamHint() const {
    const auto& rows = m_source->rows();
    if (rows.empty() || std::ranges::any_of(rows, [](const services::PlayerLoadRow& row) {
            return row.assessment.verdict.status != core::LoadStatus::NotEnoughData;
        })) {
        return {};
    }
    // The player closest to a status says what the team still needs.
    const auto closest = std::ranges::min_element(rows, {}, [](const services::PlayerLoadRow& row) {
        return std::pair(row.assessment.rpeDaysMissing,
                         row.assessment.baselineFrom.value_or(core::kMaxSupportedDate));
    });
    return LoadTexts::hint(closest->assessment);
}

QString LoadViewModel::asOfText() const { return m_asOf ? LoadTexts::asOf(*m_asOf) : QString{}; }

QString LoadViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void LoadViewModel::reload() {
    const int request = ++m_request;
    // ViewAnalysis: every role, and nobody logged out. The service checks it again.
    if (m_teamId == 0 || !m_session.isAllowed(core::Permission::ViewAnalysis)) {
        clearRows(std::nullopt);
        return;
    }
    auto future = m_load.teamLoad(core::TeamId{m_teamId});
    if (!future) {
        qCritical("LoadViewModel: %s", future.error().detail.c_str());
        clearRows(future.error());
        return;
    }
    setBusy(true);
    future->then(this, [this, request](services::TeamLoad result) {
        if (request == m_request) {
            apply(std::move(result));
        }
    });
}

void LoadViewModel::sortBy(const QString& role, bool ascending) {
    const QHash<int, QByteArray> names = m_source->roleNames();
    const int sortRole = names.key(role.toUtf8(), -1);
    if (sortRole < 0) {
        qWarning("LoadViewModel: unknown sort role '%s'", qUtf8Printable(role));
        return;
    }
    m_sortRole = role;
    m_sortAscending = ascending;
    m_filter->sortByRole(sortRole, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    emit sortChanged();
}

void LoadViewModel::clearFilters() { setStatusFilter(kAll); }

QVariantMap LoadViewModel::rowAt(int row) const {
    if (row < 0 || row >= m_filter->rowCount()) {
        return {};
    }
    QVariantMap values;
    const QHash<int, QByteArray> names = m_source->roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        values.insert(QString::fromLatin1(it.value()), m_filter->index(row, 0).data(it.key()));
    }
    return values;
}

int LoadViewModel::rowOf(qint64 playerId) const {
    const int sourceRow = m_source->rowOf(playerId);
    return sourceRow < 0 ? -1 : m_filter->mapFromSource(m_source->index(sourceRow)).row();
}

void LoadViewModel::requestPlayer(qint64 playerId) {
    m_requestedPlayer = playerId;
    // Always after a fresh read: the rows on hand may be older than the player.
    reload();
}

bool LoadViewModel::eventFilter(QObject* watched, QEvent* event) {
    // Once per change of the language: the copies for every window and item would be thousands
    // with the demo club, each one rebuilding the table or the chart.
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit loadErrorChanged();
        emit textsChanged();
    }
    return QObject::eventFilter(watched, event);
}

void LoadViewModel::apply(services::TeamLoad result) {
    // The day of most rows with data, the later one on a tie: a player without an entry today
    // is as of yesterday while the others are as of today, or the other way round.
    std::map<core::Date, int> days;
    for (const services::PlayerLoadRow& row : result.players) {
        if (row.assessment.indicators.daysSinceFirstEntry) {
            ++days[row.assessment.asOf];
        }
    }
    m_asOf.reset();
    int most = 0;
    for (const auto& [day, count] : days) {
        if (count >= most) {
            most = count;
            m_asOf = day;
        }
    }
    m_source->setRows(std::move(result.players), result.thresholds, m_asOf);
    m_loadFailure.reset();
    setBusy(false);
    emit loadErrorChanged();
    emit optionsChanged();
    emit textsChanged();
    emit playersReloaded();
    resolveRequestedPlayer();
}

void LoadViewModel::clearRows(std::optional<core::Error> failure) {
    m_source->setRows({}, core::LoadThresholds{}, std::nullopt);
    m_asOf.reset();
    m_loadFailure = std::move(failure);
    setBusy(false);
    emit loadErrorChanged();
    emit optionsChanged();
    emit textsChanged();
    emit playersReloaded();
    resolveRequestedPlayer();
}

void LoadViewModel::resolveRequestedPlayer() {
    const qint64 player = std::exchange(m_requestedPlayer, 0);
    if (player == 0) {
        return;
    }
    if (m_source->rowOf(player) < 0) {
        emit playerNotInTable(
            tr("This player is not in the load table. Archived players are not shown."));
        return;
    }
    if (rowOf(player) < 0) {
        clearFilters();
    }
    emit playerRequested(player);
}

void LoadViewModel::setBusy(bool busy) {
    if (busy == m_busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

} // namespace fdss::viewmodels
