#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/domain/TrainingSession.hpp"

#include <QAbstractListModel>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// The sessions for the SelectField of the training sheet, newest first as the service gives them.
class SessionPickerModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole { ValueRole = Qt::UserRole + 1, TextRole };

    explicit SessionPickerModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setSessions(std::vector<core::TrainingSession> sessions);
    [[nodiscard]] bool contains(qint64 sessionId) const;
    // The most recent session on or before `today`; without one the nearest future session, because
    // "no sessions yet" would not be true. 0: there are no sessions.
    [[nodiscard]] qint64 defaultSessionId(core::Date today) const;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    std::vector<core::TrainingSession> m_sessions;
};

} // namespace fdss::viewmodels
