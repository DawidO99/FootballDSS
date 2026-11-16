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
    // Yellow cards up to a suspension (P12 decision 3): one threshold for every competition.
    // Read-only here: setYellowCardThreshold() checks ManageSettings.
    Q_PROPERTY(
        int yellowCardThreshold READ yellowCardThreshold NOTIFY yellowCardThresholdChanged FINAL)
    Q_PROPERTY(int minYellowCardThreshold READ minYellowCardThreshold CONSTANT FINAL)
    Q_PROPERTY(int maxYellowCardThreshold READ maxYellowCardThreshold CONSTANT FINAL)

public:
    explicit AppSettings(QObject* parent = nullptr);

    [[nodiscard]] bool darkTheme() const;
    void setDarkTheme(bool darkTheme);
    [[nodiscard]] int yellowCardThreshold() const;
    // Needs a logged-in user with ManageSettings (checked here, not only by the view: P12 review);
    // otherwise errorOccurred and false, and the value stays. Outside the range: clamped.
    Q_INVOKABLE bool setYellowCardThreshold(int threshold);
    [[nodiscard]] int minYellowCardThreshold() const;
    [[nodiscard]] int maxYellowCardThreshold() const;

signals:
    void darkThemeChanged();
    void yellowCardThresholdChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

private:
    bool m_darkTheme = true;
    int m_yellowCardThreshold;
};

} // namespace fdss::viewmodels
