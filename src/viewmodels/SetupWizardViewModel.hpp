#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::services {
class DemoDataService;
class SetupService;
} // namespace fdss::services

namespace fdss::viewmodels {

// The first-run wizard: welcome + language, club and team, coach account, summary. Holds what was
// entered through Back and Next, and checks each step before leaving it.
class SetupWizardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int currentStep READ currentStep NOTIFY currentStepChanged FINAL)
    Q_PROPERTY(int stepCount READ stepCount CONSTANT FINAL)
    Q_PROPERTY(QString clubName READ clubName WRITE setClubName NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString teamName READ teamName WRITE setTeamName NOTIFY fieldsChanged FINAL)
    // The database text of a core TeamCategory. With loadDemoData: "senior", read-only.
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY fieldsChanged FINAL)
    // With loadDemoData: the season of today, read-only.
    Q_PROPERTY(QString season READ season WRITE setSeason NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString coachName READ coachName WRITE setCoachName NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString pin READ pin WRITE setPin NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString pinRepeat READ pinRepeat WRITE setPinRepeat NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString clubNameError READ clubNameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString teamNameError READ teamNameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString seasonError READ seasonError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString coachNameError READ coachNameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString pinError READ pinError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString pinRepeatError READ pinRepeatError NOTIFY errorsChanged FINAL)
    // The summary step: start empty (default) or with the demo club. While it is on, category and
    // season show and save "senior" and the season of today; the values chosen before stay and
    // come back when it is turned off. The typed club and team names are kept either way.
    Q_PROPERTY(bool loadDemoData READ loadDemoData WRITE setLoadDemoData NOTIFY fieldsChanged FINAL)
    // The staff the demo adds, for the summary: [{ name, role, pin }], role translated.
    Q_PROPERTY(QVariantList demoStaff READ demoStaff NOTIFY demoStaffChanged FINAL)

public:
    explicit SetupWizardViewModel(QObject* parent = nullptr);

    [[nodiscard]] int currentStep() const;
    [[nodiscard]] int stepCount() const;
    [[nodiscard]] QString clubName() const;
    void setClubName(const QString& value);
    [[nodiscard]] QString teamName() const;
    void setTeamName(const QString& value);
    [[nodiscard]] QString category() const;
    void setCategory(const QString& value);
    [[nodiscard]] QString season() const;
    void setSeason(const QString& value);
    [[nodiscard]] QString coachName() const;
    void setCoachName(const QString& value);
    [[nodiscard]] QString pin() const;
    void setPin(const QString& value);
    [[nodiscard]] QString pinRepeat() const;
    void setPinRepeat(const QString& value);
    [[nodiscard]] QString clubNameError() const;
    [[nodiscard]] QString teamNameError() const;
    [[nodiscard]] QString seasonError() const;
    [[nodiscard]] QString coachNameError() const;
    [[nodiscard]] QString pinError() const;
    [[nodiscard]] QString pinRepeatError() const;
    [[nodiscard]] bool loadDemoData() const;
    void setLoadDemoData(bool value);
    [[nodiscard]] QVariantList demoStaff() const;

    // Checks the current step and moves on. Stays there and shows why when it is not valid.
    Q_INVOKABLE bool next();
    // Keeps what was entered and clears the errors.
    Q_INVOKABLE void back();
    // Checks every step, going to the first one that is not valid, then creates the club and logs
    // the Coach in. Emits completed(). With loadDemoData the demo club follows in its own
    // transaction; if that fails the club stays empty, errorOccurred() says so and completed() is
    // still emitted.
    Q_INVOKABLE bool create();

    // A season starts on July 1.
    [[nodiscard]] static QString defaultSeason(core::Date today);

signals:
    void currentStepChanged();
    void fieldsChanged();
    void errorsChanged();
    void demoStaffChanged();
    // The Coach is logged in; the view calls AppState.refresh().
    void completed();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] bool validateStep(int step);
    void setStep(int step);
    void setField(QString& field, const QString& value, QString& error);
    void report(const core::Error& error);
    // After the setup; a failure leaves the club empty and is reported, not returned.
    void loadDemo();

    services::SetupService& m_setup;
    services::DemoDataService& m_demo;
    // Of today; the demo data is dated around today, so it belongs to this season.
    QString m_currentSeason;

    int m_currentStep = 0;
    QString m_clubName;
    QString m_teamName;
    QString m_category;
    QString m_season;
    QString m_coachName;
    QString m_pin;
    QString m_pinRepeat;
    QString m_clubNameError;
    QString m_teamNameError;
    QString m_seasonError;
    QString m_coachNameError;
    QString m_pinError;
    QString m_pinRepeatError;
    bool m_loadDemoData = false;
};

} // namespace fdss::viewmodels
