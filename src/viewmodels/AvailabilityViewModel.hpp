#pragma once

#include "core/util/Error.hpp"
#include "viewmodels/AvailabilityListModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <map>
#include <optional>
#include <string>

#include "services/SquadService.hpp"

namespace fdss::services {
class Clock;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The availability entries of one player. Dates are ISO strings, an empty one means no date.
class AvailabilityViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no player, and an empty list.
    Q_PROPERTY(qint64 playerId READ playerId WRITE setPlayerId NOTIFY playerIdChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::AvailabilityListModel* entries READ entries CONSTANT FINAL)
    // [{ value, text }]
    Q_PROPERTY(QVariantList typeOptions READ typeOptions NOTIFY optionsChanged FINAL)
    // Main.qml builds the views anew for every login, so these hold for the life of the view.
    Q_PROPERTY(bool canEdit READ canEdit CONSTANT FINAL)
    Q_PROPERTY(bool canViewDetails READ canViewDetails CONSTANT FINAL)
    Q_PROPERTY(QString today READ today NOTIFY entriesChanged FINAL)
    // For the newest open injury or illness whose expected return has passed: what to do about
    // it. Empty when there is none (P19).
    Q_PROPERTY(QString overdueHint READ overdueHint NOTIFY overdueHintChanged FINAL)
    // Why the list could not be read. Empty: the list is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)
    Q_PROPERTY(QString fromDateError READ fromDateError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString expectedToError READ expectedToError NOTIFY errorsChanged FINAL)
    Q_PROPERTY(QString actualToError READ actualToError NOTIFY errorsChanged FINAL)

public:
    explicit AvailabilityViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 playerId() const;
    void setPlayerId(qint64 playerId);
    [[nodiscard]] AvailabilityListModel* entries() const;
    [[nodiscard]] QVariantList typeOptions() const;
    [[nodiscard]] bool canEdit() const;
    [[nodiscard]] bool canViewDetails() const;
    [[nodiscard]] QString today() const;
    [[nodiscard]] QString overdueHint() const;
    [[nodiscard]] QString loadError() const;
    [[nodiscard]] QString fromDateError() const;
    [[nodiscard]] QString expectedToError() const;
    [[nodiscard]] QString actualToError() const;

    Q_INVOKABLE void reload();
    // `type` is the database text of a core AvailabilityType. Without ViewAvailabilityDetails
    // `description` is not sent at all, so an update keeps the stored text. True when the change
    // was made; otherwise the error properties or errorOccurred say why.
    Q_INVOKABLE bool add(const QString& type, const QString& fromDate, const QString& expectedTo,
                         const QString& description);
    Q_INVOKABLE bool update(qint64 entryId, const QString& type, const QString& fromDate,
                            const QString& expectedTo, const QString& actualTo,
                            const QString& description);
    // actualTo is the first available day.
    Q_INVOKABLE bool markReturned(qint64 entryId, const QString& actualTo);
    // Deletes an entry made by mistake; undoRemove() brings the last one back, with the
    // description also when this role cannot read it (P19).
    Q_INVOKABLE bool remove(qint64 entryId);
    // False when nothing was deleted since the last undo.
    Q_INVOKABLE bool undoRemove();
    // What "Mark as returned" offers: today, but not before the start of the entry.
    Q_INVOKABLE QString defaultReturnDate(qint64 entryId) const;
    Q_INVOKABLE void clearErrors();

signals:
    void playerIdChanged();
    void optionsChanged();
    void entriesChanged();
    void overdueHintChanged();
    void loadErrorChanged();
    void errorsChanged();
    // After every change. The view reads the card and the squad list anew.
    void availabilityChanged();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Without EditAvailability: errorOccurred, and the service is not called. The service checks
    // the same again (AGENTS section 4).
    [[nodiscard]] bool checkAllowed();
    // False, with the error on the field, when `iso` is neither empty nor a date.
    [[nodiscard]] bool readDate(const QString& iso, const std::string& field,
                                std::optional<core::Date>& date);
    [[nodiscard]] bool fillDraft(const QString& type, const QString& fromDate,
                                 const QString& expectedTo, const QString& actualTo,
                                 const QString& description, core::AvailabilityDraft& draft);
    [[nodiscard]] bool finish(const core::Status& status);
    [[nodiscard]] QString errorText(const std::string& field) const;
    void report(const core::Error& error);

    services::SquadService& m_squad;
    services::Session& m_session;
    const services::Clock& m_clock;
    AvailabilityListModel* m_entries;

    qint64 m_playerId = 0;
    std::optional<services::DeletedAvailability> m_deleted;
    std::optional<core::Error> m_loadFailure;
    // By column name. Kept as errors, not as text, so the messages follow the language.
    std::map<std::string, core::Error> m_errors;
};

} // namespace fdss::viewmodels
