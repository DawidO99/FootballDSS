#include "data/repositories/MatchRepository.hpp"

#include "RepositoryFixture.hpp"
#include "data/db/Transaction.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace fdss::core;
using namespace fdss::data;
using namespace fdss::tests;
using namespace std::chrono;

namespace {

constexpr Date kDate = 2026y / November / 7d;
constexpr MatchId kNoMatch{999};

// kMatch of the fixture is on this date.
constexpr Date kSeededMatchDate = 2026y / November / 1d;

class MatchRepositoryTest : public RepositoryTest {
protected:
    void SetUp() override {
        RepositoryTest::SetUp();
        seedCalendar();
    }

    [[nodiscard]] MatchRepository repository() { return MatchRepository(*m_db); }

    static MatchDraft draft(Date date = kDate) {
        MatchDraft result;
        result.opponentId = kOpponent;
        result.date = date;
        return result;
    }

    MatchId create(Date date = kDate, TeamId team = kTeam,
                   std::optional<std::string> competition = std::nullopt) {
        MatchDraft match = draft(date);
        match.competition = std::move(competition);
        const auto id = repository().create(team, match);
        EXPECT_TRUE(id.has_value()) << id.error().detail;
        return id.value_or(MatchId{});
    }

    static AppearanceDraft appearance(PlayerId player, std::optional<int> rating = std::nullopt) {
        AppearanceDraft result;
        result.playerId = player;
        result.rating = rating;
        return result;
    }

    static MatchEventDraft event(int minute, MatchEventType type = MatchEventType::Goal,
                                 std::optional<PlayerId> player = kPlayer) {
        MatchEventDraft result;
        result.minute = minute;
        result.type = type;
        result.playerId = player;
        return result;
    }

    void expectCode(const Status& status, ErrorCode code) {
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().code, code) << status.error().detail;
    }

    [[nodiscard]] std::vector<MatchId> idsIn(DateRange range) {
        const auto matches = repository().list(kTeam, range);
        EXPECT_TRUE(matches.has_value());
        std::vector<MatchId> ids;
        for (const Match& match : matches.value_or(std::vector<Match>{})) {
            ids.push_back(match.id);
        }
        return ids;
    }
};

} // namespace

// --- Matches ---

TEST_F(MatchRepositoryTest, CreatedMatchIsPlannedAndKeepsEveryField) {
    MatchDraft full = draft();
    full.kickoffTime = minutes{18 * 60 + 30};
    full.venue = Venue::Away;
    full.competition = "  Puchar Polski ";
    const auto id = repository().create(kTeam, full);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto match = repository().get(*id);
    ASSERT_TRUE(match.has_value()) << match.error().detail;
    EXPECT_EQ(match->id, *id);
    EXPECT_EQ(match->teamId, kTeam);
    EXPECT_EQ(match->opponentId, kOpponent);
    EXPECT_EQ(match->date, kDate);
    EXPECT_EQ(match->kickoffTime, minutes{18 * 60 + 30});
    EXPECT_EQ(match->venue, Venue::Away);
    EXPECT_EQ(match->competition, "Puchar Polski");
    EXPECT_EQ(match->status, MatchStatus::Planned);
    EXPECT_EQ(match->goalsFor, std::nullopt);
    EXPECT_EQ(match->goalsAgainst, std::nullopt);
    EXPECT_EQ(match->formation, std::nullopt);
}

TEST_F(MatchRepositoryTest, FormationIsRead) {
    ASSERT_TRUE(sql("UPDATE match SET formation = '4-3-3' WHERE id = 1"));
    EXPECT_EQ(repository().get(kMatch)->formation, "4-3-3");
}

TEST_F(MatchRepositoryTest, GetOfAMissingMatchIsNotFound) {
    const auto match = repository().get(kNoMatch);
    ASSERT_FALSE(match.has_value());
    EXPECT_EQ(match.error().code, ErrorCode::NotFound);
}

TEST_F(MatchRepositoryTest, MatchWithMissingParentsIsInvalidInput) {
    const auto noTeam = repository().create(kNoTeam, draft());
    ASSERT_FALSE(noTeam.has_value());
    EXPECT_EQ(noTeam.error().code, ErrorCode::InvalidInput);

    MatchDraft noOpponent = draft();
    noOpponent.opponentId = OpponentId{999};
    const auto id = repository().create(kTeam, noOpponent);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(MatchRepositoryTest, ImpossibleKickoffIsInvalidInput) {
    MatchDraft bad = draft();
    bad.kickoffTime = minutes{24 * 60};
    const auto id = repository().create(kTeam, bad);
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(MatchRepositoryTest, ListIsLimitedToTheTeamAndSortedByDate) {
    const MatchId later = create(2026y / December / 1d);
    const MatchId earlier = create(2026y / October / 1d);
    create(kDate, kOtherTeam);
    EXPECT_EQ(idsIn({}), (std::vector{earlier, kMatch, later}));
}

TEST_F(MatchRepositoryTest, ListRangeIncludesBothEnds) {
    const Date from = 2026y / November / 7d;
    const Date to = 2026y / November / 14d;
    create(2026y / November / 6d);
    const MatchId first = create(from);
    const MatchId last = create(to);
    create(2026y / November / 15d);
    EXPECT_EQ(idsIn({from, to}), (std::vector{first, last}));
}

TEST_F(MatchRepositoryTest, ListRangeCanBeOpenOnEitherSide) {
    const MatchId later = create(2026y / December / 1d);
    EXPECT_EQ(idsIn({std::nullopt, kSeededMatchDate}), std::vector{kMatch});
    EXPECT_EQ(idsIn({2026y / November / 2d, std::nullopt}), std::vector{later});
}

TEST_F(MatchRepositoryTest, UpdatePlannedChangesTheEditableFields) {
    ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (2, 'Orzeł', 'orzeł')"));
    MatchDraft changed = draft();
    changed.opponentId = OpponentId{2};
    changed.kickoffTime = minutes{600};
    changed.venue = Venue::Neutral;
    changed.competition = "Liga";
    ASSERT_TRUE(repository().updatePlanned(kMatch, changed).has_value());

    const auto match = repository().get(kMatch);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->opponentId, OpponentId{2});
    EXPECT_EQ(match->date, kDate);
    EXPECT_EQ(match->kickoffTime, minutes{600});
    EXPECT_EQ(match->venue, Venue::Neutral);
    EXPECT_EQ(match->competition, "Liga");
    EXPECT_EQ(match->status, MatchStatus::Planned);
}

TEST_F(MatchRepositoryTest, UpdateOfAMatchThatIsNotPlannedIsAConflict) {
    for (const MatchStatus status : {MatchStatus::Live, MatchStatus::Finished}) {
        ASSERT_TRUE(repository().setResult(kMatch, std::nullopt, std::nullopt, status).has_value());
        expectCode(repository().updatePlanned(kMatch, draft()), ErrorCode::Conflict);
        EXPECT_EQ(repository().get(kMatch)->date, kSeededMatchDate);
    }
}

TEST_F(MatchRepositoryTest, UpdateOfAMissingMatchIsNotFound) {
    expectCode(repository().updatePlanned(kNoMatch, draft()), ErrorCode::NotFound);
}

TEST_F(MatchRepositoryTest, UpdateWithAMissingOpponentIsInvalidInput) {
    MatchDraft changed = draft();
    changed.opponentId = OpponentId{999};
    expectCode(repository().updatePlanned(kMatch, changed), ErrorCode::InvalidInput);
}

// --- Deleting a planned match ---

TEST_F(MatchRepositoryTest, RemovePlannedDeletesTheMatchWithItsChildRows) {
    const std::vector appearances{appearance(kPlayer)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, appearances).has_value());
    ASSERT_TRUE(repository().appendEvent(kMatch, event(10)).has_value());

    ASSERT_TRUE(repository().removePlanned(kMatch).has_value());
    EXPECT_EQ(count("SELECT count(*) FROM match"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM appearance"), 0);
    EXPECT_EQ(count("SELECT count(*) FROM match_event"), 0);
}

TEST_F(MatchRepositoryTest, RemoveOfAMatchThatIsNotPlannedIsAConflict) {
    for (const MatchStatus status : {MatchStatus::Live, MatchStatus::Finished}) {
        ASSERT_TRUE(repository().setResult(kMatch, std::nullopt, std::nullopt, status).has_value());
        expectCode(repository().removePlanned(kMatch), ErrorCode::Conflict);
        EXPECT_EQ(count("SELECT count(*) FROM match"), 1);
    }
}

TEST_F(MatchRepositoryTest, RemoveOfAMissingMatchIsNotFound) {
    expectCode(repository().removePlanned(kNoMatch), ErrorCode::NotFound);
}

// Known limitation in the P06 Decisions.
TEST_F(MatchRepositoryTest, RemoveOfAMatchWithMetricValuesIsAConflict) {
    ASSERT_TRUE(sql("INSERT INTO metric_definition (id, key, name_pl, name_en, value_type, "
                    "aggregation, scope) VALUES (1, 'k', 'x', 'x', 'integer', 'sum', 'match')"));
    ASSERT_TRUE(sql("INSERT INTO metric_value (metric_id, player_id, match_id, date, value) "
                    "VALUES (1, 1, 1, '2026-11-01', 5)"));
    expectCode(repository().removePlanned(kMatch), ErrorCode::Conflict);
    EXPECT_EQ(count("SELECT count(*) FROM match"), 1);
}

// --- Result and status ---

TEST_F(MatchRepositoryTest, SetResultStoresGoalsAndStatus) {
    ASSERT_TRUE(repository().setResult(kMatch, 3, 1, MatchStatus::Finished).has_value());
    const auto match = repository().get(kMatch);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->goalsFor, 3);
    EXPECT_EQ(match->goalsAgainst, 1);
    EXPECT_EQ(match->status, MatchStatus::Finished);
}

TEST_F(MatchRepositoryTest, SetResultCanChangeOnlyTheStatus) {
    ASSERT_TRUE(
        repository().setResult(kMatch, std::nullopt, std::nullopt, MatchStatus::Live).has_value());
    const auto match = repository().get(kMatch);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->status, MatchStatus::Live);
    EXPECT_EQ(match->goalsFor, std::nullopt);
}

TEST_F(MatchRepositoryTest, NegativeGoalsAreInvalidInput) {
    expectCode(repository().setResult(kMatch, -1, 0, MatchStatus::Finished),
               ErrorCode::InvalidInput);
}

// The schema has no upper limit on goals; a plain conversion to int would read this as 0.
TEST_F(MatchRepositoryTest, GoalsThatDoNotFitAnIntAreADatabaseError) {
    ASSERT_TRUE(sql("UPDATE match SET goals_for = 4294967296 WHERE id = 1"));
    const auto match = repository().get(kMatch);
    ASSERT_FALSE(match.has_value());
    EXPECT_EQ(match.error().code, ErrorCode::Database);
    EXPECT_NE(match.error().detail.find("goals_for"), std::string::npos) << match.error().detail;
}

TEST_F(MatchRepositoryTest, SetResultOfAMissingMatchIsNotFound) {
    expectCode(repository().setResult(kNoMatch, 1, 1, MatchStatus::Finished), ErrorCode::NotFound);
}

// --- Competitions ---

TEST_F(MatchRepositoryTest, CompetitionsAreDistinctAndMostRecentFirst) {
    create(2026y / September / 1d, kTeam, "Puchar");
    create(2026y / October / 1d, kTeam, "Liga");
    create(2026y / August / 1d, kTeam, "Liga");
    create(2026y / July / 1d, kTeam, "Sparing");
    create(2026y / December / 1d, kOtherTeam, "Inna drużyna");
    create(2026y / December / 2d, kTeam, std::nullopt);
    create(2026y / December / 3d, kTeam, "   ");

    const auto competitions = repository().listCompetitions(kTeam);
    ASSERT_TRUE(competitions.has_value()) << competitions.error().detail;
    EXPECT_EQ(*competitions, (std::vector<std::string>{"Liga", "Puchar", "Sparing"}));
}

TEST_F(MatchRepositoryTest, NoCompetitionsIsAnEmptyList) {
    const auto competitions = repository().listCompetitions(kTeam);
    ASSERT_TRUE(competitions.has_value());
    EXPECT_TRUE(competitions->empty());
}

// --- Appearances ---

TEST_F(MatchRepositoryTest, AppearancesKeepEveryField) {
    AppearanceDraft full = appearance(kPlayer, 8);
    full.started = true;
    full.minuteOn = 0;
    full.minuteOff = 75;
    full.position = Position::Cm;
    full.rpe = 7;
    const std::vector appearances{full, appearance(kOtherPlayer)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, appearances).has_value());

    const auto stored = repository().listAppearances(kMatch);
    ASSERT_TRUE(stored.has_value()) << stored.error().detail;
    ASSERT_EQ(stored->size(), 2U);
    const Appearance& first = stored->at(0);
    EXPECT_EQ(first.matchId, kMatch);
    EXPECT_EQ(first.playerId, kPlayer);
    EXPECT_TRUE(first.started);
    EXPECT_EQ(first.minuteOn, 0);
    EXPECT_EQ(first.minuteOff, 75);
    EXPECT_EQ(first.position, Position::Cm);
    EXPECT_EQ(first.rating, 8);
    EXPECT_EQ(first.rpe, 7);

    const Appearance& second = stored->at(1);
    EXPECT_EQ(second.playerId, kOtherPlayer);
    EXPECT_FALSE(second.started);
    EXPECT_EQ(second.minuteOn, std::nullopt);
    EXPECT_EQ(second.position, std::nullopt);
}

TEST_F(MatchRepositoryTest, ReplaceAppearancesReplacesAllRows) {
    const std::vector before{appearance(kPlayer), appearance(kOtherPlayer)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, before).has_value());
    const std::vector after{appearance(kOtherPlayer, 9)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, after).has_value());

    const auto stored = repository().listAppearances(kMatch);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->at(0).playerId, kOtherPlayer);
    EXPECT_EQ(stored->at(0).rating, 9);
}

TEST_F(MatchRepositoryTest, ReplaceWithAnEmptyListClearsTheRows) {
    const std::vector before{appearance(kPlayer)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, before).has_value());
    ASSERT_TRUE(repository().replaceAppearances(kMatch, {}).has_value());
    EXPECT_TRUE(repository().listAppearances(kMatch)->empty());
}

TEST_F(MatchRepositoryTest, ReplaceAppearancesIsAtomic) {
    const std::vector before{appearance(kPlayer, 5)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, before).has_value());

    // The first new row is fine; the second one breaks the CHECK on rating.
    const std::vector broken{appearance(kPlayer, 9), appearance(kOtherPlayer, 11)};
    expectCode(repository().replaceAppearances(kMatch, broken), ErrorCode::InvalidInput);

    const auto stored = repository().listAppearances(kMatch);
    ASSERT_TRUE(stored.has_value());
    ASSERT_EQ(stored->size(), 1U);
    EXPECT_EQ(stored->at(0).rating, 5);
}

TEST_F(MatchRepositoryTest, SamePlayerTwiceIsAConflictAndChangesNothing) {
    const std::vector before{appearance(kOtherPlayer)};
    ASSERT_TRUE(repository().replaceAppearances(kMatch, before).has_value());

    const std::vector twice{appearance(kPlayer), appearance(kPlayer)};
    const auto status = repository().replaceAppearances(kMatch, twice);
    ASSERT_FALSE(status.has_value());
    EXPECT_EQ(status.error().code, ErrorCode::Conflict);
    EXPECT_EQ(status.error().field, "player_id");
    EXPECT_EQ(repository().listAppearances(kMatch)->at(0).playerId, kOtherPlayer);
}

TEST_F(MatchRepositoryTest, AppearanceOfAMissingPlayerIsInvalidInput) {
    const std::vector missing{appearance(kNoPlayer)};
    expectCode(repository().replaceAppearances(kMatch, missing), ErrorCode::InvalidInput);
}

TEST_F(MatchRepositoryTest, ReplaceForAMissingMatchIsNotFoundEvenWithoutRows) {
    expectCode(repository().replaceAppearances(kNoMatch, {}), ErrorCode::NotFound);
    const std::vector rows{appearance(kPlayer)};
    expectCode(repository().replaceAppearances(kNoMatch, rows), ErrorCode::NotFound);
}

TEST_F(MatchRepositoryTest, ReplaceJoinsATransactionOfTheCaller) {
    {
        auto outer = Transaction::begin(*m_db);
        ASSERT_TRUE(outer.has_value());
        const std::vector rows{appearance(kPlayer)};
        ASSERT_TRUE(repository().replaceAppearances(kMatch, rows).has_value());
    }
    EXPECT_TRUE(repository().listAppearances(kMatch)->empty());
}

TEST_F(MatchRepositoryTest, AppearancesOfAnotherMatchStayUntouched) {
    const MatchId other = create();
    const std::vector rows{appearance(kPlayer)};
    ASSERT_TRUE(repository().replaceAppearances(other, rows).has_value());
    ASSERT_TRUE(repository().replaceAppearances(kMatch, rows).has_value());
    ASSERT_TRUE(repository().replaceAppearances(kMatch, {}).has_value());
    EXPECT_EQ(repository().listAppearances(other)->size(), 1U);
}

// --- Events ---

TEST_F(MatchRepositoryTest, EventsKeepEveryField) {
    MatchEventDraft sub = event(60, MatchEventType::Sub);
    sub.relatedPlayerId = kOtherPlayer;
    const auto id = repository().appendEvent(kMatch, sub);
    ASSERT_TRUE(id.has_value()) << id.error().detail;

    const auto events = repository().listEvents(kMatch);
    ASSERT_TRUE(events.has_value()) << events.error().detail;
    ASSERT_EQ(events->size(), 1U);
    EXPECT_EQ(events->at(0).id, *id);
    EXPECT_EQ(events->at(0).matchId, kMatch);
    EXPECT_EQ(events->at(0).minute, 60);
    EXPECT_EQ(events->at(0).type, MatchEventType::Sub);
    EXPECT_EQ(events->at(0).playerId, kPlayer);
    EXPECT_EQ(events->at(0).relatedPlayerId, kOtherPlayer);
    EXPECT_EQ(events->at(0).addedTime, std::nullopt);
}

TEST_F(MatchRepositoryTest, AddedTimeIsKeptAndReturnedByDeleteLast) {
    MatchEventDraft late = event(45, MatchEventType::Goal);
    late.addedTime = 3;
    ASSERT_TRUE(repository().appendEvent(kMatch, late));
    EXPECT_EQ(repository().listEvents(kMatch)->at(0).addedTime, 3);
    const auto deleted = repository().deleteLastEvent(kMatch);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(deleted->addedTime, 3);
}

TEST_F(MatchRepositoryTest, StatusAloneAndTheLiveMatchOfATeam) {
    ASSERT_TRUE(repository().setResult(kMatch, 2, 1, MatchStatus::Finished));
    ASSERT_TRUE(repository().setStatus(kMatch, MatchStatus::Live));
    const auto match = repository().get(kMatch);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->status, MatchStatus::Live);
    EXPECT_EQ(match->goalsFor, 2);
    EXPECT_EQ(repository().findLive(kTeam).value(), kMatch);
    EXPECT_EQ(repository().findLive(kOtherTeam).value(), std::nullopt);
    const auto missing = repository().setStatus(MatchId{999}, MatchStatus::Live);
    ASSERT_FALSE(missing.has_value());
    EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST_F(MatchRepositoryTest, OpponentGoalNeedsNoPlayer) {
    const auto id =
        repository().appendEvent(kMatch, event(30, MatchEventType::OpponentGoal, std::nullopt));
    ASSERT_TRUE(id.has_value()) << id.error().detail;
    EXPECT_EQ(repository().listEvents(kMatch)->at(0).playerId, std::nullopt);
}

TEST_F(MatchRepositoryTest, OtherEventsNeedAPlayer) {
    const auto id = repository().appendEvent(kMatch, event(30, MatchEventType::Goal, std::nullopt));
    ASSERT_FALSE(id.has_value());
    EXPECT_EQ(id.error().code, ErrorCode::InvalidInput);
}

TEST_F(MatchRepositoryTest, EventsOutOfRangeOrWithMissingParentsAreInvalidInput) {
    const auto badMinute = repository().appendEvent(kMatch, event(131));
    ASSERT_FALSE(badMinute.has_value());
    EXPECT_EQ(badMinute.error().code, ErrorCode::InvalidInput);

    const auto noMatch = repository().appendEvent(kNoMatch, event(10));
    ASSERT_FALSE(noMatch.has_value());
    EXPECT_EQ(noMatch.error().code, ErrorCode::InvalidInput);

    const auto noPlayer =
        repository().appendEvent(kMatch, event(10, MatchEventType::Goal, kNoPlayer));
    ASSERT_FALSE(noPlayer.has_value());
    EXPECT_EQ(noPlayer.error().code, ErrorCode::InvalidInput);
}

TEST_F(MatchRepositoryTest, EventsAreListedByMinuteThenInTheOrderAdded) {
    const MatchEventId late = repository().appendEvent(kMatch, event(80)).value();
    const MatchEventId early = repository().appendEvent(kMatch, event(10)).value();
    const MatchEventId sameMinute = repository().appendEvent(kMatch, event(10)).value();

    const auto events = repository().listEvents(kMatch);
    ASSERT_TRUE(events.has_value());
    ASSERT_EQ(events->size(), 3U);
    EXPECT_EQ(events->at(0).id, early);
    EXPECT_EQ(events->at(1).id, sameMinute);
    EXPECT_EQ(events->at(2).id, late);
}

TEST_F(MatchRepositoryTest, DeleteLastRemovesTheEventAddedLastAndReturnsIt) {
    const MatchEventId kept = repository().appendEvent(kMatch, event(80)).value();
    const MatchEventId lastAdded =
        repository().appendEvent(kMatch, event(10, MatchEventType::Yellow)).value();

    const auto deleted = repository().deleteLastEvent(kMatch);
    ASSERT_TRUE(deleted.has_value()) << deleted.error().detail;
    EXPECT_EQ(deleted->id, lastAdded);
    EXPECT_EQ(deleted->minute, 10);
    EXPECT_EQ(deleted->type, MatchEventType::Yellow);

    const auto events = repository().listEvents(kMatch);
    ASSERT_TRUE(events.has_value());
    ASSERT_EQ(events->size(), 1U);
    EXPECT_EQ(events->at(0).id, kept);
}

TEST_F(MatchRepositoryTest, DeleteLastTouchesOnlyThatMatch) {
    const MatchId other = create();
    ASSERT_TRUE(repository().appendEvent(kMatch, event(10)).has_value());
    ASSERT_TRUE(repository().appendEvent(other, event(20)).has_value());

    ASSERT_TRUE(repository().deleteLastEvent(kMatch).has_value());
    EXPECT_TRUE(repository().listEvents(kMatch)->empty());
    EXPECT_EQ(repository().listEvents(other)->size(), 1U);
}

TEST_F(MatchRepositoryTest, DeleteLastWithoutEventsIsNotFound) {
    const auto deleted = repository().deleteLastEvent(kMatch);
    ASSERT_FALSE(deleted.has_value());
    EXPECT_EQ(deleted.error().code, ErrorCode::NotFound);
}

// The returning statement must be finished, or the next write would fail on a held lock.
TEST_F(MatchRepositoryTest, WritesWorkAfterDeleteLast) {
    ASSERT_TRUE(repository().appendEvent(kMatch, event(10)).has_value());
    ASSERT_TRUE(repository().deleteLastEvent(kMatch).has_value());
    EXPECT_TRUE(repository().appendEvent(kMatch, event(20)).has_value());
    EXPECT_EQ(repository().listEvents(kMatch)->at(0).minute, 20);
}
