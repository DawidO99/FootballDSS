#pragma once

#include <QAbstractListModel>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

namespace fdss::viewmodels {

// Sidebar sections of the main window and the currently selected one.
class NavigationModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY currentIndexChanged FINAL)
    Q_PROPERTY(QVariantMap currentArgs READ currentArgs NOTIFY currentIndexChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum Role { KeyRole = Qt::UserRole + 1, TitleRole, IconNameRole };

    explicit NavigationModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int currentIndex() const;
    // Moving to another section this way (the sidebar) clears the arguments.
    void setCurrentIndex(int index);
    [[nodiscard]] QString currentKey() const;
    [[nodiscard]] QVariantMap currentArgs() const;

    // Selects the section with the given key and hands it the arguments; unknown keys are reported
    // and ignored. New arguments for the current section also emit currentIndexChanged, the notify
    // signal of currentArgs.
    Q_INVOKABLE void activate(const QString& key, const QVariantMap& args = {});

signals:
    void currentIndexChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Item {
        QString key;
        const char* title; // untranslated source text, translated in data()
        QString iconName;
    };

    std::vector<Item> m_items;
    int m_currentIndex = 0;
    QVariantMap m_currentArgs;
};

} // namespace fdss::viewmodels
