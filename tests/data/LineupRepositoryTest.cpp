#include "data/repositories/LineupRepository.hpp"

#include "RepositoryFixture.hpp"
#include "core/algorithms/Formations.hpp"

#include <gtest/gtest.h>

#include <optional>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;

namespace {

class LineupRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] LineupRepository repository() { return LineupRepository(*m_db); }

    static Lineup lineup(const char* formation = "4-4-2") {
        Lineup result = emptyLineup(*findFormation(formation));
        result.pitchSlots[0].player = kPlayer;
        result.bench = {kOtherPlayer};
        return result;
    }

    [[nodiscard]] QString matchFormation() const {
        return scalar(m_db->database(), "SELECT formation FROM match WHERE id = 1", {}).toString();
    }
};

} // namespace

TEST_F(LineupRepositoryTest, NothingSavedForAMatch) {
    const auto found = repository().findForMatch(kMatch);
    ASSERT_TRUE(found.has_value()) << found.error().detail;
    EXPECT_EQ(*found, std::nullopt);
}

TEST_F(LineupRepositoryTest, SaveAndReadTheLineupOfAMatch) {
    ASSERT_TRUE(repository().saveForMatch(kMatch, lineup()).has_value());
    const auto found = repository().findForMatch(kMatch);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, lineup());
    EXPECT_EQ(matchFormation(), "4-4-2");
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup WHERE match_id = 1 AND formation = '4-4-2'"), 1);
}

TEST_F(LineupRepositoryTest, SavingAgainReplacesTheRow) {
    ASSERT_TRUE(repository().saveForMatch(kMatch, lineup()).has_value());
    ASSERT_TRUE(repository().saveForMatch(kMatch, lineup("3-5-2")).has_value());
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup WHERE match_id = 1"), 1);
    EXPECT_EQ(repository().findForMatch(kMatch).value(), lineup("3-5-2"));
    EXPECT_EQ(matchFormation(), "3-5-2");
}

TEST_F(LineupRepositoryTest, OnlyAPlannedMatch) {
    ASSERT_TRUE(sql("UPDATE match SET status = 'finished', goals_for = 1, goals_against = 0"));
    const auto saved = repository().saveForMatch(kMatch, lineup());
    ASSERT_FALSE(saved.has_value());
    EXPECT_EQ(saved.error().code, ErrorCode::Conflict);
    EXPECT_EQ(count("SELECT COUNT(*) FROM lineup"), 0);

    const auto missing = repository().saveForMatch(MatchId{999}, lineup());
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(LineupRepositoryTest, RemoveForMatch) {
    ASSERT_TRUE(repository().saveForMatch(kMatch, lineup()).has_value());
    ASSERT_TRUE(repository().removeForMatch(kMatch).has_value());
    EXPECT_EQ(repository().findForMatch(kMatch).value(), std::nullopt);
    EXPECT_TRUE(repository().removeForMatch(kMatch).has_value()); // nothing left: no error
}

TEST_F(LineupRepositoryTest, ADamagedStoredLineupIsADatabaseError) {
    ASSERT_TRUE(sql("INSERT INTO lineup (match_id, formation, tactic_json) "
                    "VALUES (1, '4-4-2', '{\"version\": 1')"));
    const auto found = repository().findForMatch(kMatch);
    ASSERT_FALSE(found.has_value());
    EXPECT_EQ(found.error().code, ErrorCode::Database);
}

TEST_F(LineupRepositoryTest, Templates) {
    auto repo = repository();
    const auto first = repo.createTemplate("  Plan B ", lineup("4-3-3"));
    ASSERT_TRUE(first.has_value()) << first.error().detail;
    const auto second = repo.createTemplate("Plan A", lineup());
    ASSERT_TRUE(second.has_value());

    const auto list = repo.listTemplates();
    ASSERT_TRUE(list.has_value());
    EXPECT_EQ(*list, (std::vector<LineupTemplateRow>{{*second, "Plan A", "4-4-2"},
                                                     {*first, "Plan B", "4-3-3"}}));
    EXPECT_EQ(repo.getTemplate(*first).value(), lineup("4-3-3"));

    ASSERT_TRUE(repo.replaceTemplate(*first, lineup("5-3-2")).has_value());
    EXPECT_EQ(repo.getTemplate(*first).value(), lineup("5-3-2"));
    EXPECT_EQ(repo.listTemplates().value()[1].formation, "5-3-2");

    ASSERT_TRUE(repo.removeTemplate(*first).has_value());
    EXPECT_EQ(repo.getTemplate(*first).error().code, ErrorCode::NotFound);
    EXPECT_EQ(repo.removeTemplate(*first).error().code, ErrorCode::NotFound);
}

TEST_F(LineupRepositoryTest, TemplatesAndMatchLineupsDoNotMix) {
    auto repo = repository();
    ASSERT_TRUE(repo.saveForMatch(kMatch, lineup()).has_value());
    EXPECT_TRUE(repo.listTemplates().value().empty());
    const LineupId matchRow{count("SELECT id FROM lineup WHERE match_id = 1")};
    EXPECT_EQ(repo.getTemplate(matchRow).error().code, ErrorCode::NotFound);
    EXPECT_EQ(repo.removeTemplate(matchRow).error().code, ErrorCode::NotFound);
    EXPECT_EQ(repo.replaceTemplate(matchRow, lineup("3-4-3")).error().code, ErrorCode::NotFound);
}
