#pragma once

#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <map>
#include <string>
#include <vector>

namespace fdss::services {
class Clock;
class Session;
class SquadService;
} // namespace fdss::services

namespace fdss::viewmodels {

// The form of the add/edit player dialog.
class PlayerEditorViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // The team of a new player. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(bool adding READ adding NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString firstName READ firstName WRITE setFirstName NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString lastName READ lastName WRITE setLastName NOTIFY fieldsChanged FINAL)
    // An int or null. Signals of their own: a NumberField shown a value again drops the text it has
    // rejected, so its value must not be read anew when another field changes.
    Q_PROPERTY(
        QVariant shirtNumber READ shirtNumber WRITE setShirtNumber NOTIFY shirtNumberChanged FINAL)
    Q_PROPERTY(QVariant birthYear READ birthYear WRITE setBirthYear NOTIFY birthYearChanged FINAL)
    // The database text of a core Position; empty: not given ("—", P19).
    Q_PROPERTY(QString primaryPosition READ primaryPosition WRITE setPrimaryPosition NOTIFY
                   fieldsChanged FINAL)
    // A new primary position drops out of these.
    Q_PROPERTY(QStringList secondaryPositions READ secondaryPositions WRITE setSecondaryPositions
                   NOTIFY fieldsChanged FINAL)
    // The database text of a core PreferredFoot.
    Q_PROPERTY(QString foot READ foot WRITE setFoot NOTIFY fieldsChanged FINAL)
    Q_PROPERTY(QString notes READ notes WRITE setNotes NOTIFY fieldsChanged FINAL)
    // [{ value, text }]
    Q_PROPERTY(QVariantList positionOptions READ positionOptions NOTIFY optionsChanged FINAL)
    // positionOptions without the primary position.
    Q_PROPERTY(QVariantList secondaryPositionOptions READ secondaryPositionOptions NOTIFY
                   secondaryOptionsChanged FINAL)
    Q_PROPERTY(QVariantList footOptions READ footOptions NOTIFY optionsChanged FINAL)
    Q_PROPERTY(int minShirtNumber READ minShirtNumber CONSTANT FINAL)
    Q_PROPERTY(int maxShirtNumber READ maxShirtNumber CONSTANT FINAL)
    Q_PROPERTY(int minBirthYear READ minBirthYear CONSTANT FINAL)
    // Follows the year: read anew whenever the form starts, as the app may stay open over New Year.
    Q_PROPERTY(int maxBirthYear READ maxBirthYear NOTIFY maxBirthYearChanged FINAL)
    Q_PROPERTY(QString firstNameError READ firstNameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString lastNameError READ lastNameError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString shirtNumberError READ shirtNumberError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString birthYearError READ birthYearError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(
        QString secondaryPositionsError READ secondaryPositionsError NOTIFY errorsChanged FINAL)

public:
    explicit PlayerEditorViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] bool adding() const;
    [[nodiscard]] QString firstName() const;
    void setFirstName(const QString& name);
    [[nodiscard]] QString lastName() const;
    void setLastName(const QString& name);
    [[nodiscard]] QVariant shirtNumber() const;
    void setShirtNumber(const QVariant& number);
    [[nodiscard]] QVariant birthYear() const;
    void setBirthYear(const QVariant& year);
    [[nodiscard]] QString primaryPosition() const;
    void setPrimaryPosition(const QString& position);
    [[nodiscard]] QStringList secondaryPositions() const;
    void setSecondaryPositions(const QStringList& positions);
    [[nodiscard]] QString foot() const;
    void setFoot(const QString& foot);
    [[nodiscard]] QString notes() const;
    void setNotes(const QString& notes);
    [[nodiscard]] QVariantList positionOptions() const;
    [[nodiscard]] QVariantList secondaryPositionOptions() const;
    [[nodiscard]] QVariantList footOptions() const;
    [[nodiscard]] int minShirtNumber() const;
    [[nodiscard]] int maxShirtNumber() const;
    [[nodiscard]] int minBirthYear() const;
    [[nodiscard]] int maxBirthYear() const;
    [[nodiscard]] QString firstNameError() const;
    [[nodiscard]] QString lastNameError() const;
    [[nodiscard]] QString shirtNumberError() const;
    [[nodiscard]] QString birthYearError() const;
    [[nodiscard]] QString secondaryPositionsError() const;

    // Empty fields, no errors.
    Q_INVOKABLE void startAdding();
    // False when the player cannot be read; errorOccurred says why.
    Q_INVOKABLE bool startEditing(qint64 playerId);
    // True when saved. Otherwise the error properties or errorOccurred say why.
    Q_INVOKABLE bool save();

signals:
    void teamIdChanged();
    void fieldsChanged();
    void shirtNumberChanged();
    void birthYearChanged();
    void maxBirthYearChanged();
    void optionsChanged();
    void secondaryOptionsChanged();
    void errorsChanged();
    void saved(qint64 playerId);
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refreshLimits();
    void load(const core::PlayerDraft& draft, qint64 playerId);
    [[nodiscard]] core::PlayerDraft draft() const;
    void clearError(const std::string& field);
    [[nodiscard]] QString errorText(const std::string& field) const;
    void report(const core::Error& error);

    services::SquadService& m_squad;
    services::Session& m_session;
    const services::Clock& m_clock;

    qint64 m_teamId = 0;
    int m_maxBirthYear = 0;
    qint64 m_playerId = 0; // 0: adding
    QString m_firstName;
    QString m_lastName;
    QVariant m_shirtNumber;
    QVariant m_birthYear;
    std::optional<core::Position> m_primaryPosition;
    std::vector<core::Position> m_secondaryPositions;
    core::PreferredFoot m_foot = core::PreferredFoot::Unknown;
    QString m_notes;
    // By column name. Kept as errors, not as text, so the messages follow the language.
    std::map<std::string, core::Error> m_errors;
};

} // namespace fdss::viewmodels
