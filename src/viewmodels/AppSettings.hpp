#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::viewmodels {

// Application-wide UI preferences persisted in QSettings.
class AppSettings : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool darkTheme READ darkTheme WRITE setDarkTheme NOTIFY darkThemeChanged FINAL)

public:
    explicit AppSettings(QObject* parent = nullptr);

    [[nodiscard]] bool darkTheme() const;
    void setDarkTheme(bool darkTheme);

signals:
    void darkThemeChanged();

private:
    bool m_darkTheme = true;
};

} // namespace fdss::viewmodels
