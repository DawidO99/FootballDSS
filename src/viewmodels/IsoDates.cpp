#include "viewmodels/IsoDates.hpp"

#include <QDate>
#include <QTime>

#include <chrono>

namespace fdss::viewmodels {

namespace {

constexpr qsizetype kIsoDateLength = 10;
constexpr qsizetype kTimeLength = 5;
const auto kIsoFormat = QStringLiteral("yyyy-MM-dd");
const auto kTimeFormat = QStringLiteral("HH:mm");

QDate toQDate(core::Date date) {
    return QDate(static_cast<int>(date.year()),
                 static_cast<int>(static_cast<unsigned>(date.month())),
                 static_cast<int>(static_cast<unsigned>(date.day())));
}

} // namespace

QString IsoDates::toIso(core::Date date) { return toQDate(date).toString(kIsoFormat); }

QString IsoDates::toIso(const std::optional<core::Date>& date) {
    return date ? toIso(*date) : QString{};
}

std::optional<core::Date> IsoDates::parse(const QString& iso) {
    const QDate date = QDate::fromString(iso, kIsoFormat);
    if (iso.size() != kIsoDateLength || !date.isValid()) {
        return std::nullopt;
    }
    return core::Date{std::chrono::year{date.year()},
                      std::chrono::month{static_cast<unsigned>(date.month())},
                      std::chrono::day{static_cast<unsigned>(date.day())}};
}

QString IsoDates::displayText(const std::optional<core::Date>& date) {
    if (!date) {
        return {};
    }
    //: Qt date format of the app language. Keep it the same as in DateField: dd.MM.yyyy in Polish.
    return toQDate(*date).toString(tr("dd/MM/yyyy"));
}

QString IsoDates::timeToIso(const std::optional<core::TimeOfDay>& time) {
    if (!time) {
        return {};
    }
    return QTime::fromMSecsSinceStartOfDay(
               static_cast<int>(std::chrono::milliseconds{*time}.count()))
        .toString(kTimeFormat);
}

std::optional<core::TimeOfDay> IsoDates::parseTime(const QString& text) {
    const QTime time = QTime::fromString(text, kTimeFormat);
    if (text.size() != kTimeLength || !time.isValid()) {
        return std::nullopt;
    }
    return std::chrono::hours{time.hour()} + std::chrono::minutes{time.minute()};
}

} // namespace fdss::viewmodels
