#include "viewmodels/AppSettings.hpp"

#include "core/algorithms/LineupValidator.hpp"
#include "core/domain/Permissions.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

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

bool AppSettings::setYellowCardThreshold(int threshold) {
    // The singleton also lives where no registry is installed (the component tests): nobody may
    // change the setting there.
    if (!ServiceRegistry::isInstalled() ||
        !ServiceRegistry::instance().session().isAllowed(core::Permission::ManageSettings)) {
        emit errorOccurred(ErrorPresenter::message(
            core::Error{core::ErrorCode::PermissionDenied, "yellow card threshold", {}}));
        return false;
    }
    const int clamped = clampedThreshold(threshold);
    if (clamped == m_yellowCardThreshold) {
        return true;
    }
    m_yellowCardThreshold = clamped;
    QSettings().setValue(kYellowCardThresholdKey, m_yellowCardThreshold);
    emit yellowCardThresholdChanged();
    return true;
}

int AppSettings::minYellowCardThreshold() const { return core::kMinYellowCardThreshold; }

int AppSettings::maxYellowCardThreshold() const { return core::kMaxYellowCardThreshold; }

} // namespace fdss::viewmodels
