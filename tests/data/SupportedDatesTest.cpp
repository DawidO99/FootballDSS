// The way a date takes through the app: validate, write, read back. A date that passes validate()
// must come back unchanged, whichever field it is in.

#include "RepositoryFixture.hpp"
#include "core/domain/Validation.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string_view>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

constexpr std::array kSupportedEnds{1900y / January / 1d, 2100y / December / 31d};
// Year 0 is the case that used to be stored and then could not be read.
constexpr std::array kUnsupportedDates{1899y / December / 31d, 2101y / January / 1d,
                                       0y / January / 1d};

bool hasField(const ValidationErrors& errors, std::string_view field) {
    return std::ranges::any_of(errors,
                               [&](const ValidationError& error) { return error.field == field; });
}

class SupportedDatesTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    static void expectInvalidField(const Status& status, std::string_view field) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, ErrorCode::InvalidInput) << status.error().detail;
        EXPECT_EQ(status.error().field, field);
    }
};

} // namespace

TEST_F(SupportedDatesTest, SessionDateAtTheEndsComesBackUnchanged) {
    TrainingRepository repository(*m_db);
    for (const Date date : kSupportedEnds) {
        TrainingSessionDraft draft;
        draft.date = date;
        ASSERT_TRUE(validate(draft).empty());

        const auto id = repository.create(kTeam, draft);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        const auto session = repository.get(*id);
        ASSERT_TRUE(session.has_value()) << session.error().detail;
        EXPECT_EQ(session->date, date);
    }
    const auto sessions = repository.list(kTeam, {});
    ASSERT_TRUE(sessions.has_value()) << sessions.error().detail;
    EXPECT_EQ(sessions->size(), 3U);
}

TEST_F(SupportedDatesTest, SessionDateOutsideIsStoppedByValidation) {
    for (const Date date : kUnsupportedDates) {
        TrainingSessionDraft draft;
        draft.date = date;
        EXPECT_TRUE(hasField(validate(draft), "date"));
    }
}

TEST_F(SupportedDatesTest, MatchDateAtTheEndsComesBackUnchanged) {
    MatchRepository repository(*m_db);
    for (const Date date : kSupportedEnds) {
        MatchDraft draft;
        draft.opponentId = kOpponent;
        draft.date = date;
        ASSERT_TRUE(validate(draft).empty());

        const auto id = repository.create(kTeam, draft);
        ASSERT_TRUE(id.has_value()) << id.error().detail;
        const auto match = repository.get(*id);
        ASSERT_TRUE(match.has_value()) << match.error().detail;
        EXPECT_EQ(match->date, date);
    }
    const auto matches = repository.list(kTeam, {});
    ASSERT_TRUE(matches.has_value()) << matches.error().detail;
    EXPECT_EQ(matches->size(), 3U);
}

TEST_F(SupportedDatesTest, MatchDateOutsideIsStoppedByValidation) {
    for (const Date date : kUnsupportedDates) {
        MatchDraft draft;
        draft.opponentId = kOpponent;
        draft.date = date;
        EXPECT_TRUE(hasField(validate(draft), "date"));
    }
}

TEST_F(SupportedDatesTest, AvailabilityDatesAtTheEndsComeBackUnchanged) {
    AvailabilityRepository repository(*m_db);
    AvailabilityDraft draft;
    draft.fromDate = kSupportedEnds.front();
    draft.expectedTo = kSupportedEnds.back();
    draft.actualTo = kSupportedEnds.back();
    ASSERT_TRUE(validate(draft).empty());
    ASSERT_TRUE(repository.create(kPlayer, draft).has_value());

    const auto entries = repository.listByPlayer(kPlayer);
    ASSERT_TRUE(entries.has_value()) << entries.error().detail;
    ASSERT_EQ(entries->size(), 1U);
    EXPECT_EQ(entries->front().fromDate, kSupportedEnds.front());
    EXPECT_EQ(entries->front().expectedTo, kSupportedEnds.back());
    EXPECT_EQ(entries->front().actualTo, kSupportedEnds.back());
}

TEST_F(SupportedDatesTest, AvailabilityDatesOutsideAreStoppedByValidation) {
    for (const Date date : kUnsupportedDates) {
        AvailabilityDraft draft;
        draft.fromDate = date;
        draft.expectedTo = date;
        draft.actualTo = date;
        const ValidationErrors errors = validate(draft);
        EXPECT_TRUE(hasField(errors, "from_date"));
        EXPECT_TRUE(hasField(errors, "expected_to"));
        EXPECT_TRUE(hasField(errors, "actual_to"));
    }
}

// --- close() and archive() take a bare date, which no validate() sees ---

TEST_F(SupportedDatesTest, CloseAtTheEndComesBackUnchanged) {
    AvailabilityRepository repository(*m_db);
    AvailabilityDraft draft;
    draft.fromDate = kSupportedEnds.front();
    const auto id = repository.create(kPlayer, draft);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    ASSERT_TRUE(repository.close(*id, kSupportedEnds.back()).has_value());
    const auto entries = repository.listByPlayer(kPlayer);
    ASSERT_TRUE(entries.has_value()) << entries.error().detail;
    EXPECT_EQ(entries->front().actualTo, kSupportedEnds.back());
}

TEST_F(SupportedDatesTest, CloseOutsideIsInvalidInputAndChangesNothing) {
    AvailabilityRepository repository(*m_db);
    AvailabilityDraft draft;
    draft.fromDate = kSupportedEnds.front();
    const auto id = repository.create(kPlayer, draft);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    for (const Date date : kUnsupportedDates) {
        expectInvalidField(repository.close(*id, date), "actual_to");
    }
    const auto entries = repository.listByPlayer(kPlayer);
    ASSERT_TRUE(entries.has_value()) << entries.error().detail;
    EXPECT_EQ(entries->front().actualTo, std::nullopt);
}

TEST_F(SupportedDatesTest, ArchiveAtTheEndsComesBackUnchanged) {
    PlayerRepository repository(*m_db);
    for (const Date date : kSupportedEnds) {
        ASSERT_TRUE(repository.archive(kPlayer, date).has_value());
        const auto player = repository.get(kPlayer);
        ASSERT_TRUE(player.has_value()) << player.error().detail;
        EXPECT_EQ(player->archivedAt, date);
        ASSERT_TRUE(repository.unarchive(kPlayer).has_value());
    }
}

TEST_F(SupportedDatesTest, ArchiveOutsideIsInvalidInputAndChangesNothing) {
    PlayerRepository repository(*m_db);
    for (const Date date : kUnsupportedDates) {
        expectInvalidField(repository.archive(kPlayer, date), "archived_at");
    }
    const auto player = repository.get(kPlayer);
    ASSERT_TRUE(player.has_value()) << player.error().detail;
    EXPECT_EQ(player->archivedAt, std::nullopt);
}
