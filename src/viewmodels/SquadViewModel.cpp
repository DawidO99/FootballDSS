#include "viewmodels/SquadViewModel.hpp"

#include "core/domain/Ids.hpp"
#include "core/domain/Permissions.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "services/SquadService.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"
#include "viewmodels/SquadListModel.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <array>
#include <utility>

namespace fdss::viewmodels {

namespace {

const auto kAll = QStringLiteral("all");
const auto kAvailable = QStringLiteral("available");
const auto kUnavailable = QStringLiteral("unavailable");

struct PositionGroupKey {
    const char* key;
    core::PositionGroup group;
};

constexpr std::array kPositionGroups{
    PositionGroupKey{"goalkeeper", core::PositionGroup::Goalkeeper},
    PositionGroupKey{"defender", core::PositionGroup::Defender},
    PositionGroupKey{"midfielder", core::PositionGroup::Midfielder},
    PositionGroupKey{"forward", core::PositionGroup::Forward}};

QVariantMap option(const QString& value, const QString& text) {
    return QVariantMap{{QStringLiteral("value"), value}, {QStringLiteral("text"), text}};
}

QString positionGroupName(core::PositionGroup group) {
    switch (group) {
    case core::PositionGroup::Goalkeeper:
        return SquadViewModel::tr("Goalkeepers");
    case core::PositionGroup::Defender:
        return SquadViewModel::tr("Defenders");
    case core::PositionGroup::Midfielder:
        return SquadViewModel::tr("Midfielders");
    case core::PositionGroup::Forward:
        return SquadViewModel::tr("Forwards");
    }
    return {};
}

} // namespace

SquadViewModel::SquadViewModel(QObject* parent)
    : QObject(parent), m_squad(ServiceRegistry::instance().squad()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()), m_source(new SquadListModel(this)),
      m_filter(new SquadFilterModel(m_source, this)), m_statusGroup(kAll), m_positionGroup(kAll),
      m_sortRole(QStringLiteral("number")) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

qint64 SquadViewModel::teamId() const { return m_teamId; }

void SquadViewModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    reload();
}

SquadFilterModel* SquadViewModel::players() const { return m_filter; }

QString SquadViewModel::searchText() const { return m_searchText; }

void SquadViewModel::setSearchText(const QString& text) {
    if (text == m_searchText) {
        return;
    }
    m_searchText = text;
    m_filter->setSearchText(text);
    emit filtersChanged();
}

QString SquadViewModel::statusGroup() const { return m_statusGroup; }

void SquadViewModel::setStatusGroup(const QString& group) {
    if (group == m_statusGroup) {
        return;
    }
    if (group == kAll) {
        m_filter->setStatusGroup(StatusGroup::All);
    } else if (group == kAvailable) {
        m_filter->setStatusGroup(StatusGroup::Available);
    } else if (group == kUnavailable) {
        m_filter->setStatusGroup(StatusGroup::Unavailable);
    } else {
        qWarning("SquadViewModel: unknown status group '%s'", qUtf8Printable(group));
        return;
    }
    m_statusGroup = group;
    emit filtersChanged();
}

QString SquadViewModel::positionGroup() const { return m_positionGroup; }

void SquadViewModel::setPositionGroup(const QString& group) {
    if (group == m_positionGroup) {
        return;
    }
    if (group == kAll) {
        m_filter->setPositionGroup(std::nullopt);
    } else {
        const auto found = std::ranges::find_if(kPositionGroups, [&group](const auto& candidate) {
            return group == QLatin1StringView(candidate.key);
        });
        if (found == kPositionGroups.end()) {
            qWarning("SquadViewModel: unknown position group '%s'", qUtf8Printable(group));
            return;
        }
        m_filter->setPositionGroup(found->group);
    }
    m_positionGroup = group;
    emit filtersChanged();
}

bool SquadViewModel::showArchived() const { return m_showArchived; }

void SquadViewModel::setShowArchived(bool show) {
    if (show == m_showArchived) {
        return;
    }
    m_showArchived = show;
    m_filter->setShowArchived(show);
    emit filtersChanged();
}

bool SquadViewModel::filtersActive() const {
    return !m_searchText.trimmed().isEmpty() || m_statusGroup != kAll || m_positionGroup != kAll;
}

QString SquadViewModel::sortRole() const { return m_sortRole; }

bool SquadViewModel::sortAscending() const { return m_sortAscending; }

QVariantList SquadViewModel::statusGroupOptions() const {
    return {option(kAll, tr("All")), option(kAvailable, tr("Available")),
            option(kUnavailable, tr("Unavailable"))};
}

QVariantList SquadViewModel::positionGroupOptions() const {
    QVariantList options{option(kAll, tr("All positions"))};
    for (const PositionGroupKey& entry : kPositionGroups) {
        options.append(option(QString::fromLatin1(entry.key), positionGroupName(entry.group)));
    }
    return options;
}

QString SquadViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void SquadViewModel::reload() {
    const int currentYear = static_cast<int>(m_clock.today().year());
    if (m_teamId == 0 || !m_session.user()) {
        m_source->setPlayers({}, currentYear);
        m_loadFailure.reset();
    } else if (auto players = m_squad.listPlayers(core::TeamId{m_teamId})) {
        m_source->setPlayers(std::move(*players), currentYear);
        m_loadFailure.reset();
    } else {
        qCritical("SquadViewModel: %s", players.error().detail.c_str());
        m_source->setPlayers({}, currentYear);
        m_loadFailure = players.error();
    }
    emit loadErrorChanged();
    emit playersReloaded();
}

void SquadViewModel::sortBy(const QString& role, bool ascending) {
    const QHash<int, QByteArray> names = m_source->roleNames();
    const int sortRole = names.key(role.toUtf8(), -1);
    if (sortRole < 0) {
        qWarning("SquadViewModel: unknown sort role '%s'", qUtf8Printable(role));
        return;
    }
    m_sortRole = role;
    m_sortAscending = ascending;
    m_filter->sortByRole(sortRole, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    emit sortChanged();
}

void SquadViewModel::clearFilters() {
    setSearchText({});
    setStatusGroup(kAll);
    setPositionGroup(kAll);
}

QVariantMap SquadViewModel::rowAt(int row) const {
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

int SquadViewModel::rowOf(qint64 playerId) const {
    const int sourceRow = m_source->rowOf(playerId);
    return sourceRow < 0 ? -1 : m_filter->mapFromSource(m_source->index(sourceRow)).row();
}

bool SquadViewModel::archive(qint64 playerId) {
    return checkAllowed() && finish(m_squad.archivePlayer(core::PlayerId{playerId}));
}

bool SquadViewModel::unarchive(qint64 playerId) {
    return checkAllowed() && finish(m_squad.unarchivePlayer(core::PlayerId{playerId}));
}

bool SquadViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

bool SquadViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::EditSquad)) {
        return true;
    }
    report(core::Error{
        core::ErrorCode::PermissionDenied, "refused by the viewmodel: EditSquad is needed", {}});
    return false;
}

bool SquadViewModel::finish(const core::Status& status) {
    if (!status) {
        report(status.error());
        return false;
    }
    reload();
    return true;
}

void SquadViewModel::report(const core::Error& error) {
    // A shirt number already taken is a rule the user ran into; anything else is a real failure.
    if (error.code != core::ErrorCode::Conflict) {
        qCritical("SquadViewModel: %s", error.detail.c_str());
    }
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
