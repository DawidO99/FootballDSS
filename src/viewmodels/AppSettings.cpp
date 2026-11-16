#include "viewmodels/AppSettings.hpp"

#include "core/algorithms/LineupValidator.hpp"

#include <QSettings>

#include <algorithm>

namespace fdss::viewmodels {

namespace {
constexpr auto kDarkThemeKey = "ui/darkTheme";
constexpr auto kYellowCardThresholdKey = "rules/yellowCardThreshold";

int clampedThreshold(int threshold) {
    return std::clamp(threshold, core::kMinYellowCardThreshold, core::kMaxYellowCardThreshold);
}
} // namespace

AppSettings::AppSettings(QObject* parent)
    : QObject(parent), m_darkTheme(QSettings().value(kDarkThemeKey, true).toBool()),
      m_yellowCardThreshold(clampedThreshold(
          QSettings().value(kYellowCardThresholdKey, core::kDefaultYellowCardThreshold).toInt())) {}

bool AppSettings::darkTheme() const { return m_darkTheme; }

void AppSettings::setDarkTheme(bool darkTheme) {
    if (m_darkTheme == darkTheme) {
        return;
    }
    m_darkTheme = darkTheme;
    QSettings().setValue(kDarkThemeKey, m_darkTheme);
    emit darkThemeChanged();
}

int AppSettings::yellowCardThreshold() const { return m_yellowCardThreshold; }

void AppSettings::setYellowCardThreshold(int threshold) {
    const int clamped = clampedThreshold(threshold);
    if (clamped == m_yellowCardThreshold) {
        return;
    }
    m_yellowCardThreshold = clamped;
    QSettings().setValue(kYellowCardThresholdKey, m_yellowCardThreshold);
    emit yellowCardThresholdChanged();
}

int AppSettings::minYellowCardThreshold() const { return core::kMinYellowCardThreshold; }

int AppSettings::maxYellowCardThreshold() const { return core::kMaxYellowCardThreshold; }

} // namespace fdss::viewmodels
