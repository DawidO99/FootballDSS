#pragma once

#include "core/parsing/SquadImport.hpp"
#include "core/util/Error.hpp"
#include "services/import/ImportFile.hpp"
#include "services/import/ImportService.hpp"
#include "viewmodels/RowsModel.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

template <class T> class QFutureWatcher;

namespace fdss::services {
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The import wizard (SPEC section 8, packet P16): type -> file -> preview -> mapping -> validation
// report -> commit -> summary with undo. Needs Import; the service checks it again. Reading and
// validation run on the thread pool (busy, progress); a result of an older request is dropped.
// Nothing is written before commit().
class ImportWizardViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no team; the view binds it to AppState.teamId.
    Q_PROPERTY(qint64 teamId READ teamId WRITE setTeamId NOTIFY teamIdChanged FINAL)
    // "type", "file", "preview", "mapping", "report" or "summary".
    Q_PROPERTY(QString step READ step NOTIFY stepChanged FINAL)
    Q_PROPERTY(bool canImport READ canImport CONSTANT FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    // 0-1 while validating; -1 while the length is unknown (reading a file).
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    // Why the chosen file or sheet cannot be imported; empty when it can.
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged FINAL)
    // [{ value, text, description }] of the import types; only "squad" in P16.
    Q_PROPERTY(QVariantList typeOptions READ typeOptions NOTIFY textsChanged FINAL)

    // Preview.
    Q_PROPERTY(QString fileName READ fileName NOTIFY sourceChanged FINAL)
    // "CSV · Windows-1250 (Polish Excel) · semicolon · 20 rows", or "XLSX · 20 rows".
    Q_PROPERTY(QString sourceSummary READ sourceSummary NOTIFY sourceChanged FINAL)
    // [{ value, text }] of the sheets; empty for CSV.
    Q_PROPERTY(QVariantList sheetOptions READ sheetOptions NOTIFY sourceChanged FINAL)
    Q_PROPERTY(int sheetIndex READ sheetIndex NOTIFY sourceChanged FINAL)
    // [{ role, title }] and the first rows [{ rowNumber, c0, c1… }].
    Q_PROPERTY(QVariantList previewColumns READ previewColumns NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QVariantList previewRows READ previewRows NOTIFY sourceChanged FINAL)
    // Rows below the header, shown or not.
    Q_PROPERTY(int rowCount READ rowCount NOTIFY sourceChanged FINAL)
    // "This file was already imported on …" before commit; empty for a new file.
    Q_PROPERTY(QString duplicateWarning READ duplicateWarning NOTIFY sourceChanged FINAL)

    // Mapping. Rows: column, header, samples, field (a field key, "" = ignored), autoMatched.
    Q_PROPERTY(fdss::viewmodels::RowsModel* mapping READ mapping CONSTANT FINAL)
    Q_PROPERTY(QVariantList fieldOptions READ fieldOptions NOTIFY textsChanged FINAL)
    Q_PROPERTY(bool mappingRemembered READ mappingRemembered NOTIFY mappingChanged FINAL)
    Q_PROPERTY(bool nameOrderVisible READ nameOrderVisible NOTIFY mappingChanged FINAL)
    Q_PROPERTY(QString nameOrder READ nameOrder NOTIFY mappingChanged FINAL)
    Q_PROPERTY(QVariantList nameOrderOptions READ nameOrderOptions NOTIFY textsChanged FINAL)
    // [{ fullName, firstName, lastName }] of the first rows, or with an error text.
    Q_PROPERTY(QVariantList nameSplitPreview READ nameSplitPreview NOTIFY mappingChanged FINAL)
    Q_PROPERTY(
        bool defaultPositionRequired READ defaultPositionRequired NOTIFY mappingChanged FINAL)
    // A position database text, "" for none.
    Q_PROPERTY(QString defaultPosition READ defaultPosition NOTIFY mappingChanged FINAL)
    Q_PROPERTY(QVariantList positionOptions READ positionOptions NOTIFY textsChanged FINAL)
    // What stops the mapping, translated; empty when it can be checked.
    Q_PROPERTY(QStringList mappingErrors READ mappingErrors NOTIFY mappingChanged FINAL)

    // Validation report. Rows: rowNumber, statusKey, statusText, name, action, details, message.
    Q_PROPERTY(fdss::viewmodels::RowsModel* report READ report CONSTANT FINAL)
    Q_PROPERTY(int okCount READ okCount NOTIFY reportChanged FINAL)
    Q_PROPERTY(int warningCount READ warningCount NOTIFY reportChanged FINAL)
    Q_PROPERTY(int errorCount READ errorCount NOTIFY reportChanged FINAL)
    Q_PROPERTY(int createCount READ createCount NOTIFY reportChanged FINAL)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY reportChanged FINAL)
    Q_PROPERTY(int skipCount READ skipCount NOTIFY reportChanged FINAL)
    Q_PROPERTY(
        bool updateExisting READ updateExisting WRITE setUpdateExisting NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool skipInvalid READ skipInvalid WRITE setSkipInvalid NOTIFY reportChanged FINAL)
    Q_PROPERTY(bool canCommit READ canCommit NOTIFY reportChanged FINAL)

    // Summary: created, updated, skipped, invalid; after an undo also deleted, archived,
    // notReverted.
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY summaryChanged FINAL)
    Q_PROPERTY(bool undone READ undone NOTIFY summaryChanged FINAL)

public:
    explicit ImportWizardViewModel(QObject* parent = nullptr);
    ~ImportWizardViewModel() override;

    [[nodiscard]] qint64 teamId() const;
    void setTeamId(qint64 teamId);
    [[nodiscard]] QString step() const;
    [[nodiscard]] bool canImport() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] double progress() const;
    [[nodiscard]] QString errorText() const;
    [[nodiscard]] QVariantList typeOptions() const;

    [[nodiscard]] QString fileName() const;
    [[nodiscard]] QString sourceSummary() const;
    [[nodiscard]] QVariantList sheetOptions() const;
    [[nodiscard]] int sheetIndex() const;
    [[nodiscard]] QVariantList previewColumns() const;
    [[nodiscard]] QVariantList previewRows() const;
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] QString duplicateWarning() const;

    [[nodiscard]] RowsModel* mapping() const;
    [[nodiscard]] QVariantList fieldOptions() const;
    [[nodiscard]] bool mappingRemembered() const;
    [[nodiscard]] bool nameOrderVisible() const;
    [[nodiscard]] QString nameOrder() const;
    [[nodiscard]] QVariantList nameOrderOptions() const;
    [[nodiscard]] QVariantList nameSplitPreview() const;
    [[nodiscard]] bool defaultPositionRequired() const;
    [[nodiscard]] QString defaultPosition() const;
    [[nodiscard]] QVariantList positionOptions() const;
    [[nodiscard]] QStringList mappingErrors() const;

    [[nodiscard]] RowsModel* report() const;
    [[nodiscard]] int okCount() const;
    [[nodiscard]] int warningCount() const;
    [[nodiscard]] int errorCount() const;
    [[nodiscard]] int createCount() const;
    [[nodiscard]] int updateCount() const;
    [[nodiscard]] int skipCount() const;
    [[nodiscard]] bool updateExisting() const;
    void setUpdateExisting(bool update);
    [[nodiscard]] bool skipInvalid() const;
    void setSkipInvalid(bool skip);
    [[nodiscard]] bool canCommit() const;

    [[nodiscard]] QVariantMap summary() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool undone() const;

    // Type step: "squad". Goes to the file step.
    Q_INVOKABLE void chooseType(const QString& type);
    // File step: reads the file; preview step once it is read, errorText otherwise.
    Q_INVOKABLE void openFile(const QUrl& file);
    Q_INVOKABLE void openPath(const QString& path);
    // Preview step: reads the file again with another sheet.
    Q_INVOKABLE void selectSheet(int index);
    // Preview -> mapping.
    Q_INVOKABLE void goToMapping();
    // A field key or "" (ignore). A field already on another column moves here.
    Q_INVOKABLE void setColumnField(int column, const QString& field);
    Q_INVOKABLE void setNameOrder(const QString& order);
    Q_INVOKABLE void setDefaultPosition(const QString& position);
    // Mapping -> report, after the validation on the thread pool.
    Q_INVOKABLE void validate();
    // Report -> summary. Writes; errors go to errorOccurred.
    Q_INVOKABLE void commit();
    // Summary: undoes the import.
    Q_INVOKABLE void undoImport();
    // One step back (report -> mapping -> preview -> file -> type).
    Q_INVOKABLE void back();
    // A new import from the type step.
    Q_INVOKABLE void reset();
    // Saves a copy of the squad template ("csv" or "xlsx"); true when written.
    Q_INVOKABLE bool saveTemplate(const QUrl& target, const QString& format);
    // The values of a report row under the role names; for the view and the tests.
    Q_INVOKABLE QVariantMap reportRow(int row) const;

signals:
    void teamIdChanged();
    void stepChanged();
    void busyChanged();
    void progressChanged();
    void errorTextChanged();
    void textsChanged();
    void sourceChanged();
    void mappingChanged();
    void reportChanged();
    void summaryChanged();
    // A translated message for a toast.
    void errorOccurred(const QString& message);
    void infoMessage(const QString& message);
    // After commit() wrote the players and after undoImport(): the squad changed.
    void squadChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void read(const QString& path, std::optional<int> sheet);
    void applySource(services::ImportSource source);
    void setStep(const QString& step);
    void setBusy(bool busy, double progress = 0);
    void setErrorText(const QString& text);
    void failWith(const core::Error& error);
    void rebuildMappingRows();
    void rebuildReport();
    void clearReport();
    [[nodiscard]] int currentYear() const;
    [[nodiscard]] core::ExistingPolicy existingPolicy() const;
    [[nodiscard]] QString previousImportText() const;

    services::ImportService& m_imports;
    services::Session& m_session;
    RowsModel* m_mapping;
    RowsModel* m_report;
    QFutureWatcher<std::vector<core::SquadRow>>* m_validation = nullptr;

    qint64 m_teamId = 0;
    QString m_step = QStringLiteral("type");
    bool m_busy = false;
    double m_progress = 0;
    QString m_errorText;
    std::optional<core::Error> m_error; // what errorText says, again on a change of the language
    int m_request = 0;

    std::optional<services::ImportSource> m_source;
    std::vector<services::PreviousImport> m_previous;
    core::SquadMapping m_squadMapping;
    bool m_mappingRemembered = false;
    std::vector<core::SquadRow> m_rows;
    bool m_updateExisting = false;
    bool m_skipInvalid = true;

    std::optional<services::SquadImportSummary> m_summary;
    std::optional<services::ImportUndoSummary> m_undo;
};

} // namespace fdss::viewmodels
