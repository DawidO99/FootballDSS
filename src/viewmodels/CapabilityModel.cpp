#include "viewmodels/CapabilityModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/CapabilityService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <ranges>

namespace fdss::viewmodels {

namespace {

// Who may enter the data each capability waits for (P15 decision 7).
core::Permission enteredWith(core::CapabilityKind kind) {
    switch (kind) {
    case core::CapabilityKind::Squad:
        return core::Permission::EditSquad;
    case core::CapabilityKind::Matches:
    case core::CapabilityKind::Opponents:
        return core::Permission::EditCalendar;
    case core::CapabilityKind::Load:
        return core::Permission::EditTraining;
    case core::CapabilityKind::Ratings:
        return core::Permission::EditLineup;
    case core::CapabilityKind::EventData:
        return core::Permission::Import;
    }
    return core::Permission::ManageSettings;
}

QString sectionOf(core::CapabilityKind kind) {
    switch (kind) {
    case core::CapabilityKind::Squad:
        return QStringLiteral("squad");
    case core::CapabilityKind::Matches:
    case core::CapabilityKind::Ratings:
    case core::CapabilityKind::Opponents:
        return QStringLiteral("calendar");
    case core::CapabilityKind::Load:
        return QStringLiteral("training");
    case core::CapabilityKind::EventData:
        return QStringLiteral("import");
    }
    return QStringLiteral("dashboard");
}

} // namespace

CapabilityModel::CapabilityModel(QObject* parent)
    : QAbstractListModel(parent), m_service(ServiceRegistry::instance().capabilities()),
      m_session(ServiceRegistry::instance().session()) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int CapabilityModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant CapabilityModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    return value(m_rows[static_cast<std::size_t>(index.row())], role);
}

QHash<int, QByteArray> CapabilityModel::roleNames() const {
    return {{KeyRole, "key"},
            {TitleRole, "title"},
            {StateKeyRole, "stateKey"},
            {StateTextRole, "stateText"},
            {DescriptionRole, "description"},
            {HintRole, "hint"},
            {ActionSectionRole, "actionSection"},
            {ActionTextRole, "actionText"},
            {AskTextRole, "askText"}};
}

qint64 CapabilityModel::teamId() const { return m_teamId; }

void CapabilityModel::setTeamId(qint64 teamId) {
    if (teamId == m_teamId) {
        return;
    }
    m_teamId = teamId;
    emit teamIdChanged();
    reload();
}

bool CapabilityModel::showAdvanced() const { return m_showAdvanced; }

void CapabilityModel::setShowAdvanced(bool show) {
    if (show == m_showAdvanced) {
        return;
    }
    m_showAdvanced = show;
    emit showAdvancedChanged();
    reload();
}

QVariantMap CapabilityModel::locks() const {
    QVariantMap result;
    for (const core::Capability& capability : m_all) {
        if (capability.state == core::CapabilityState::Locked) {
            result.insert(key(capability.kind), value(capability, HintRole));
        }
    }
    return result;
}

QVariantMap CapabilityModel::byKey() const {
    QVariantMap result;
    for (const core::Capability& capability : m_all) {
        result.insert(key(capability.kind), values(capability));
    }
    return result;
}

bool CapabilityModel::ready() const { return m_ready; }

void CapabilityModel::reload() {
    std::vector<core::Capability> all;
    bool ready = false;
    // ViewAnalysis: every role, and nobody logged out. The service checks it again.
    if (m_teamId != 0 && m_session.isAllowed(core::Permission::ViewAnalysis)) {
        auto evaluated = m_service.evaluate(core::TeamId{m_teamId}, m_showAdvanced);
        if (evaluated) {
            all = std::move(*evaluated);
            ready = true;
        } else {
            qCritical("CapabilityModel: %s", evaluated.error().detail.c_str());
            emit errorOccurred(ErrorPresenter::message(evaluated.error()));
        }
    }
    if (ready == m_ready && all == m_all) {
        return;
    }
    beginResetModel();
    m_all = std::move(all);
    m_ready = ready;
    m_rows.clear();
    std::ranges::copy_if(m_all, std::back_inserter(m_rows), [](const core::Capability& item) {
        return item.state != core::CapabilityState::Hidden;
    });
    endResetModel();
    emit capabilitiesChanged();
}

QVariantMap CapabilityModel::capability(const QString& key) const {
    const core::Capability* found = find(key);
    return found == nullptr ? QVariantMap{} : values(*found);
}

QVariantMap CapabilityModel::values(const core::Capability& capability) const {
    QVariantMap result;
    const QHash<int, QByteArray> names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) {
        result.insert(QString::fromLatin1(it.value()), value(capability, it.key()));
    }
    return result;
}

QString CapabilityModel::key(core::CapabilityKind kind) {
    switch (kind) {
    case core::CapabilityKind::Squad:
        return QStringLiteral("squad");
    case core::CapabilityKind::Matches:
        return QStringLiteral("matches");
    case core::CapabilityKind::Load:
        return QStringLiteral("load");
    case core::CapabilityKind::Ratings:
        return QStringLiteral("ratings");
    case core::CapabilityKind::Opponents:
        return QStringLiteral("opponents");
    case core::CapabilityKind::EventData:
        return QStringLiteral("eventData");
    }
    return {};
}

bool CapabilityModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        if (!m_rows.empty()) {
            emit dataChanged(index(0), index(rowCount() - 1));
        }
        emit capabilitiesChanged();
    }
    return QAbstractListModel::eventFilter(watched, event);
}

const core::Capability* CapabilityModel::find(const QString& key) const {
    const auto found = std::ranges::find_if(m_all, [&key](const core::Capability& item) {
        return CapabilityModel::key(item.kind) == key;
    });
    return found == m_all.end() ? nullptr : &*found;
}

bool CapabilityModel::canEnter(core::CapabilityKind kind) const {
    return m_session.isAllowed(enteredWith(kind));
}

QVariant CapabilityModel::value(const core::Capability& capability, int role) const {
    const core::CapabilityKind kind = capability.kind;
    const bool locked = capability.state != core::CapabilityState::Available;
    switch (role) {
    case KeyRole:
        return key(kind);
    case TitleRole:
        switch (kind) {
        case core::CapabilityKind::Squad:
            return tr("Squad");
        case core::CapabilityKind::Matches:
            return tr("Matches");
        case core::CapabilityKind::Load:
            return tr("Load");
        case core::CapabilityKind::Ratings:
            return tr("Ratings");
        case core::CapabilityKind::Opponents:
            return tr("Opponents");
        case core::CapabilityKind::EventData:
            return tr("Event data");
        }
        return {};
    case StateKeyRole:
        switch (capability.state) {
        case core::CapabilityState::Available:
            return QStringLiteral("available");
        case core::CapabilityState::Locked:
            return QStringLiteral("locked");
        case core::CapabilityState::Hidden:
            return QStringLiteral("hidden");
        }
        return {};
    case StateTextRole:
        return locked ? tr("Locked") : tr("Available");
    case DescriptionRole:
        switch (kind) {
        case core::CapabilityKind::Squad:
            return tr("Tactics board and lineup validator");
        case core::CapabilityKind::Matches:
            return tr("Match report, form and minutes");
        case core::CapabilityKind::Load:
            return tr("Load signals and trends");
        case core::CapabilityKind::Ratings:
            return tr("Lineup assistant");
        case core::CapabilityKind::Opponents:
            return tr("Opponent profile");
        case core::CapabilityKind::EventData:
            return tr("Pro analysis: shot map and heatmaps");
        }
        return {};
    case HintRole: {
        // Counted texts put the number after a colon, so they need no plural forms (as LoadTexts).
        const int missing = capability.missing();
        if (!locked) {
            return capability.daysToFullLoad
                       ? tr("Days until the full acute:chronic comparison: %1.")
                             .arg(*capability.daysToFullLoad)
                       : QString{};
        }
        switch (kind) {
        case core::CapabilityKind::Squad:
            return tr("Add players to use the tactics board and the lineup validator. Players "
                      "still needed: %1.")
                .arg(missing);
        case core::CapabilityKind::Matches:
            return tr("Finish a match to see the match report, form and minutes. Matches still "
                      "needed: %1.")
                .arg(missing);
        case core::CapabilityKind::Load:
            return tr("Enter RPE after sessions to see load signals and trends. Days with RPE "
                      "still needed: %1.")
                .arg(missing);
        case core::CapabilityKind::Ratings:
            return tr("Rate the players after a match, or enter attributes or custom metrics, to "
                      "use the lineup assistant.");
        case core::CapabilityKind::Opponents:
            return tr("Enter the results of finished matches to see opponent profiles. Matches "
                      "with a result still needed: %1.")
                .arg(missing);
        case core::CapabilityKind::EventData:
            return tr("Import an event data file (StatsBomb JSON) to unlock Pro analysis.");
        }
        return {};
    }
    case ActionSectionRole:
        return sectionOf(kind);
    case ActionTextRole:
        if (!locked || !canEnter(kind)) {
            return QString{};
        }
        switch (kind) {
        case core::CapabilityKind::Squad:
            return tr("Add players");
        case core::CapabilityKind::Load:
            return tr("Open training");
        case core::CapabilityKind::EventData:
            return tr("Open import");
        case core::CapabilityKind::Matches:
        case core::CapabilityKind::Ratings:
        case core::CapabilityKind::Opponents:
            return tr("Open the calendar");
        }
        return {};
    case AskTextRole:
        if (!locked || canEnter(kind)) {
            return QString{};
        }
        switch (kind) {
        case core::CapabilityKind::Squad:
            return tr("Ask the coach or the assistant to add the players.");
        case core::CapabilityKind::Matches:
            return tr("Ask the coach or the assistant to enter a finished match.");
        case core::CapabilityKind::Load:
            return tr("Ask the coach, the assistant or the physio to enter RPE after sessions.");
        case core::CapabilityKind::Ratings:
            return tr("Ask the coach or the assistant to rate the players after a match.");
        case core::CapabilityKind::Opponents:
            return tr("Ask the coach or the assistant to enter the results of finished matches.");
        case core::CapabilityKind::EventData:
            return tr("Ask the coach, the assistant or the analyst to import an event data file.");
        }
        return {};
    default:
        return {};
    }
}

} // namespace fdss::viewmodels
