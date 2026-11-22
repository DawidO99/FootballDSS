#include "viewmodels/LoadSettingsViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "services/LoadService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QSettings>

#include <optional>

namespace fdss::viewmodels {

namespace {

constexpr auto kRatioHighKey = "load/ratioHigh";
constexpr auto kRatioElevatedKey = "load/ratioElevated";
constexpr auto kRatioLowKey = "load/ratioLow";
constexpr auto kMonotonyHighKey = "load/monotonyHigh";
constexpr auto kGroup = "load";

constexpr auto kRatioHighField = "ratio_high";
constexpr auto kRatioElevatedField = "ratio_elevated";
constexpr auto kRatioLowField = "ratio_low";
constexpr auto kMonotonyHighField = "monotony_high";

// Two decimals at most, without trailing zeros: 1.5, 2, 0.75.
QString shown(double value) {
    QString text = LoadTexts::decimal(value, 2);
    while (text.size() > 1 && text.back() == u'0') {
        text.chop(1);
    }
    if (!text.isEmpty() && !text.back().isDigit()) {
        text.chop(1);
    }
    return text;
}

std::optional<double> parsed(const QString& text) {
    QString normalized = text.trimmed();
    normalized.replace(u',', u'.');
    bool ok = false;
    const double value = normalized.toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

} // namespace

LoadSettingsViewModel::LoadSettingsViewModel(QObject* parent)
    : QObject(parent), m_load(ServiceRegistry::instance().load()),
      m_session(ServiceRegistry::instance().session()) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

QString LoadSettingsViewModel::ratioHigh() const { return shown(m_load.thresholds().ratioHigh); }

QString LoadSettingsViewModel::ratioElevated() const {
    return shown(m_load.thresholds().ratioElevated);
}

QString LoadSettingsViewModel::ratioLow() const { return shown(m_load.thresholds().ratioLow); }

QString LoadSettingsViewModel::monotonyHigh() const {
    return shown(m_load.thresholds().monotonyHigh);
}

bool LoadSettingsViewModel::isDefault() const {
    return m_load.thresholds() == core::LoadThresholds{};
}

bool LoadSettingsViewModel::canEdit() const {
    return m_session.isAllowed(core::Permission::ManageSettings);
}

QString LoadSettingsViewModel::ratioHighError() const { return errorText(kRatioHighField); }

QString LoadSettingsViewModel::ratioElevatedError() const { return errorText(kRatioElevatedField); }

QString LoadSettingsViewModel::ratioLowError() const { return errorText(kRatioLowField); }

QString LoadSettingsViewModel::monotonyHighError() const { return errorText(kMonotonyHighField); }

bool LoadSettingsViewModel::save(const QString& ratioHigh, const QString& ratioElevated,
                                 const QString& ratioLow, const QString& monotonyHigh) {
    clearErrors();
    core::LoadThresholds thresholds = m_load.thresholds();
    const auto read = [&](const QString& text, const char* field, double& value) {
        if (const auto number = parsed(text)) {
            value = *number;
        } else {
            m_errors.insert(field);
        }
    };
    read(ratioHigh, kRatioHighField, thresholds.ratioHigh);
    read(ratioElevated, kRatioElevatedField, thresholds.ratioElevated);
    read(ratioLow, kRatioLowField, thresholds.ratioLow);
    read(monotonyHigh, kMonotonyHighField, thresholds.monotonyHigh);
    if (!m_errors.empty()) {
        emit errorsChanged();
        return false;
    }
    return apply(thresholds);
}

bool LoadSettingsViewModel::restoreDefaults() {
    clearErrors();
    return apply(core::LoadThresholds{});
}

void LoadSettingsViewModel::clearErrors() {
    if (m_errors.empty()) {
        return;
    }
    m_errors.clear();
    emit errorsChanged();
}

core::LoadThresholds LoadSettingsViewModel::storedThresholds() {
    const QSettings settings;
    core::LoadThresholds thresholds;
    thresholds.ratioHigh = settings.value(kRatioHighKey, thresholds.ratioHigh).toDouble();
    thresholds.ratioElevated =
        settings.value(kRatioElevatedKey, thresholds.ratioElevated).toDouble();
    thresholds.ratioLow = settings.value(kRatioLowKey, thresholds.ratioLow).toDouble();
    thresholds.monotonyHigh = settings.value(kMonotonyHighKey, thresholds.monotonyHigh).toDouble();
    return thresholds;
}

bool LoadSettingsViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit thresholdsChanged();
        emit errorsChanged();
    }
    return QObject::eventFilter(watched, event);
}

bool LoadSettingsViewModel::apply(const core::LoadThresholds& thresholds) {
    // Checked here and again by the service (AGENTS section 4).
    if (const auto allowed = m_session.require(core::Permission::ManageSettings); !allowed) {
        emit errorOccurred(ErrorPresenter::message(allowed.error()));
        return false;
    }
    if (const auto stored = m_load.setThresholds(thresholds); !stored) {
        const core::Error& error = stored.error();
        if (error.code == core::ErrorCode::InvalidInput && !error.field.empty()) {
            // The service names the first broken limit; the others follow from the same check.
            for (const core::ValidationError& invalid : core::validate(thresholds)) {
                m_errors.insert(invalid.field);
            }
            emit errorsChanged();
        } else {
            qCritical("LoadSettingsViewModel: %s", error.detail.c_str());
            emit errorOccurred(ErrorPresenter::message(error));
        }
        return false;
    }
    QSettings settings;
    if (thresholds == core::LoadThresholds{}) {
        settings.remove(kGroup);
    } else {
        settings.setValue(kRatioHighKey, thresholds.ratioHigh);
        settings.setValue(kRatioElevatedKey, thresholds.ratioElevated);
        settings.setValue(kRatioLowKey, thresholds.ratioLow);
        settings.setValue(kMonotonyHighKey, thresholds.monotonyHigh);
    }
    emit thresholdsChanged();
    return true;
}

QString LoadSettingsViewModel::errorText(const std::string& field) const {
    return m_errors.contains(field) ? ErrorPresenter::invalidInputMessage(field) : QString{};
}

} // namespace fdss::viewmodels
