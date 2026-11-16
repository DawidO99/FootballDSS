#include "data/readers/TacticJson.hpp"

#include "core/algorithms/Formations.hpp"
#include "core/domain/Enums.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSaveFile>

#include <cmath>
#include <format>
#include <optional>
#include <string>

namespace fdss::data {

namespace {

constexpr auto kVersionKey = "version";
constexpr auto kFormationKey = "formation";
constexpr auto kSlotsKey = "slots";
constexpr auto kBenchKey = "bench";
constexpr auto kPositionKey = "position";
constexpr auto kXKey = "x";
constexpr auto kYKey = "y";
constexpr auto kPlayerIdKey = "playerId";

constexpr std::string_view kFileField = "tactic_file";
// The largest integer a double holds exactly: JSON numbers are doubles.
constexpr double kMaxExactInteger = 9007199254740991.0;

core::Error badFile(std::string detail) {
    return core::Error{core::ErrorCode::UnsupportedFormat, "tactic file: " + std::move(detail),
                       std::string(kFileField)};
}

bool isWholeNumber(const QJsonValue& value) {
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && std::trunc(number) == number;
}

// A player id: a whole number from 1.
std::optional<core::PlayerId> playerIdOf(const QJsonValue& value) {
    if (!isWholeNumber(value) || value.toDouble() < 1.0 || value.toDouble() > kMaxExactInteger) {
        return std::nullopt;
    }
    return core::PlayerId{static_cast<std::int64_t>(value.toDouble())};
}

std::optional<double> coordinateOf(const QJsonValue& value) {
    if (!value.isDouble()) {
        return std::nullopt;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
        return std::nullopt;
    }
    return number;
}

core::Result<core::LineupSlot> readSlot(const QJsonValue& value, qsizetype index) {
    if (!value.isObject()) {
        return std::unexpected(badFile(std::format("slot {} is not an object", index)));
    }
    const QJsonObject object = value.toObject();
    const QJsonValue position = object.value(QLatin1StringView(kPositionKey));
    if (!position.isString()) {
        return std::unexpected(badFile(std::format("slot {} has no position", index)));
    }
    const auto parsed =
        core::fromDbText<core::Position>(position.toString().toLower().toStdString());
    if (!parsed) {
        return std::unexpected(badFile(std::format("slot {} has an unknown position", index)));
    }
    const auto x = coordinateOf(object.value(QLatin1StringView(kXKey)));
    const auto y = coordinateOf(object.value(QLatin1StringView(kYKey)));
    if (!x || !y) {
        return std::unexpected(
            badFile(std::format("slot {} has coordinates outside 0-1 or none", index)));
    }
    core::LineupSlot slot{*parsed, *x, *y, std::nullopt};
    const QJsonValue player = object.value(QLatin1StringView(kPlayerIdKey));
    if (!player.isUndefined() && !player.isNull()) {
        slot.player = playerIdOf(player);
        if (!slot.player) {
            return std::unexpected(badFile(std::format("slot {} has an invalid player id", index)));
        }
    }
    return slot;
}

core::Status readVersion(const QJsonObject& root) {
    const QJsonValue version = root.value(QLatin1StringView(kVersionKey));
    if (!isWholeNumber(version)) {
        return std::unexpected(badFile("no version number"));
    }
    if (version.toDouble() > kTacticJsonVersion) {
        return std::unexpected(core::Error{core::ErrorCode::IncompatibleVersion,
                                           std::format("tactic file: version {} is newer than {}",
                                                       version.toDouble(), kTacticJsonVersion),
                                           "tactic_version"});
    }
    if (version.toDouble() != kTacticJsonVersion) {
        return std::unexpected(badFile(std::format("unknown version {}", version.toDouble())));
    }
    return {};
}

} // namespace

QByteArray toTacticJson(const core::Lineup& lineup, QJsonDocument::JsonFormat format) {
    QJsonArray slotArray;
    for (const core::LineupSlot& slot : lineup.pitchSlots) {
        QJsonObject object{
            {QLatin1StringView(kPositionKey),
             QString::fromUtf8(core::toDbText(slot.position)).toUpper()},
            {QLatin1StringView(kXKey), slot.x},
            {QLatin1StringView(kYKey), slot.y},
        };
        object.insert(QLatin1StringView(kPlayerIdKey),
                      slot.player ? QJsonValue(static_cast<qint64>(slot.player->value))
                                  : QJsonValue(QJsonValue::Null));
        slotArray.append(object);
    }
    QJsonArray bench;
    for (const core::PlayerId player : lineup.bench) {
        bench.append(static_cast<qint64>(player.value));
    }
    const QJsonObject root{
        {QLatin1StringView(kVersionKey), kTacticJsonVersion},
        {QLatin1StringView(kFormationKey), QString::fromStdString(lineup.formation)},
        {QLatin1StringView(kSlotsKey), slotArray},
        {QLatin1StringView(kBenchKey), bench},
    };
    return QJsonDocument(root).toJson(format);
}

core::Result<core::Lineup> fromTacticJson(const QByteArray& json) {
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return std::unexpected(badFile(std::format(
            "not JSON: {} at {}", parseError.errorString().toStdString(), parseError.offset)));
    }
    if (!document.isObject()) {
        return std::unexpected(badFile("the root is not an object"));
    }
    const QJsonObject root = document.object();
    if (auto version = readVersion(root); !version) {
        return std::unexpected(version.error());
    }
    if (!root.value(QLatin1StringView(kFormationKey)).isString()) {
        return std::unexpected(badFile("no formation"));
    }
    const QJsonValue slotsValue = root.value(QLatin1StringView(kSlotsKey));
    if (!slotsValue.isArray() || slotsValue.toArray().size() != core::kLineupSlots) {
        return std::unexpected(badFile("the slots are not 11"));
    }
    core::Lineup lineup;
    const QJsonArray slotArray = slotsValue.toArray();
    int goalkeepers = 0;
    for (qsizetype i = 0; i < slotArray.size(); ++i) {
        auto slot = readSlot(slotArray.at(i), i);
        if (!slot) {
            return std::unexpected(slot.error());
        }
        goalkeepers += slot->position == core::Position::Gk ? 1 : 0;
        lineup.pitchSlots.push_back(*slot);
    }
    if (goalkeepers > 1) {
        return std::unexpected(badFile("more than one goalkeeper slot"));
    }
    const QJsonValue benchValue = root.value(QLatin1StringView(kBenchKey));
    if (!benchValue.isUndefined() && !benchValue.isNull()) {
        if (!benchValue.isArray() || benchValue.toArray().size() > core::kMaxBench) {
            return std::unexpected(badFile("the bench is not a list of at most 12"));
        }
        for (const QJsonValue& value : benchValue.toArray()) {
            const auto player = playerIdOf(value);
            if (!player) {
                return std::unexpected(badFile("the bench has an invalid player id"));
            }
            lineup.bench.push_back(*player);
        }
    }
    lineup.formation = core::formationName(lineup.pitchSlots);
    return lineup;
}

core::Status writeTacticFile(const QString& path, const core::Lineup& lineup) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(core::Error{
            core::ErrorCode::Io,
            std::format("tactic file: cannot write: {}", file.errorString().toStdString()),
            std::string(kFileField)});
    }
    const QByteArray json = toTacticJson(lineup);
    if (file.write(json) != json.size() || !file.commit()) {
        return std::unexpected(core::Error{
            core::ErrorCode::Io,
            std::format("tactic file: cannot write: {}", file.errorString().toStdString()),
            std::string(kFileField)});
    }
    return {};
}

core::Result<core::Lineup> readTacticFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(core::Error{
            core::ErrorCode::Io,
            std::format("tactic file: cannot read: {}", file.errorString().toStdString()),
            std::string(kFileField)});
    }
    if (file.size() > kMaxTacticFileBytes) {
        return std::unexpected(core::Error{core::ErrorCode::InvalidInput,
                                           std::format("tactic file: {} bytes", file.size()),
                                           "tactic_file_size"});
    }
    // read() with a limit: size() may be 0 for a special file.
    const QByteArray json = file.read(kMaxTacticFileBytes + 1);
    if (json.size() > kMaxTacticFileBytes) {
        return std::unexpected(core::Error{core::ErrorCode::InvalidInput,
                                           "tactic file: larger than the limit",
                                           "tactic_file_size"});
    }
    return fromTacticJson(json);
}

} // namespace fdss::data
