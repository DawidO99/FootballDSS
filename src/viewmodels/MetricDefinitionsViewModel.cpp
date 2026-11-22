#include "viewmodels/MetricDefinitionsViewModel.hpp"

#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "services/MetricService.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/MetricTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <utility>

namespace fdss::viewmodels {

namespace {

const QString kNamePl = QStringLiteral("namePl");
const QString kNameEn = QStringLiteral("nameEn");
const QString kUnit = QStringLiteral("unit");
const QString kMinText = QStringLiteral("minText");
const QString kMaxText = QStringLiteral("maxText");
const QString kAggregation = QStringLiteral("aggregation");

template <class E> std::optional<E> enumOf(const QVariantMap& form, const QString& key) {
    const auto value = core::fromDbText<E>(form.value(key).toString().toStdString());
    return value ? std::optional(*value) : std::nullopt;
}

// The form key that shows an error of core::validate() or of the service; empty: none does.
QString formKeyOf(const std::string& field, const core::MetricDefinitionDraft& draft) {
    if (field == "name" || field == "name_pl") {
        return kNamePl;
    }
    if (field == "name_en") {
        return kNameEn;
    }
    if (field == "key") {
        // The key comes from the English name when there is one (core::metricKey()).
        return core::trimmed(draft.nameEn).empty() ? kNamePl : kNameEn;
    }
    if (field == "unit") {
        return kUnit;
    }
    if (field == "min") {
        return kMinText;
    }
    if (field == "max") {
        return kMaxText;
    }
    if (field == "aggregation") {
        return kAggregation;
    }
    return {};
}

} // namespace

MetricDefinitionsViewModel::MetricDefinitionsViewModel(QObject* parent)
    : QObject(parent), m_metrics(ServiceRegistry::instance().metrics()),
      m_session(ServiceRegistry::instance().session()),
      m_definitions(new MetricDefinitionListModel(this)),
      m_canManage(m_session.isAllowed(core::Permission::ManageMetrics)) {
    // Installing a translator sends QEvent::LanguageChange to the application object, which only
    // an application-wide filter sees; eventFilter() ignores the copies Qt sends on to the items.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    reload();
}

MetricDefinitionListModel* MetricDefinitionsViewModel::definitions() const { return m_definitions; }

bool MetricDefinitionsViewModel::canManage() const { return m_canManage; }

QVariantList MetricDefinitionsViewModel::valueTypeOptions() const {
    return MetricTexts::valueTypeOptions();
}

QVariantList MetricDefinitionsViewModel::scopeOptions() const {
    return MetricTexts::scopeOptions();
}

QVariantMap MetricDefinitionsViewModel::fieldErrors() const { return m_fieldErrors; }

QString MetricDefinitionsViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

bool MetricDefinitionsViewModel::canUndo() const { return m_removed.has_value(); }

QVariantList MetricDefinitionsViewModel::aggregationOptions(const QString& valueType) const {
    const auto type = core::fromDbText<core::MetricValueType>(valueType.toStdString());
    return MetricTexts::aggregationOptions(type.value_or(core::MetricValueType::Decimal));
}

bool MetricDefinitionsViewModel::isLocked(qint64 id) const {
    const services::MetricDefinitionInfo* info = m_definitions->find(id);
    return info && info->valueCount > 0;
}

void MetricDefinitionsViewModel::reload() {
    auto definitions = m_metrics.listDefinitions();
    const bool hadFailure = m_loadFailure.has_value();
    if (!definitions) {
        qCritical("MetricDefinitionsViewModel: %s", definitions.error().detail.c_str());
        m_loadFailure = definitions.error();
        m_definitions->setDefinitions({});
        emit loadErrorChanged();
        return;
    }
    m_loadFailure.reset();
    m_definitions->setDefinitions(std::move(*definitions));
    if (hadFailure) {
        emit loadErrorChanged();
    }
}

bool MetricDefinitionsViewModel::add(const QVariantMap& form) {
    if (!checkAllowed()) {
        return false;
    }
    const std::optional<core::MetricDefinitionDraft> draft = readForm(form);
    if (!draft) {
        return false;
    }
    const auto id = m_metrics.addDefinition(*draft);
    return finish(id ? core::Status{} : core::Status{std::unexpected(id.error())}, *draft);
}

bool MetricDefinitionsViewModel::update(qint64 id, const QVariantMap& form) {
    if (!checkAllowed()) {
        return false;
    }
    const std::optional<core::MetricDefinitionDraft> draft = readForm(form);
    if (!draft) {
        return false;
    }
    return finish(m_metrics.updateDefinition(core::MetricDefinitionId{id}, *draft), *draft);
}

bool MetricDefinitionsViewModel::remove(qint64 id) {
    if (!checkAllowed()) {
        return false;
    }
    auto deleted = m_metrics.deleteDefinition(core::MetricDefinitionId{id});
    if (!deleted) {
        report(deleted.error());
        return false;
    }
    const QString name = MetricTexts::name(*deleted);
    m_removed = std::move(*deleted);
    emit canUndoChanged();
    reload();
    emit definitionsChanged();
    emit removed(name);
    return true;
}

bool MetricDefinitionsViewModel::undoRemove() {
    if (!checkAllowed()) {
        return false;
    }
    if (!m_removed) {
        report(core::Error{core::ErrorCode::NotFound, "no deleted metric", "nothing_to_undo"});
        return false;
    }
    const auto restored = m_metrics.restoreDefinition(*m_removed);
    if (!restored) {
        report(restored.error());
        return false;
    }
    m_removed.reset();
    emit canUndoChanged();
    reload();
    emit definitionsChanged();
    return true;
}

void MetricDefinitionsViewModel::clearErrors() {
    if (m_fieldErrors.isEmpty()) {
        return;
    }
    m_fieldErrors.clear();
    emit errorsChanged();
}

bool MetricDefinitionsViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange) {
        emit optionsChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

std::optional<core::MetricDefinitionDraft>
MetricDefinitionsViewModel::readForm(const QVariantMap& form) {
    m_fieldErrors.clear();
    core::MetricDefinitionDraft draft;
    draft.namePl = form.value(kNamePl).toString().trimmed().toStdString();
    draft.nameEn = form.value(kNameEn).toString().trimmed().toStdString();
    const QString unit = form.value(kUnit).toString().trimmed();
    if (!unit.isEmpty()) {
        draft.unit = unit.toStdString();
    }
    draft.valueType = enumOf<core::MetricValueType>(form, QStringLiteral("valueType"))
                          .value_or(core::MetricValueType::Decimal);
    draft.aggregation =
        enumOf<core::MetricAggregation>(form, kAggregation).value_or(core::MetricAggregation::Avg);
    draft.scope = enumOf<core::MetricScope>(form, QStringLiteral("scope"))
                      .value_or(core::MetricScope::Training);
    draft.higherIsBetter = form.value(QStringLiteral("higherIsBetter"), true).toBool();

    const QString notANumber = tr("Enter a number, such as 4,35, or leave it empty.");
    if (!MetricTexts::parseLimit(form.value(kMinText).toString(), draft.min)) {
        setFieldError(kMinText, notANumber);
    }
    if (!MetricTexts::parseLimit(form.value(kMaxText).toString(), draft.max)) {
        setFieldError(kMaxText, notANumber);
    }
    for (const core::ValidationError& error : core::validate(draft)) {
        const QString key = formKeyOf(error.field, draft);
        if (!key.isEmpty()) {
            setFieldError(key, ErrorPresenter::invalidInputMessage(error.field));
        }
    }
    emit errorsChanged();
    if (!m_fieldErrors.isEmpty()) {
        return std::nullopt;
    }
    return draft;
}

bool MetricDefinitionsViewModel::checkAllowed() {
    if (m_session.isAllowed(core::Permission::ManageMetrics)) {
        return true;
    }
    report(core::Error{core::ErrorCode::PermissionDenied,
                       "refused by the viewmodel: ManageMetrics is needed",
                       {}});
    return false;
}

bool MetricDefinitionsViewModel::finish(const core::Status& status,
                                        const core::MetricDefinitionDraft& draft) {
    if (!status) {
        const core::Error& error = status.error();
        if (const QString key = formKeyOf(error.field, draft); !key.isEmpty()) {
            // A taken key, or a check of the service that the form did not make.
            setFieldError(key, ErrorPresenter::message(error));
            emit errorsChanged();
            qWarning("MetricDefinitionsViewModel: %s", error.detail.c_str());
            return false;
        }
        report(error);
        return false;
    }
    m_fieldErrors.clear();
    emit errorsChanged();
    reload();
    emit definitionsChanged();
    return true;
}

void MetricDefinitionsViewModel::setFieldError(const QString& key, const QString& message) {
    if (!m_fieldErrors.contains(key)) {
        m_fieldErrors.insert(key, message);
    }
}

void MetricDefinitionsViewModel::report(const core::Error& error) {
    qCritical("MetricDefinitionsViewModel: %s", error.detail.c_str());
    emit errorOccurred(ErrorPresenter::message(error));
}

} // namespace fdss::viewmodels
