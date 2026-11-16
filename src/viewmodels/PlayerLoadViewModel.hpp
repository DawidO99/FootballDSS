#pragma once

#include "core/util/Error.hpp"
#include "services/LoadService.hpp"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace fdss::services {
class Clock;
class Session;
} // namespace fdss::services

namespace fdss::viewmodels {

// The load of one player (packet P11): status, why, what is missing, and the chart of the last
// 8 weeks for LoadChart. The chart series are lists of numbers and points, not list models: they go
// straight into BarSet.values and LineSeries.replace() (P11 decision 7). Day index 0 is the first
// day of the chart, chartDays - 1 is today.
class PlayerLoadViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // 0: no player.
    Q_PROPERTY(qint64 playerId READ playerId WRITE setPlayerId NOTIFY playerIdChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    // At least one RPE entry.
    Q_PROPERTY(bool hasData READ hasData NOTIFY loadChanged FINAL)
    // A status other than "not enough data": the chronic load and the ratio are shown.
    Q_PROPERTY(bool baselineReady READ baselineReady NOTIFY loadChanged FINAL)
    // What LoadStatusBadge takes, and the word on it.
    Q_PROPERTY(QString statusKey READ statusKey NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY loadChanged FINAL)
    // Why, ending in "A signal to talk to the player." for high and elevated.
    Q_PROPERTY(QString explanation READ explanation NOTIFY loadChanged FINAL)
    // The same, independent of the language: ratioHigh, afterBreak… (LoadTexts::reasonKey).
    Q_PROPERTY(QStringList reasonKeys READ reasonKeys NOTIFY loadChanged FINAL)
    // What is missing for a status; empty when nothing is.
    Q_PROPERTY(QString hint READ hint NOTIFY loadChanged FINAL)
    // Sessions and matches without RPE in the last 28 days; empty when there are none.
    Q_PROPERTY(QString missingRpeText READ missingRpeText NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString asOfText READ asOfText NOTIFY loadChanged FINAL)
    // The numbers as the panel shows them; empty when there is none (grey: acute and week only).
    Q_PROPERTY(QString acuteText READ acuteText NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString chronicText READ chronicText NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString ratioText READ ratioText NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString monotonyText READ monotonyText NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString strainText READ strainText NOTIFY loadChanged FINAL)
    Q_PROPERTY(QString weekLoadText READ weekLoadText NOTIFY loadChanged FINAL)
    // The chart: chartDays days through today.
    Q_PROPERTY(int chartDays READ chartDays CONSTANT FINAL)
    Q_PROPERTY(QStringList chartDayLabels READ chartDayLabels NOTIFY loadChanged FINAL)
    Q_PROPERTY(QList<qreal> sessionLoads READ sessionLoads NOTIFY loadChanged FINAL)
    Q_PROPERTY(QList<qreal> matchLoads READ matchLoads NOTIFY loadChanged FINAL)
    // x = day index. From the first entry on; chronic only with a baseline, the ratio only on days
    // that had a full one.
    Q_PROPERTY(QList<QPointF> acutePoints READ acutePoints NOTIFY loadChanged FINAL)
    Q_PROPERTY(QList<QPointF> chronicPoints READ chronicPoints NOTIFY loadChanged FINAL)
    Q_PROPERTY(QList<QPointF> ratioPoints READ ratioPoints NOTIFY loadChanged FINAL)
    // Rounded up so the axes end on a tick; the load axis has at most 5 steps.
    Q_PROPERTY(qreal loadAxisMax READ loadAxisMax NOTIFY loadChanged FINAL)
    Q_PROPERTY(qreal loadAxisStep READ loadAxisStep NOTIFY loadChanged FINAL)
    Q_PROPERTY(qreal ratioAxisMax READ ratioAxisMax NOTIFY loadChanged FINAL)
    // { high, elevated, low } as numbers and { highText, elevatedText, lowText } as the legend
    // shows them.
    Q_PROPERTY(QVariantMap thresholds READ thresholds NOTIFY loadChanged FINAL)
    // Why the load could not be read. Empty: the data is fine.
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadErrorChanged FINAL)

public:
    static constexpr int kChartDays = 56;

    explicit PlayerLoadViewModel(QObject* parent = nullptr);

    [[nodiscard]] qint64 playerId() const;
    void setPlayerId(qint64 playerId);
    [[nodiscard]] bool busy() const;
    [[nodiscard]] bool hasData() const;
    [[nodiscard]] bool baselineReady() const;
    [[nodiscard]] QString statusKey() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString explanation() const;
    [[nodiscard]] QStringList reasonKeys() const;
    [[nodiscard]] QString hint() const;
    [[nodiscard]] QString missingRpeText() const;
    [[nodiscard]] QString asOfText() const;
    [[nodiscard]] QString acuteText() const;
    [[nodiscard]] QString chronicText() const;
    [[nodiscard]] QString ratioText() const;
    [[nodiscard]] QString monotonyText() const;
    [[nodiscard]] QString strainText() const;
    [[nodiscard]] QString weekLoadText() const;
    [[nodiscard]] int chartDays() const;
    [[nodiscard]] QStringList chartDayLabels() const;
    [[nodiscard]] QList<qreal> sessionLoads() const;
    [[nodiscard]] QList<qreal> matchLoads() const;
    [[nodiscard]] QList<QPointF> acutePoints() const;
    [[nodiscard]] QList<QPointF> chronicPoints() const;
    [[nodiscard]] QList<QPointF> ratioPoints() const;
    [[nodiscard]] qreal loadAxisMax() const;
    [[nodiscard]] qreal loadAxisStep() const;
    [[nodiscard]] qreal ratioAxisMax() const;
    [[nodiscard]] QVariantMap thresholds() const;
    [[nodiscard]] QString loadError() const;

    Q_INVOKABLE void reload();
    // The tooltip of one chart day: { title, rows: [{ key, label, value }] }; key is sessions,
    // matches, acute, chronic, ratio or none (no data that day). Empty out of range.
    Q_INVOKABLE QVariantMap dayAt(int index) const;

signals:
    void playerIdChanged();
    void busyChanged();
    void loadChanged();
    void loadErrorChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Chart {
        QList<qreal> sessionLoads;
        QList<qreal> matchLoads;
        QList<QPointF> acutePoints;
        QList<QPointF> chronicPoints;
        QList<QPointF> ratioPoints;
        // Index into the series of the detail for every chart day, -1 without data.
        QList<int> seriesIndex;
        qreal loadAxisMax = 0;
        qreal loadAxisStep = 0;
        qreal ratioAxisMax = 0;
    };

    void apply(services::PlayerLoadDetail detail);
    void clear(std::optional<core::Error> failure);
    void setBusy(bool busy);
    [[nodiscard]] const core::LoadAssessment* assessment() const;
    [[nodiscard]] core::Date chartDay(int index) const;
    [[nodiscard]] static Chart buildChart(const services::PlayerLoadDetail& detail);

    services::LoadService& m_load;
    services::Session& m_session;
    const services::Clock& m_clock;

    qint64 m_playerId = 0;
    bool m_busy = false;
    int m_request = 0;
    std::optional<services::PlayerLoadDetail> m_detail;
    Chart m_chart;
    std::optional<core::Error> m_loadFailure;
};

} // namespace fdss::viewmodels
