#include "viewmodels/AppSettings.hpp"

#include <QSettings>

namespace fdss::viewmodels {

namespace {
constexpr auto kDarkThemeKey = "ui/darkTheme";
} // namespace

AppSettings::AppSettings(QObject* parent)
    : QObject(parent), m_darkTheme(QSettings().value(kDarkThemeKey, true).toBool()) {}

bool AppSettings::darkTheme() const { return m_darkTheme; }

void AppSettings::setDarkTheme(bool darkTheme) {
    if (m_darkTheme == darkTheme) {
        return;
    }
    m_darkTheme = darkTheme;
    QSettings().setValue(kDarkThemeKey, m_darkTheme);
    emit darkThemeChanged();
}

} // namespace fdss::viewmodels
