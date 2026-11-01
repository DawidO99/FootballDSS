#pragma once

// A migrated in-memory database for the repository tests. Parent rows are inserted with raw SQL, so
// a test does not depend on any repository other than the one it checks.

#include "TestDatabase.hpp"
#include "core/domain/Ids.hpp"
#include "data/db/DatabaseManager.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace fdss::tests {

class RepositoryTest : public ::testing::Test {
protected:
    static constexpr core::ClubId kClub{1};
    static constexpr core::TeamId kTeam{1};
    static constexpr core::TeamId kOtherTeam{2};
    static constexpr core::OpponentId kOpponent{1};
    static constexpr core::PlayerId kPlayer{1};
    static constexpr core::PlayerId kOtherPlayer{2};
    static constexpr core::MatchId kMatch{1};
    static constexpr core::TrainingSessionId kSession{1};
    static constexpr core::PlayerId kNoPlayer{999};
    static constexpr core::TeamId kNoTeam{999};

    void SetUp() override {
        auto opened = data::DatabaseManager::openInMemory();
        ASSERT_TRUE(opened.has_value()) << opened.error().detail;
        m_db = std::move(*opened);
    }

    [[nodiscard]] ::testing::AssertionResult sql(const char* statement,
                                                 const QVariantList& values = {}) const {
        return run(m_db->database(), QString::fromUtf8(statement), values);
    }

    [[nodiscard]] int count(const char* statement, const QVariantList& values = {}) const {
        return scalar(m_db->database(), QString::fromUtf8(statement), values).toInt();
    }

    // Club 1 with teams 1 and 2.
    void seedTeams() {
        ASSERT_TRUE(sql("INSERT INTO club (id, name) VALUES (1, 'KS Test')"));
        ASSERT_TRUE(sql("INSERT INTO team (id, club_id, name, category, season) "
                        "VALUES (1, 1, 'Seniorzy', 'senior', '2026/27')"));
        ASSERT_TRUE(sql("INSERT INTO team (id, club_id, name, category, season) "
                        "VALUES (2, 1, 'Juniorzy', 'u19', '2026/27')"));
    }

    // seedTeams() plus players 1 and 2 of team 1.
    void seedPlayers() {
        seedTeams();
        ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, shirt_number, "
                        "primary_position) VALUES (1, 1, 'Jan', 'Kowalski', 7, 'cm')"));
        ASSERT_TRUE(sql("INSERT INTO player (id, team_id, first_name, last_name, shirt_number, "
                        "primary_position) VALUES (2, 1, 'Adam', 'Nowak', 9, 'st')"));
    }

    // seedPlayers() plus opponent 1, match 1 and session 1 of team 1.
    void seedCalendar() {
        seedPlayers();
        ASSERT_TRUE(sql("INSERT INTO opponent (id, name, name_key) VALUES (1, 'Rywal', 'rywal')"));
        ASSERT_TRUE(sql("INSERT INTO match (id, team_id, opponent_id, date, venue) "
                        "VALUES (1, 1, 1, '2026-11-01', 'home')"));
        ASSERT_TRUE(sql("INSERT INTO training_session (id, team_id, date, duration_min, type) "
                        "VALUES (1, 1, '2026-11-02', 90, 'training')"));
    }

    std::unique_ptr<data::DatabaseManager> m_db;
};

} // namespace fdss::tests
