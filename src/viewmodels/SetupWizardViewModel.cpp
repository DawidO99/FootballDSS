#include "viewmodels/SetupWizardViewModel.hpp"

#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Club.hpp"
#include "core/domain/Enums.hpp"
#include "core/domain/StaffUser.hpp"
#include "core/domain/Team.hpp"
#include "core/domain/Validation.hpp"
#include "services/AuthService.hpp"
#include "services/Clock.hpp"
#include "services/DemoDataService.hpp"
#include "services/SetupService.hpp"
#include "viewmodels/AppState.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <string>
#include <string_view>
#include <tuple>

namespace fdss::viewmodels {

namespace {

constexpr int kClubStep = 1;
constexpr int kCoachStep = 2;
constexpr int kSummaryStep = 3;
constexpr int kStepCount = 4;

bool hasError(const core::ValidationErrors& errors, std::string_view field) {
    return std::ranges::any_of(
        errors, [field](const core::ValidationError& error) { return error.field == field; });
}

// Empty when the field is not among the errors. The core reports "name" for a club, a team and a
// coach alike, so `message` says which one it is.
QString errorText(const core::ValidationErrors& errors, std::string_view field,
                  const std::string& message) {
    return hasError(errors, field) ? ErrorPresenter::invalidInputMessage(message) : QString{};
}

core::ClubDraft clubDraft(const QString& name) {
    core::ClubDraft draft;
    draft.name = name.toStdString();
    return draft;
}

core::TeamDraft teamDraft(const QString& name, const QString& season) {
    core::TeamDraft draft;
    draft.name = name.toStdString();
    draft.season = season.trimmed().toStdString();
    return draft;
}

} // namespace

SetupWizardViewModel::SetupWizardViewModel(QObject* parent)
    : QObject(parent), m_setup(ServiceRegistry::instance().setup()),
      m_demo(ServiceRegistry::instance().demoData()),
      m_category(QString::fromUtf8(core::toDbText(core::TeamCategory::Senior))),
      m_season(defaultSeason(ServiceRegistry::instance().clock().today())) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int SetupWizardViewModel::currentStep() const { return m_currentStep; }

int SetupWizardViewModel::stepCount() const { return kStepCount; }

QString SetupWizardViewModel::clubName() const { return m_clubName; }

void SetupWizardViewModel::setClubName(const QString& value) {
    setField(m_clubName, value, m_clubNameError);
}

QString SetupWizardViewModel::teamName() const { return m_teamName; }

void SetupWizardViewModel::setTeamName(const QString& value) {
    setField(m_teamName, value, m_teamNameError);
}

QString SetupWizardViewModel::category() const { return m_category; }

void SetupWizardViewModel::setCategory(const QString& value) {
    if (m_category != value) {
        m_category = value;
        emit fieldsChanged();
    }
}

QString SetupWizardViewModel::season() const { return m_season; }

void SetupWizardViewModel::setSeason(const QString& value) {
    setField(m_season, value, m_seasonError);
}

QString SetupWizardViewModel::coachName() const { return m_coachName; }

void SetupWizardViewModel::setCoachName(const QString& value) {
    setField(m_coachName, value, m_coachNameError);
}

QString SetupWizardViewModel::pin() const { return m_pin; }

void SetupWizardViewModel::setPin(const QString& value) { setField(m_pin, value, m_pinError); }

QString SetupWizardViewModel::pinRepeat() const { return m_pinRepeat; }

void SetupWizardViewModel::setPinRepeat(const QString& value) {
    setField(m_pinRepeat, value, m_pinRepeatError);
}

QString SetupWizardViewModel::clubNameError() const { return m_clubNameError; }

QString SetupWizardViewModel::teamNameError() const { return m_teamNameError; }

QString SetupWizardViewModel::seasonError() const { return m_seasonError; }

QString SetupWizardViewModel::coachNameError() const { return m_coachNameError; }

QString SetupWizardViewModel::pinError() const { return m_pinError; }

QString SetupWizardViewModel::pinRepeatError() const { return m_pinRepeatError; }

bool SetupWizardViewModel::loadDemoData() const { return m_loadDemoData; }

void SetupWizardViewModel::setLoadDemoData(bool value) {
    if (m_loadDemoData == value) {
        return;
    }
    m_loadDemoData = value;
    // The demo club is a senior B-klasa team.
    if (value) {
        m_category = QString::fromUtf8(core::toDbText(core::TeamCategory::Senior));
    }
    emit fieldsChanged();
}

QVariantList SetupWizardViewModel::demoStaff() const {
    QVariantList result;
    for (const core::demo::DemoStaff& member : core::demo::kDemoStaff) {
        result.append(QVariantMap{
            {QStringLiteral("name"),
             QString::fromUtf8(member.name.data(), static_cast<qsizetype>(member.name.size()))},
            {QStringLiteral("role"), AppState::roleDisplayName(member.role)},
            {QStringLiteral("pin"),
             QString::fromUtf8(core::demo::kDemoStaffPin.data(),
                               static_cast<qsizetype>(core::demo::kDemoStaffPin.size()))}});
    }
    return result;
}

bool SetupWizardViewModel::next() {
    if (!validateStep(m_currentStep)) {
        return false;
    }
    if (m_currentStep < kSummaryStep) {
        setStep(m_currentStep + 1);
    }
    return true;
}

void SetupWizardViewModel::back() {
    m_clubNameError.clear();
    m_teamNameError.clear();
    m_seasonError.clear();
    m_coachNameError.clear();
    m_pinError.clear();
    m_pinRepeatError.clear();
    emit errorsChanged();
    if (m_currentStep > 0) {
        setStep(m_currentStep - 1);
    }
}

bool SetupWizardViewModel::create() {
    for (int step = 0; step < kStepCount; ++step) {
        if (!validateStep(step)) {
            setStep(step);
            return false;
        }
    }
    const auto category = core::fromDbText<core::TeamCategory>(m_category.toStdString());
    if (!category) {
        report(category.error());
        return false;
    }
    core::TeamDraft team = teamDraft(m_teamName, m_season);
    team.category = m_loadDemoData ? core::TeamCategory::Senior : *category;
    const auto coach = m_setup.completeSetup(clubDraft(m_clubName), team, m_coachName.toStdString(),
                                             m_pin.toStdString());
    if (!coach) {
        report(coach.error());
        return false;
    }
    m_pin.clear();
    m_pinRepeat.clear();
    emit fieldsChanged();
    if (m_loadDemoData) {
        loadDemo();
    }
    emit completed();
    return true;
}

QString SetupWizardViewModel::defaultSeason(core::Date today) {
    const int year = static_cast<int>(today.year());
    const int start = today.month() >= std::chrono::July ? year : year - 1;
    return QStringLiteral("%1/%2").arg(start).arg((start + 1) % 100, 2, 10, QLatin1Char('0'));
}

void SetupWizardViewModel::loadDemo() {
    auto context = m_setup.clubContext();
    if (!context) {
        qCritical("SetupWizardViewModel: %s", context.error().detail.c_str());
        emit errorOccurred(tr("Demo data could not be loaded. The club starts empty."));
        return;
    }
    if (const auto loaded = m_demo.load(context->team.id); !loaded) {
        qCritical("SetupWizardViewModel: %s", loaded.error().detail.c_str());
        emit errorOccurred(tr("Demo data could not be loaded. The club starts empty."));
    }
}

bool SetupWizardViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit demoStaffChanged();
    }
    // The errors are texts made by the check, so the check makes them anew in the new language.
    if (event->type() == QEvent::LanguageChange &&
        !(m_clubNameError.isEmpty() && m_teamNameError.isEmpty() && m_seasonError.isEmpty() &&
          m_coachNameError.isEmpty() && m_pinError.isEmpty() && m_pinRepeatError.isEmpty())) {
        std::ignore = validateStep(m_currentStep);
    }
    return QObject::eventFilter(watched, event);
}

bool SetupWizardViewModel::validateStep(int step) {
    bool valid = true;
    if (step == kClubStep) {
        const auto club = core::validate(clubDraft(m_clubName));
        const auto team = core::validate(teamDraft(m_teamName, m_season));
        m_clubNameError = errorText(club, "name", "club_name");
        m_teamNameError = errorText(team, "name", "team_name");
        m_seasonError = errorText(team, "season", "season");
        valid = club.empty() && team.empty();
    } else if (step == kCoachStep) {
        core::StaffUserDraft coach;
        coach.name = m_coachName.toStdString();
        const auto errors = core::validate(coach);
        m_coachNameError = errorText(errors, "name", "coach_name");
        const bool pinValid = services::AuthService::isValidPin(m_pin.toStdString());
        m_pinError = pinValid ? QString{} : ErrorPresenter::invalidInputMessage("pin");
        m_pinRepeatError =
            m_pin == m_pinRepeat ? QString{} : ErrorPresenter::invalidInputMessage("pin_repeat");
        valid = errors.empty() && pinValid && m_pin == m_pinRepeat;
    }
    emit errorsChanged();
    return valid;
}

void SetupWizardViewModel::setStep(int step) {
    if (m_currentStep != step) {
        m_currentStep = step;
        emit currentStepChanged();
    }
}

void SetupWizardViewModel::setField(QString& field, const QString& value, QString& error) {
    if (field == value) {
        return;
    }
    field = value;
    emit fieldsChanged();
    if (!error.isEmpty()) {
        error.clear();
        emit errorsChanged();
    }
}

void SetupWizardViewModel::report(const core::Error& error) {
    qCritical("SetupWizardViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
