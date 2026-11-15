#include "viewmodels/PlayerLoadViewModel.hpp"

#include "core/domain/Ids.hpp"
#include "core/domain/Permissions.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"
#include "viewmodels/ErrorPresenter.hpp"
#include "viewmodels/IsoDates.hpp"
#include "viewmodels/LoadTexts.hpp"
#include "viewmodels/ServiceRegistry.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QFuture>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <utility>

namespace fdss::viewmodels {

namespace {

using std::chrono::days;
using std::chrono::sys_days;

constexpr qreal kMinRatioAxis = 2.0;
constexpr qreal kRatioAxisStep = 0.5;
constexpr int kMaxLoadSteps = 5;
constexpr std::array<qreal, 8> kLoadSteps{100, 200, 250, 500, 1000, 2000, 2500, 5000};

int daysBetween(core::Date from, core::Date to) {
    return static_cast<int>((sys_days{to} - sys_days{from}).count());
}

QVariantMap row(const QString& key, const QString& label, const QString& value) {
    return QVariantMap{{QStringLiteral("key"), key},
                       {QStringLiteral("label"), label},
                       {QStringLiteral("value"), value}};
}

} // namespace

PlayerLoadViewModel::PlayerLoadViewModel(QObject* parent)
    : QObject(parent), m_load(ServiceRegistry::instance().load()),
      m_session(ServiceRegistry::instance().session()),
      m_clock(ServiceRegistry::instance().clock()) {
    // Installing a translator sends QEvent::LanguageChange to the application object only.
    if (QCoreApplication* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
    clear(std::nullopt);
}

qint64 PlayerLoadViewModel::playerId() const { return m_playerId; }

void PlayerLoadViewModel::setPlayerId(qint64 playerId) {
    if (playerId == m_playerId) {
        return;
    }
    m_playerId = playerId;
    emit playerIdChanged();
    reload();
}

bool PlayerLoadViewModel::busy() const { return m_busy; }

bool PlayerLoadViewModel::hasData() const { return m_detail && !m_detail->load.days.empty(); }

bool PlayerLoadViewModel::baselineReady() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr && current->verdict.status != core::LoadStatus::NotEnoughData;
}

QString PlayerLoadViewModel::statusKey() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr ? LoadTexts::statusKey(current->verdict.status) : QString{};
}

QString PlayerLoadViewModel::statusText() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr ? LoadTexts::statusName(current->verdict) : QString{};
}

QString PlayerLoadViewModel::explanation() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr ? LoadTexts::explanation(*current, m_detail->thresholds) : QString{};
}

QString PlayerLoadViewModel::hint() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr ? LoadTexts::hint(*current) : QString{};
}

QString PlayerLoadViewModel::missingRpeText() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr ? LoadTexts::missingRpe(*current, m_detail->thresholds) : QString{};
}

QString PlayerLoadViewModel::asOfText() const {
    const core::LoadAssessment* current = assessment();
    return current != nullptr && hasData() ? LoadTexts::asOf(current->asOf) : QString{};
}

QString PlayerLoadViewModel::acuteText() const {
    return hasData() ? LoadTexts::load(assessment()->indicators.acute) : QString{};
}

QString PlayerLoadViewModel::chronicText() const {
    return baselineReady() ? LoadTexts::load(assessment()->indicators.chronic) : QString{};
}

QString PlayerLoadViewModel::ratioText() const {
    if (!baselineReady() || !assessment()->indicators.ratio) {
        return {};
    }
    return LoadTexts::decimal(*assessment()->indicators.ratio, 2);
}

QString PlayerLoadViewModel::monotonyText() const {
    if (!baselineReady() || !assessment()->indicators.monotony) {
        return {};
    }
    return LoadTexts::decimal(*assessment()->indicators.monotony, 2);
}

QString PlayerLoadViewModel::strainText() const {
    if (!baselineReady() || !assessment()->indicators.strain) {
        return {};
    }
    return LoadTexts::load(*assessment()->indicators.strain);
}

QString PlayerLoadViewModel::weekLoadText() const {
    return hasData() ? LoadTexts::load(assessment()->indicators.weekLoad) : QString{};
}

int PlayerLoadViewModel::chartDays() const { return kChartDays; }

QStringList PlayerLoadViewModel::chartDayLabels() const {
    QStringList labels;
    labels.reserve(kChartDays);
    for (int index = 0; index < kChartDays; ++index) {
        labels.append(LoadTexts::chartDay(chartDay(index)));
    }
    return labels;
}

QList<qreal> PlayerLoadViewModel::sessionLoads() const { return m_chart.sessionLoads; }

QList<qreal> PlayerLoadViewModel::matchLoads() const { return m_chart.matchLoads; }

QList<QPointF> PlayerLoadViewModel::acutePoints() const { return m_chart.acutePoints; }

QList<QPointF> PlayerLoadViewModel::chronicPoints() const { return m_chart.chronicPoints; }

QList<QPointF> PlayerLoadViewModel::ratioPoints() const { return m_chart.ratioPoints; }

qreal PlayerLoadViewModel::loadAxisMax() const { return m_chart.loadAxisMax; }

qreal PlayerLoadViewModel::loadAxisStep() const { return m_chart.loadAxisStep; }

qreal PlayerLoadViewModel::ratioAxisMax() const { return m_chart.ratioAxisMax; }

QVariantMap PlayerLoadViewModel::thresholds() const {
    const core::LoadThresholds& values = m_detail ? m_detail->thresholds : m_load.thresholds();
    return QVariantMap{
        {QStringLiteral("high"), values.ratioHigh},
        {QStringLiteral("elevated"), values.ratioElevated},
        {QStringLiteral("low"), values.ratioLow},
        {QStringLiteral("highText"), LoadTexts::decimal(values.ratioHigh, 1)},
        {QStringLiteral("elevatedText"), LoadTexts::decimal(values.ratioElevated, 1)},
        {QStringLiteral("lowText"), LoadTexts::decimal(values.ratioLow, 1)}};
}

QString PlayerLoadViewModel::loadError() const {
    return m_loadFailure ? ErrorPresenter::message(*m_loadFailure) : QString{};
}

void PlayerLoadViewModel::reload() {
    const int request = ++m_request;
    // ViewAnalysis: every role, and nobody logged out. The service checks it again.
    if (m_playerId == 0 || !m_session.isAllowed(core::Permission::ViewAnalysis)) {
        clear(std::nullopt);
        return;
    }
    auto future = m_load.playerLoad(core::PlayerId{m_playerId});
    if (!future) {
        qCritical("PlayerLoadViewModel: %s", future.error().detail.c_str());
        clear(future.error());
        return;
    }
    setBusy(true);
    future->then(this, [this, request](services::PlayerLoadDetail detail) {
        if (request == m_request) {
            apply(std::move(detail));
        }
    });
}

QVariantMap PlayerLoadViewModel::dayAt(int index) const {
    if (index < 0 || index >= kChartDays) {
        return {};
    }
    QVariantList rows;
    const int seriesIndex = m_chart.seriesIndex.value(index, -1);
    if (!m_detail || seriesIndex < 0) {
        rows.append(row(QStringLiteral("none"), tr("No data"), {}));
    } else {
        const core::LoadDay& day = m_detail->load.days[static_cast<std::size_t>(seriesIndex)];
        rows.append(row(QStringLiteral("sessions"), tr("Training"),
                        tr("%1 AU").arg(LoadTexts::load(day.sessionLoad))));
        rows.append(row(QStringLiteral("matches"), tr("Match"),
                        tr("%1 AU").arg(LoadTexts::load(day.matchLoad))));
        rows.append(row(QStringLiteral("acute"), tr("Acute load"),
                        tr("%1 AU").arg(LoadTexts::load(day.acute))));
        if (baselineReady()) {
            rows.append(row(QStringLiteral("chronic"), tr("Chronic load"),
                            tr("%1 AU").arg(LoadTexts::load(day.chronic))));
        }
        const auto ratioPoint = std::ranges::find(m_chart.ratioPoints, qreal(index), &QPointF::x);
        if (ratioPoint != m_chart.ratioPoints.end()) {
            rows.append(
                row(QStringLiteral("ratio"), tr("Ratio"), LoadTexts::decimal(ratioPoint->y(), 2)));
        }
    }
    return QVariantMap{{QStringLiteral("title"), IsoDates::displayText(chartDay(index))},
                       {QStringLiteral("rows"), rows}};
}

bool PlayerLoadViewModel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        emit loadChanged();
        emit loadErrorChanged();
    }
    return QObject::eventFilter(watched, event);
}

void PlayerLoadViewModel::apply(services::PlayerLoadDetail detail) {
    m_chart = buildChart(detail);
    m_detail = std::move(detail);
    m_loadFailure.reset();
    setBusy(false);
    emit loadErrorChanged();
    emit loadChanged();
}

void PlayerLoadViewModel::clear(std::optional<core::Error> failure) {
    m_detail.reset();
    m_chart = Chart{};
    m_chart.sessionLoads.fill(0.0, kChartDays);
    m_chart.matchLoads.fill(0.0, kChartDays);
    m_chart.seriesIndex.fill(-1, kChartDays);
    m_chart.loadAxisMax = kLoadSteps.front();
    m_chart.loadAxisStep = kLoadSteps.front();
    m_chart.ratioAxisMax = kMinRatioAxis;
    m_loadFailure = std::move(failure);
    setBusy(false);
    emit loadErrorChanged();
    emit loadChanged();
}

void PlayerLoadViewModel::setBusy(bool busy) {
    if (busy == m_busy) {
        return;
    }
    m_busy = busy;
    emit busyChanged();
}

const core::LoadAssessment* PlayerLoadViewModel::assessment() const {
    return m_detail ? &m_detail->load.assessment : nullptr;
}

core::Date PlayerLoadViewModel::chartDay(int index) const {
    const core::Date today = m_detail ? m_detail->today : m_clock.today();
    return core::Date{sys_days{today} - days{kChartDays - 1 - index}};
}

PlayerLoadViewModel::Chart
PlayerLoadViewModel::buildChart(const services::PlayerLoadDetail& detail) {
    Chart chart;
    chart.sessionLoads.fill(0.0, kChartDays);
    chart.matchLoads.fill(0.0, kChartDays);
    chart.seriesIndex.fill(-1, kChartDays);

    const std::vector<core::LoadDay>& series = detail.load.days;
    const core::LoadThresholds& thresholds = detail.thresholds;
    const bool baseline = detail.load.assessment.verdict.status != core::LoadStatus::NotEnoughData;
    const core::Date firstChartDay = core::Date{sys_days{detail.today} - days{kChartDays - 1}};
    qreal maxLoad = 0.0;
    qreal maxRatio = 0.0;
    int rpeDays = 0;
    for (std::size_t seriesIndex = 0; seriesIndex < series.size(); ++seriesIndex) {
        const core::LoadDay& day = series[seriesIndex];
        rpeDays += day.hasRpe ? 1 : 0;
        const int index = daysBetween(firstChartDay, day.date);
        if (index < 0 || index >= kChartDays) {
            continue;
        }
        const auto position = static_cast<qsizetype>(index);
        chart.sessionLoads[position] = day.sessionLoad;
        chart.matchLoads[position] = day.matchLoad;
        chart.seriesIndex[position] = static_cast<int>(seriesIndex);
        chart.acutePoints.append(QPointF(index, day.acute));
        maxLoad = std::max({maxLoad, qreal(day.total()), day.acute});
        if (baseline) {
            chart.chronicPoints.append(QPointF(index, day.chronic));
            maxLoad = std::max(maxLoad, day.chronic);
        }
        // The ratio only where that day had a full baseline itself.
        const auto ratio = day.ratio();
        if (baseline && ratio && static_cast<int>(seriesIndex) >= thresholds.baselineDays &&
            rpeDays >= thresholds.minRpeDays) {
            chart.ratioPoints.append(QPointF(index, *ratio));
            maxRatio = std::max(maxRatio, *ratio);
        }
    }

    const auto step = std::ranges::find_if(
        kLoadSteps, [maxLoad](qreal candidate) { return maxLoad <= candidate * kMaxLoadSteps; });
    chart.loadAxisStep = step != kLoadSteps.end() ? *step : kLoadSteps.back();
    chart.loadAxisMax = std::max(1.0, std::ceil(maxLoad / chart.loadAxisStep)) * chart.loadAxisStep;
    chart.ratioAxisMax =
        std::max(kMinRatioAxis, std::ceil(maxRatio / kRatioAxisStep) * kRatioAxisStep);
    return chart;
}

} // namespace fdss::viewmodels
