#include "RepositoryFixture.hpp"

#include "data/repositories/CapabilityRepository.hpp"

#include <chrono>

using namespace fdss;
using namespace std::chrono;

namespace {

constexpr core::Date kToday = 2026y / November / 20d;

class CapabilityRepositoryTest : public tests::RepositoryTest {
protected:
    core::DataCounts countsOf(core::TeamId team) const {
        const data::CapabilityRepository repository(*m_db);
        auto counts = repository.counts(team, kToday);
        EXPECT_TRUE(counts.has_value()) << (counts ? "" : counts.error().detail);
        return counts.value_or(core::DataCounts{});
    }
};

} // namespace

TEST_F(CapabilityRepositoryTest, EmptyTeamCountsNothing) {
    seedTeams();
    EXPECT_EQ(countsOf(kTeam), core::DataCounts{});
}

TEST_F(CapabilityRepositoryTest, CountsActivePlayersOfTheTeamOnly) {
    seedPlayers();
    ASSERT_TRUE(sql("UPDATE player SET archived_at = '2026-10-01' WHERE id = 2"));
    ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, primary_position) "
                    "VALUES (3, 2, 'Piotr', 'Inny', 'gk')"));
    EXPECT_EQ(countsOf(kTeam).activePlayers, 1);
    EXPECT_EQ(countsOf(kOtherTeam).activePlayers, 1);
}

TEST_F(CapabilityRepositoryTest, CountsFinishedMatchesAndResults) {
    seedCalendar();
    // Match 1 stays planned; 2 finished with a result; 3 finished without one; 4 live.
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, status, goals_for, "
                    "goals_against) VALUES (2, 1, 1, '2026-10-01', 'home', 'finished', 2, 1)"));
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, status) "
                    "VALUES (3, 1, 1, '2026-10-08', 'away', 'finished')"));
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, status, goals_for, "
                    "goals_against) VALUES (4, 1, 1, '2026-10-15', 'home', 'live', 0, 0)"));
    const core::DataCounts counts = countsOf(kTeam);
    EXPECT_EQ(counts.finishedMatches, 2);
    EXPECT_EQ(counts.matchesWithResult, 1);
}

TEST_F(CapabilityRepositoryTest, RpeDaysAreDistinctDatesThroughToday) {
    seedCalendar();
    // Session 1 (2026-11-02): two players with RPE, one date.
    ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status, rpe) "
                    "VALUES (1, 1, 'present', 5), (1, 2, 'present', 6)"));
    // A session without RPE and an absent row do not count.
    ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                    "VALUES (2, 1, '2026-11-03', 60, 'training')"));
    ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status) "
                    "VALUES (2, 1, 'present'), (2, 2, 'absent')"));
    // After today: not counted.
    ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                    "VALUES (3, 1, '2026-11-25', 60, 'training')"));
    ASSERT_TRUE(sql("INSERT INTO attendance (session_id, player_id, status, rpe) "
                    "VALUES (3, 1, 'present', 4)"));
    // A finished match with an RPE on another date; the planned match 1 with an RPE does not count.
    ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue, status) "
                    "VALUES (2, 1, 1, '2026-10-24', 'home', 'finished')"));
    ASSERT_TRUE(sql("INSERT INTO appearance (match_id, player_id, rpe) VALUES (2, 1, 8)"));
    ASSERT_TRUE(sql("INSERT INTO appearance (match_id, player_id, rpe) VALUES (1, 1, 8)"));
    const core::DataCounts counts = countsOf(kTeam);
    EXPECT_EQ(counts.rpeDays, 2);
    // 2026-10-24 through 2026-11-20, both included.
    EXPECT_EQ(counts.daysSinceFirstRpe, 28);
    EXPECT_EQ(countsOf(kOtherTeam).rpeDays, 0);
}

TEST_F(CapabilityRepositoryTest, RatingEntriesSumRatingsAttributesAndMetrics) {
    seedCalendar();
    ASSERT_TRUE(sql("INSERT INTO appearance (match_id, player_id, rating) VALUES (1, 1, 7)"));
    ASSERT_TRUE(sql("INSERT INTO appearance (match_id, player_id) VALUES (1, 2)"));
    ASSERT_TRUE(sql("INSERT INTO player_attribute (player_id, attribute, rating, date) "
                    "VALUES (1, 'pace', 6, '2026-11-01')"));
    ASSERT_TRUE(sql("INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, "
                    "aggregation, scope) VALUES (1, 'km', 'Dystans', 'Distance', 'decimal', 'sum', "
                    "'match')"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, match_id, date, value) "
                    "VALUES (1, 2, 1, '2026-11-01', 9.5)"));
    EXPECT_EQ(countsOf(kTeam).ratingEntries, 3);
    EXPECT_EQ(countsOf(kOtherTeam).ratingEntries, 0);
}

TEST_F(CapabilityRepositoryTest, CountsEventDataFilesOfTheTeam) {
    seedCalendar();
    ASSERT_TRUE(sql("INSERT INTO event_data_file (match_id, provider, path, hash) "
                    "VALUES (1, 'statsbomb', 'a.json', 'abc')"));
    EXPECT_EQ(countsOf(kTeam).eventFiles, 1);
    EXPECT_EQ(countsOf(kOtherTeam).eventFiles, 0);
}
