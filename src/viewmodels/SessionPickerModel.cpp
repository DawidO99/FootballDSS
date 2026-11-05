#include "viewmodels/SessionPickerModel.hpp"

#include "viewmodels/CalendarTexts.hpp"
#include "viewmodels/IsoDates.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QString>

#include <algorithm>
#include <utility>

namespace fdss::viewmodels {

SessionPickerModel::SessionPickerModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int SessionPickerModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_sessions.size());
}

QVariant SessionPickerModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const core::TrainingSession& session = m_sessions[static_cast<std::size_t>(index.row())];
    switch (role) {
    case ValueRole:
        return QVariant::fromValue<qint64>(session.id.value);
    case TextRole: {
        const QString time = IsoDates::timeToIso(session.startTime);
        const QString when = IsoDates::displayText(session.date);
        return QStringLiteral("%1 · %2").arg(
            time.isEmpty() ? when : QStringLiteral("%1 %2").arg(when, time),
            CalendarTexts::sessionTypeName(session.type));
    }
    default:
        return {};
    }
}

QHash<int, QByteArray> SessionPickerModel::roleNames() const {
    return {{ValueRole, "value"}, {TextRole, "text"}};
}

void SessionPickerModel::setSessions(std::vector<core::TrainingSession> sessions) {
    beginResetModel();
    m_sessions = std::move(sessions);
    endResetModel();
}

bool SessionPickerModel::contains(qint64 sessionId) const {
    return std::ranges::any_of(m_sessions, [sessionId](const core::TrainingSession& session) {
        return session.id.value == sessionId;
    });
}

qint64 SessionPickerModel::defaultSessionId(core::Date today) const {
    if (m_sessions.empty()) {
        return 0;
    }
    const auto found =
        std::ranges::find_if(m_sessions, [today](const core::TrainingSession& session) {
            return session.date <= today;
        });
    // Newest first, so the last one of the future sessions is the nearest.
    return found != m_sessions.end() ? found->id.value : m_sessions.back().id.value;
}

bool SessionPickerModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange && !m_sessions.empty()) {
        emit dataChanged(index(0), index(rowCount() - 1), {TextRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
