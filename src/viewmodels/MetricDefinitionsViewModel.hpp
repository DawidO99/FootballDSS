#pragma once

#include "core/domain/Metric.hpp"
#include "core/util/Error.hpp"
#include "viewmodels/MetricDefinitionListModel.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class MetricService;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// Settings -> Custom metrics (packet P17). Every role reads the list; changes need ManageMetrics,
// and the service refuses them too.
//
// The form of add() and update() is a map with the keys namePl, nameEn, valueType, unit, minText,
// maxText (as typed; decimal comma works), aggregation, scope (database texts) and higherIsBetter.
class MetricDefinitionsViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(
        fdss::viewmodels::MetricDefinitionListModel* definitions READ definitions CONSTANT FINAL)
    // Main.qml builds the views anew for every login, so this holds for the life of the view.
    Q_PROPERTY(bool canManage READ canManage CONSTANT FINAL)
    Q_PROPERTY(QVariantList valueTypeOptions READ valueTypeOptions NOTIFY optionsChanged FINAL)
    Q_PROPERTY(QVariantList scopeOptions READ scopeOptions NOTIFY optionsChanged FINAL)
    // Form key -> translated error of the last add() or update(): namePl, nameEn, unit, minText,
    // maxText, aggregation. A message without a field goes to errorOccurred.
    Q_PROPERTY(QVariantMap fieldErrors READ fieldErrors NOTIFY errorsChanged FINAL)
    // Why the list could not be read; the view offers to try again. Empty: the list is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)
    // A delete that undoRemove() can bring back.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged FINAL)

public:
    explicit MetricDefinitionsViewModel(QObject* parent = nullptr);

    [[nodiscard]] MetricDefinitionListModel* definitions() const;
    [[nodiscard]] bool canManage() const;
    [[nodiscard]] QVariantList valueTypeOptions() const;
    [[nodiscard]] QVariantList scopeOptions() const;
    [[nodiscard]] QVariantMap fieldErrors() const;
    [[nodiscard]] QString loadError() const;
    [[nodiscard]] bool canUndo() const;

    // [{ value, text }] of the aggregations a value type allows (database text of the type).
    Q_INVOKABLE QVariantList aggregationOptions(const QString& valueType) const;
    // A definition with values keeps its type and scope: the dialog disables them.
    Q_INVOKABLE bool isLocked(qint64 id) const;

    Q_INVOKABLE void reload();
    // True when the change was made; otherwise fieldErrors or errorOccurred say why.
    Q_INVOKABLE bool add(const QVariantMap& form);
    Q_INVOKABLE bool update(qint64 id, const QVariantMap& form);
    // Only without values, otherwise errorOccurred explains why. Emits removed() for the undo
    // toast.
    Q_INVOKABLE bool remove(qint64 id);
    Q_INVOKABLE bool undoRemove();
    Q_INVOKABLE void clearErrors();

signals:
    void optionsChanged();
    void errorsChanged();
    void loadErrorChanged();
    void canUndoChanged();
    // After every change, so the Metrics section and the player card can read again.
    void definitionsChanged();
    void removed(const QString& name);
    // A translated text for the user; the view shows it in a toast.
    void errorOccurred(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The draft of the form, or the fieldErrors of what does not parse.
    [[nodiscard]] std::optional<core::MetricDefinitionDraft> readForm(const QVariantMap& form);
    // Without ManageMetrics: errorOccurred, and the service is not called.
    [[nodiscard]] bool checkAllowed();
    [[nodiscard]] bool finish(const core::Status& status, const core::MetricDefinitionDraft& draft);
    void setFieldError(const QString& key, const QString& message);
    void report(const core::Error& error);

    services::MetricService& m_metrics;
    services::Session& m_session;
    MetricDefinitionListModel* m_definitions;
    bool m_canManage = false;
    std::optional<core::Error> m_loadFailure;
    QVariantMap m_fieldErrors;
    std::optional<core::MetricDefinition> m_removed;
};

} // namespace fdss::viewmodels
