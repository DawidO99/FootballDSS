#include "data/repositories/MatchNoteRepository.hpp"

#include "RepositoryFixture.hpp"

#include <gtest/gtest.h>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;

namespace {

class MatchNoteRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] MatchNoteRepository repository() { return MatchNoteRepository(*m_db); }

    static MatchNoteDraft note(int minute, std::optional<int> added, const char* text,
                               std::optional<PlayerId> player = std::nullopt) {
        return MatchNoteDraft{minute, added, player, text};
    }
};

} // namespace

TEST_F(MatchNoteRepositoryTest, NotesKeepEveryFieldAndComeInTheOrderOfTheMatch) {
    auto notes = repository();
    const MatchNoteId later =
        notes.add(kMatch, note(60, std::nullopt, "Zmiana ustawienia"), 3).value();
    const MatchNoteId start = notes.add(kMatch, note(45, std::nullopt, "Druga połowa"), 4).value();
    const MatchNoteId stoppage = notes.add(kMatch, note(45, 2, "Uraz kostki", kPlayer), 2).value();
    const auto list = notes.list(kMatch);
    ASSERT_TRUE(list.has_value()) << list.error().detail;
    ASSERT_EQ(list->size(), 3U);
    // Added time of the first half before the minute that starts the second.
    EXPECT_EQ(list->at(0).id, stoppage);
    EXPECT_EQ(list->at(1).id, start);
    EXPECT_EQ(list->at(2).id, later);
    EXPECT_EQ(list->at(0).playerId, kPlayer);
    EXPECT_EQ(list->at(0).addedTime, 2);
    EXPECT_EQ(list->at(0).text, "Uraz kostki");
    EXPECT_EQ(list->at(0).seq, 2);
    EXPECT_EQ(list->at(0).matchId, kMatch);
}

TEST_F(MatchNoteRepositoryTest, UpdateKeepsTheSeqAndRemoveReturnsTheNote) {
    auto notes = repository();
    const MatchNoteId id = notes.add(kMatch, note(10, std::nullopt, "a"), 5).value();
    ASSERT_TRUE(notes.update(kMatch, id, note(12, std::nullopt, "b", kOtherPlayer)));
    const auto updated = notes.get(kMatch, id);
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->minute, 12);
    EXPECT_EQ(updated->text, "b");
    EXPECT_EQ(updated->playerId, kOtherPlayer);
    EXPECT_EQ(updated->seq, 5);
    const auto removed = notes.remove(kMatch, id);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(removed->text, "b");
    EXPECT_EQ(notes.remove(kMatch, id).error().code, ErrorCode::NotFound);
    EXPECT_EQ(notes.update(kMatch, id, note(1, std::nullopt, "c")).error().code,
              ErrorCode::NotFound);
}

TEST_F(MatchNoteRepositoryTest, DeleteWithSeqTakesOnlyThatAction) {
    auto notes = repository();
    ASSERT_TRUE(notes.add(kMatch, note(10, std::nullopt, "a"), 1));
    ASSERT_TRUE(notes.add(kMatch, note(11, std::nullopt, "b"), 2));
    const auto deleted = notes.deleteWithSeq(kMatch, 2);
    ASSERT_TRUE(deleted.has_value());
    ASSERT_EQ(deleted->size(), 1U);
    EXPECT_EQ(deleted->front().text, "b");
    EXPECT_EQ(notes.list(kMatch)->size(), 1U);
}

TEST_F(MatchNoteRepositoryTest, InvalidNotesAreInvalidInput) {
    auto notes = repository();
    EXPECT_EQ(notes.add(kMatch, note(10, std::nullopt, " "), 1).error().code,
              ErrorCode::InvalidInput);
    EXPECT_EQ(notes.add(kMatch, note(10, std::nullopt, "x", kNoPlayer), 1).error().code,
              ErrorCode::InvalidInput);
    EXPECT_EQ(notes.add(MatchId{999}, note(10, std::nullopt, "x"), 1).error().code,
              ErrorCode::InvalidInput);
}
