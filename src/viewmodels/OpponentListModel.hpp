#pragma once

#include "core/domain/Opponent.hpp"

#include <QAbstractListModel>
#include <QCollator>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// The opponents for a SelectField, in Polish alphabetical order whatever the language of the app:
// the repository sorts by name_key, which puts "Ł" after "Z".
class OpponentListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by a viewmodel")

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole { ValueRole = Qt::UserRole + 1, TextRole };

    explicit OpponentListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setOpponents(std::vector<core::Opponent> opponents);

private:
    QCollator m_collator;
    std::vector<core::Opponent> m_opponents;
};

} // namespace fdss::viewmodels
