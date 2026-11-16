#include "data/readers/TacticJson.hpp"

#include "core/algorithms/Formations.hpp"

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <functional>
#include <string>

using namespace fdss::core;
using namespace fdss::data;

namespace {

Lineup sample() {
    Lineup lineup = emptyLineup(*findFormation("4-3-3"));
    for (std::size_t i = 0; i < lineup.pitchSlots.size(); ++i) {
        if (i != 6) { // the defensive midfielder stays free
            lineup.pitchSlots[i].player = PlayerId{static_cast<std::int64_t>(i + 10)};
        }
    }
    lineup.bench = {PlayerId{30}, PlayerId{31}};
    return lineup;
}

// The sample as a JSON object, changed by `edit`, back to text.
QByteArray edited(const std::function<void(QJsonObject&)>& edit) {
    QJsonObject root = QJsonDocument::fromJson(toTacticJson(sample())).object();
    edit(root);
    return QJsonDocument(root).toJson();
}

QByteArray editedSlot(int index, const std::function<void(QJsonObject&)>& edit) {
    return edited([&](QJsonObject& root) {
        QJsonArray slotArray = root.value("slots").toArray();
        QJsonObject slot = slotArray.at(index).toObject();
        edit(slot);
        slotArray.replace(index, slot);
        root.insert("slots", slotArray);
    });
}

void expectRejected(const QByteArray& json, ErrorCode code = ErrorCode::UnsupportedFormat,
                    const std::string& field = "tactic_file") {
    const auto lineup = fromTacticJson(json);
    ASSERT_FALSE(lineup.has_value()) << json.toStdString();
    EXPECT_EQ(lineup.error().code, code) << lineup.error().detail;
    EXPECT_EQ(lineup.error().field, field);
}

} // namespace

TEST(TacticJsonTest, RoundTrip) {
    const Lineup lineup = sample();
    const auto read = fromTacticJson(toTacticJson(lineup));
    ASSERT_TRUE(read.has_value()) << read.error().detail;
    EXPECT_EQ(*read, lineup);
    const auto compact = fromTacticJson(toTacticJson(lineup, QJsonDocument::Compact));
    ASSERT_TRUE(compact.has_value());
    EXPECT_EQ(*compact, lineup);
}

TEST(TacticJsonTest, WritesTheSpecFormat) {
    const QJsonObject root = QJsonDocument::fromJson(toTacticJson(sample())).object();
    EXPECT_EQ(root.value("version").toInt(), 1);
    EXPECT_EQ(root.value("formation").toString(), "4-3-3");
    const QJsonObject goalkeeper = root.value("slots").toArray().at(0).toObject();
    EXPECT_EQ(goalkeeper.value("position").toString(), "GK");
    EXPECT_DOUBLE_EQ(goalkeeper.value("x").toDouble(), 0.5);
    EXPECT_DOUBLE_EQ(goalkeeper.value("y").toDouble(), 0.94);
    EXPECT_EQ(goalkeeper.value("playerId").toInteger(), 10);
    EXPECT_TRUE(root.value("slots").toArray().at(6).toObject().value("playerId").isNull());
    EXPECT_EQ(root.value("bench").toArray(), (QJsonArray{30, 31}));
}

// Free positions (P12 decision 12): any valid positions and coordinates come back as they were.
TEST(TacticJsonTest, RoundTripOfAnAsymmetricShape) {
    Lineup lineup = sample();
    ASSERT_EQ(moveSlot(lineup, 4, Position::Lwb), LineupEdit::Done);
    ASSERT_EQ(moveSlot(lineup, 9, Position::Rw, 0.9), LineupEdit::Done);
    lineup.pitchSlots[1].x = 0.97; // as another tool might write it
    lineup.pitchSlots[1].y = 0.83;
    ASSERT_EQ(matchingFormation(lineup.pitchSlots), nullptr);
    const auto read = fromTacticJson(toTacticJson(lineup));
    ASSERT_TRUE(read.has_value()) << read.error().detail;
    EXPECT_EQ(*read, lineup);
    EXPECT_EQ(read->formation, "3-2-2-3");
}

TEST(TacticJsonTest, TheNameIsCountedFromTheSlots) {
    const auto read = fromTacticJson(edited(
        [](QJsonObject& root) { root.insert("formation", "whatever the other tool calls it"); }));
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->formation, "4-3-3");
}

TEST(TacticJsonTest, ReadsPositionsInAnyCaseAndWithoutOptionalKeys) {
    const auto read = fromTacticJson(edited([](QJsonObject& root) {
        root.remove("bench");
        QJsonArray slotArray = root.value("slots").toArray();
        QJsonObject first = slotArray.at(0).toObject();
        first.insert("position", "gK");
        first.remove("playerId");
        slotArray.replace(0, first);
        root.insert("slots", slotArray);
        root.insert("comment", "unknown keys are ignored");
    }));
    ASSERT_TRUE(read.has_value()) << read.error().detail;
    EXPECT_EQ(read->pitchSlots[0].position, Position::Gk);
    EXPECT_EQ(read->pitchSlots[0].player, std::nullopt);
    EXPECT_TRUE(read->bench.empty());
}

TEST(TacticJsonTest, RejectsANewerVersion) {
    expectRejected(edited([](QJsonObject& root) { root.insert("version", 2); }),
                   ErrorCode::IncompatibleVersion, "tactic_version");
}

TEST(TacticJsonTest, RejectsAWrongVersion) {
    expectRejected(edited([](QJsonObject& root) { root.insert("version", 0); }));
    expectRejected(edited([](QJsonObject& root) { root.insert("version", 1.5); }));
    expectRejected(edited([](QJsonObject& root) { root.insert("version", "1"); }));
    expectRejected(edited([](QJsonObject& root) { root.remove("version"); }));
}

TEST(TacticJsonTest, RejectsABrokenFile) {
    const QByteArray json = toTacticJson(sample());
    expectRejected(json.left(json.size() / 2));
    expectRejected("");
    expectRejected("[1, 2, 3]");
    expectRejected("\x89PNG\r\n\x1a\n");
}

TEST(TacticJsonTest, RejectsWrongStructure) {
    expectRejected(edited([](QJsonObject& root) { root.remove("formation"); }));
    expectRejected(edited([](QJsonObject& root) { root.insert("slots", QJsonArray{}); }));
    expectRejected(edited([](QJsonObject& root) { root.insert("slots", "eleven"); }));
    expectRejected(edited([](QJsonObject& root) {
        QJsonArray slotArray = root.value("slots").toArray();
        slotArray.append(slotArray.at(1));
        root.insert("slots", slotArray);
    }));
    expectRejected(edited([](QJsonObject& root) { root.insert("bench", 7); }));
    expectRejected(edited([](QJsonObject& root) {
        QJsonArray bench;
        for (int i = 0; i < 13; ++i) {
            bench.append(40 + i);
        }
        root.insert("bench", bench);
    }));
    expectRejected(edited([](QJsonObject& root) { root.insert("bench", QJsonArray{0}); }));
}

TEST(TacticJsonTest, RejectsWrongSlots) {
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("position", "cf"); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.remove("position"); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("x", 1.01); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("y", -0.1); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("y", "0.5"); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.remove("x"); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("playerId", 3.5); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("playerId", -4); }));
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("playerId", "12"); }));
    // A second goalkeeper.
    expectRejected(editedSlot(2, [](QJsonObject& slot) { slot.insert("position", "GK"); }));
}

TEST(TacticJsonTest, AcceptsDuplicatePlayersForTheValidator) {
    const auto read = fromTacticJson(editedSlot(2, [](QJsonObject& slot) {
        slot.insert("playerId", 13); // also in slot 3
    }));
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->pitchSlots[2].player, read->pitchSlots[3].player);
}

TEST(TacticJsonTest, FileRoundTrip) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.filePath("Plan A.tactic.json");
    ASSERT_TRUE(writeTacticFile(path, sample()).has_value());
    const auto read = readTacticFile(path);
    ASSERT_TRUE(read.has_value()) << read.error().detail;
    EXPECT_EQ(*read, sample());
}

TEST(TacticJsonTest, FileErrors) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const auto missing = readTacticFile(dir.filePath("missing.tactic.json"));
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::Io);
    EXPECT_EQ(missing.error().field, "tactic_file");

    const QString big = dir.filePath("big.tactic.json");
    {
        QFile file(big);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        ASSERT_EQ(file.write(QByteArray(kMaxTacticFileBytes + 1, ' ')), kMaxTacticFileBytes + 1);
    }
    const auto tooBig = readTacticFile(big);
    ASSERT_FALSE(tooBig.has_value());
    EXPECT_EQ(tooBig.error().code, ErrorCode::InvalidInput);
    EXPECT_EQ(tooBig.error().field, "tactic_file_size");

    const auto unwritable = writeTacticFile(dir.filePath("no/such/folder/x.tactic.json"), sample());
    ASSERT_FALSE(unwritable.has_value());
    EXPECT_EQ(unwritable.error().code, ErrorCode::Io);
}
