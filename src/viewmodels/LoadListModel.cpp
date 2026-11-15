#include "viewmodels/LoadListModel.hpp"

#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/SquadTexts.hpp"

#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <optional>
#include <utility>

namespace fdss::viewmodels {

namespace {

bool hasBaseline(const core::LoadAssessment& assessment) {
    return assessment.verdict.status != core::LoadStatus::NotEnoughData;
}

QVariant numberOrNull(const std::optional<double>& value) {
    return value ? QVariant(*value) : QVariant{};
}

QString textOrEmpty(const std::optional<double>& value, int decimals) {
    if (!value) {
        return {};
    }
    return decimals == 0 ? LoadTexts::load(*value) : LoadTexts::decimal(*value, decimals);
}

} // namespace

LoadListModel::LoadListModel(QObject* parent) : QAbstractListModel(parent) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

int LoadListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QVariant LoadListModel::data(const QModelIndex& index, int role) const {
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const services::PlayerLoadRow& row = rowAt(index.row());
    const core::LoadAssessment& assessment = row.assessment;
    const core::LoadIndicators& indicators = assessment.indicators;
    const bool baseline = hasBaseline(assessment);
    const std::optional<double> acute =
        indicators.daysSinceFirstEntry ? std::optional(indicators.acute) : std::nullopt;
    const std::optional<double> chronic =
        baseline ? std::optional(indicators.chronic) : std::nullopt;
    const std::optional<double> ratio = baseline ? indicators.ratio : std::nullopt;
    const std::optional<double> monotony = baseline ? indicators.monotony : std::nullopt;
    const std::optional<double> weekLoad =
        indicators.daysSinceFirstEntry ? std::optional<double>(indicators.weekLoad) : std::nullopt;

    switch (role) {
    case PlayerIdRole:
        return QVariant::fromValue<qint64>(row.player.id.value);
    case NumberRole:
        return row.player.shirtNumber ? QVariant(*row.player.shirtNumber) : QVariant{};
    case NameRole:
        return SquadTexts::displayName(row.player);
    case AcuteRole:
        return numberOrNull(acute);
    case AcuteTextRole:
        return textOrEmpty(acute, 0);
    case ChronicRole:
        return numberOrNull(chronic);
    case ChronicTextRole:
        return textOrEmpty(chronic, 0);
    case RatioRole:
        return numberOrNull(ratio);
    case RatioTextRole:
        return textOrEmpty(ratio, 2);
    case MonotonyRole:
        return numberOrNull(monotony);
    case MonotonyTextRole:
        return textOrEmpty(monotony, 2);
    case WeekLoadRole:
        return numberOrNull(weekLoad);
    case WeekLoadTextRole:
        return textOrEmpty(weekLoad, 0);
    case StatusKeyRole:
        return LoadTexts::statusKey(assessment.verdict.status);
    case StatusTextRole:
        return LoadTexts::statusName(assessment.verdict);
    case StatusRankRole:
        return LoadTexts::statusRank(assessment.verdict.status);
    case HintRole:
        return LoadTexts::hint(assessment);
    case ExplanationRole:
        return LoadTexts::explanation(assessment, m_thresholds);
    default:
        return {};
    }
}

QHash<int, QByteArray> LoadListModel::roleNames() const {
    return {{PlayerIdRole, "playerId"},
            {NumberRole, "number"},
            {NameRole, "name"},
            {AcuteRole, "acute"},
            {AcuteTextRole, "acuteText"},
            {ChronicRole, "chronic"},
            {ChronicTextRole, "chronicText"},
            {RatioRole, "ratio"},
            {RatioTextRole, "ratioText"},
            {MonotonyRole, "monotony"},
            {MonotonyTextRole, "monotonyText"},
            {WeekLoadRole, "weekLoad"},
            {WeekLoadTextRole, "weekLoadText"},
            {StatusKeyRole, "statusKey"},
            {StatusTextRole, "statusText"},
            {StatusRankRole, "statusRank"},
            {HintRole, "hint"},
            {ExplanationRole, "explanation"}};
}

void LoadListModel::setRows(std::vector<services::PlayerLoadRow> rows,
                            const core::LoadThresholds& thresholds) {
    beginResetModel();
    m_rows = std::move(rows);
    m_thresholds = thresholds;
    endResetModel();
}

const services::PlayerLoadRow& LoadListModel::rowAt(int row) const {
    return m_rows[static_cast<std::size_t>(row)];
}

int LoadListModel::rowOf(qint64 playerId) const {
    const auto it = std::ranges::find(
        m_rows, playerId, [](const services::PlayerLoadRow& row) { return row.player.id.value; });
    return it == m_rows.end() ? -1 : static_cast<int>(it - m_rows.begin());
}

const std::vector<services::PlayerLoadRow>& LoadListModel::rows() const { return m_rows; }

bool LoadListModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange && !m_rows.empty()) {
        // The numbers carry the decimal separator of the language too.
        emit dataChanged(
            index(0), index(rowCount() - 1),
            {RatioTextRole, MonotonyTextRole, StatusTextRole, HintRole, ExplanationRole});
    }
    return QAbstractListModel::eventFilter(watched, event);
}

} // namespace fdss::viewmodels
