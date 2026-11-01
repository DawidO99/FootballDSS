#pragma once

#include "core/domain/DateTypes.hpp"
#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace fdss::services {
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
    // The database text of a core TeamCategory.
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY fieldsChanged FINAL)
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

    // Checks the current step and moves on. Stays there and shows why when it is not valid.
    Q_INVOKABLE bool next();
    // Keeps what was entered and clears the errors.
    Q_INVOKABLE void back();
    // Checks every step, going to the first one that is not valid, then creates the club and logs
    // the Coach in. Emits completed().
    Q_INVOKABLE bool create();

    // A season starts on July 1.
    [[nodiscard]] static QString defaultSeason(core::Date today);

signals:
    void currentStepChanged();
    void fieldsChanged();
    void errorsChanged();
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

    services::SetupService& m_setup;

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
};

} // namespace fdss::viewmodels
