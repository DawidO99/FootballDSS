#pragma once

#include "core/domain/Metric.hpp"
#include "core/util/Error.hpp"
#include "services/CalendarService.hpp"
#include "viewmodels/MetricDefinitionListModel.hpp"
#include "viewmodels/RowsModel.hpp"
#include "viewmodels/SessionPickerModel.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

namespace fdss::services {
class Clock;
class MetricService;
class Session;
struct MetricSheet;
} // namespace fdss::services

namespace fdss::viewmodels {

// The Metrics section (packet P17): one metric for one session, match or day, as a grid with one
// value cell per player. Reading again after a save emits dataChanged instead of a reset, so the
// delegates and the focus in the grid stay.
class MetricEntryViewModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team, and no grid. The view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    Q_PROPERTY(fdss::viewmodels::MetricDefinitionListModel* metrics READ metrics CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::SessionPickerModel* sessions READ sessions CONSTANT FINAL)
    Q_PROPERTY(fdss::viewmodels::ChoiceListModel* matches READ matches CONSTANT FINAL)
    // 0: no metric is open.
    Q_PROPERTY(qint64 metricId READ metricId NOTIFY metricChanged FINAL)
    // Database texts of the open metric; empty without one.
    Q_PROPERTY(QString scope READ scope NOTIFY metricChanged FINAL)
    Q_PROPERTY(QString valueType READ valueType NOTIFY metricChanged FINAL)
    // E.g. "Decimal number · s · 3–10 s · lower is better"; empty without a metric.
    Q_PROPERTY(QString metricHint READ metricHint NOTIFY metricChanged FINAL)
    // The open session or match; 0 for scope player or with none.
    Q_PROPERTY(qint64 targetId READ targetId NOTIFY targetChanged FINAL)
    // The day of the entry, ISO yyyy-MM-dd; empty without one.
    Q_PROPERTY(QString date READ date NOTIFY targetChanged FINAL)
    // A grid is open.
    Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY targetChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY targetChanged FINAL)
    // Without ManageMetrics.
    Q_PROPERTY(bool readOnly READ readOnly CONSTANT FINAL)
    // The user changed something, so leaving must ask.
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(bool canSave READ canSave NOTIFY dirtyChanged FINAL)
    // Why something could not be read; the view offers to try again. Empty: it is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    // QML uses the role names from roleNames(); the enum is for C++ callers.
    enum DataRole {
        PlayerIdRole = Qt::UserRole + 1,
        ShirtNumberRole, // int or nothing
        PlayerNameRole,
        ValueTextRole, // what the cell holds, as typed
        CheckedRole,   // a boolean metric: yes
        HasValueRole,
        // A boolean metric: "Yes", "No" or "Not entered"; empty for the others.
        StateTextRole,
        RowErrorRole, // translated, empty when the row is fine
        HasErrorRole
    };

    explicit MetricEntryViewModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] MetricDefinitionListModel* metrics() const;
    [[nodiscard]] SessionPickerModel* sessions() const;
    [[nodiscard]] ChoiceListModel* matches() const;
    [[nodiscard]] qint64 metricId() const;
    [[nodiscard]] QString scope() const;
    [[nodiscard]] QString valueType() const;
    [[nodiscard]] QString metricHint() const;
    [[nodiscard]] qint64 targetId() const;
    [[nodiscard]] QString date() const;
    [[nodiscard]] bool hasTarget() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] bool readOnly() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool canSave() const;
    [[nodiscard]] QString loadError() const;

    // Reads the metrics, the sessions, the matches and the open grid again. Changes that were not
    // saved stay.
    Q_INVOKABLE void reload();
    // Drops changes that were not saved and opens the default entry of the metric: the most recent
    // session or match on or before today, or today for scope player.
    Q_INVOKABLE void openMetric(qint64 metricId);
    // These drop changes that were not saved. One that does not fit the metric: errorOccurred.
    Q_INVOKABLE void openSession(qint64 sessionId);
    Q_INVOKABLE void openMatch(qint64 matchId);
    Q_INVOKABLE void openDate(const QString& isoDate);
    // A number as typed (decimal comma works); empty is no value. A wrong one stays as typed, with
    // the error in the row.
    Q_INVOKABLE void setValueText(int row, const QString& text);
    // A boolean metric.
    Q_INVOKABLE void setChecked(int row, bool checked);
    Q_INVOKABLE void clearValue(int row);
    // A cell got the focus: this is what revertCell() goes back to.
    Q_INVOKABLE void rememberCell(int row);
    // Esc.
    Q_INVOKABLE void revertCell(int row);
    // True when saved. Otherwise the rows or errorOccurred say why.
    Q_INVOKABLE bool save();
    Q_INVOKABLE void discardChanges();
    // The first row with an error, -1 when there is none.
    Q_INVOKABLE int firstErrorRow() const;

signals:
    void teamIdChanged();
    void metricChanged();
    void targetChanged();
    void dirtyChanged();
    void loadErrorChanged();
    void saved();
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Row {
        core::PlayerId playerId;
        std::optional<int> shirtNumber;
        QString name;
        QString text;
        // What the database holds.
        std::optional<double> stored;
        // Kept as an error, not as text, so the message follows the language.
        std::optional<core::Error> saveError;
    };

    [[nodiscard]] const core::MetricDefinition* metric() const;
    void refreshLists();
    void rebuildMatchRows();
    void openTarget(const core::MetricTarget& target);
    void openDefaultTarget();
    [[nodiscard]] bool loadSheet(const core::MetricTarget& target, core::Error& failure);
    void applySheet(services::MetricSheet sheet);
    void clearSheet();
    void setText(int row, const QString& text);
    void updateDirty();
    void setLoadFailure(std::optional<core::Error> failure);
    // The parsed value of the row; nullopt with `ok` false when the text does not parse.
    [[nodiscard]] std::optional<double> parsed(const Row& row, bool& ok) const;
    [[nodiscard]] QString rowError(const Row& row) const;
    [[nodiscard]] bool isEditable(int row) const;
    [[nodiscard]] bool checkAllowed();
    void report(const core::Error& error);

    services::MetricService& m_metricService;
    services::CalendarService& m_calendar;
    services::Session& m_session;
    const services::Clock& m_clock;
    MetricDefinitionListModel* m_metrics;
    SessionPickerModel* m_sessions;
    ChoiceListModel* m_matches;
    // Newest first, as the picker shows them.
    std::vector<services::CalendarMatch> m_matchItems;

    qint64 m_teamId = 0;
    qint64 m_metricId = 0;
    std::optional<core::MetricDefinition> m_metric;
    std::optional<core::MetricTarget> m_target;
    std::vector<Row> m_rows;
    bool m_dirty = false;
    std::optional<std::pair<int, QString>> m_remembered;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
