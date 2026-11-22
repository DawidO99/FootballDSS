#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Validation.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using namespace fdss::core;
using namespace fdss::core::demo;
using std::chrono::days;
using std::chrono::sys_days;

constexpr Date kStart{std::chrono::year{2026}, std::chrono::August, std::chrono::day{18}};

// Same seed and start date: this value on every platform. A deliberate change of the generator
// changes it; update it then, and only then.
// P11: the load-spike player trains lightly before the last week (RPE 3 on Tuesday and Thursday,
// excused on Friday, no match minutes) and hard in it (RPE 8/9/8, the whole last match at RPE 9),
// so the EWMA ratio of SPEC 7.1 is red on most weekdays (P11 decision 2). He no longer comes on as
// a forced substitute, so the substitutions of every match draw different numbers.
constexpr std::uint64_t kGoldenHash = 0x4EF3D50617412691ULL;

Date addDays(Date date, int count) { return Date{sys_days{date} + days{count}}; }

std::string text(Date date) {
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
}

template <class T> std::string text(const std::optional<T>& value) {
    if (!value) {
        return "-";
    }
    if constexpr (std::is_same_v<T, Date>) {
        return text(*value);
    } else if constexpr (std::is_same_v<T, TimeOfDay>) {
        return std::to_string(value->count());
    } else if constexpr (std::is_enum_v<T>) {
        return std::string(toDbText(*value));
    } else {
        return std::format("{}", *value);
    }
}

// Every field, in a fixed order, one row per line.
std::string canonical(const DemoDataset& data) {
    std::string out = "today " + text(data.today) + "\n";
    for (const DemoPlayer& player : data.players) {
        const PlayerDraft& draft = player.draft;
        out += std::format("P|{}|{}|{}|{}|{}|", draft.firstName, draft.lastName,
                           text(draft.shirtNumber), text(draft.birthYear),
                           toDbText(draft.primaryPosition.value()));
        for (const Position position : draft.secondaryPositions) {
            out += std::string(toDbText(position)) + ",";
        }
        out += std::format("|{}|{}|{}\n", toDbText(draft.foot), text(draft.notes),
                           text(player.archivedAt));
    }
    for (const OpponentDraft& opponent : data.opponents) {
        out += std::format("O|{}|{}\n", opponent.name, text(opponent.notes));
    }
    for (const DemoMatch& match : data.matches) {
        out += std::format("M|{}|{}|{}|{}|{}|{}|{}|{}\n", match.opponent, text(match.draft.date),
                           text(match.draft.kickoffTime), toDbText(match.draft.venue),
                           text(match.draft.competition), toDbText(match.status),
                           text(match.goalsFor), text(match.goalsAgainst));
        for (const DemoAppearance& a : match.appearances) {
            out += std::format("A|{}|{}|{}|{}|{}|{}|{}\n", a.player, a.started, a.minuteOn,
                               a.minuteOff, toDbText(a.position), text(a.rating), text(a.rpe));
        }
        for (const DemoEvent& e : match.events) {
            out += std::format("E|{}|{}|{}|{}\n", e.minute, toDbText(e.type), text(e.player),
                               text(e.relatedPlayer));
        }
    }
    for (const DemoSession& session : data.sessions) {
        const TrainingSessionDraft& draft = session.draft;
        out += std::format("S|{}|{}|{}|{}|{}\n", text(draft.date), text(draft.startTime),
                           draft.durationMin, toDbText(draft.type), text(draft.note));
        for (const DemoAttendance& a : session.attendance) {
            out += std::format("T|{}|{}|{}\n", a.player, toDbText(a.status), text(a.rpe));
        }
    }
    for (const DemoAvailability& entry : data.availability) {
        const AvailabilityDraft& draft = entry.draft;
        out += std::format("V|{}|{}|{}|{}|{}|{}\n", entry.player, toDbText(draft.type),
                           text(draft.fromDate), text(draft.expectedTo), text(draft.actualTo),
                           text(draft.description));
    }
    const DemoStory& story = data.story;
    out += std::format("Y|{}|{}|{}|{}|{}\n", story.loadSpike, story.returned, story.longInjury,
                       story.returningSoon, story.suspended);
    return out;
}

std::uint64_t fnv1a64(std::string_view bytes) {
    std::uint64_t hash = 0xCBF2'9CE4'8422'2325ULL;
    for (const char byte : bytes) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 0x0000'0100'0000'01B3ULL;
    }
    return hash;
}

Availability asAvailability(const DemoAvailability& entry) {
    Availability availability;
    static_cast<AvailabilityDraft&>(availability) = entry.draft;
    return availability;
}

bool isOut(const DemoDataset& data, int player, Date date, bool countSuspension) {
    return std::ranges::any_of(data.availability, [&](const DemoAvailability& entry) {
        return entry.player == player &&
               (countSuspension || entry.draft.type != AvailabilityType::Suspension) &&
               isActiveOn(asAvailability(entry), date);
    });
}

// sRPE: RPE x minutes of sessions and matches with from <= date <= to.
int load(const DemoDataset& data, int player, Date from, Date to) {
    int total = 0;
    for (const DemoSession& session : data.sessions) {
        if (session.draft.date < from || session.draft.date > to) {
            continue;
        }
        for (const DemoAttendance& a : session.attendance) {
            if (a.player == player && a.rpe) {
                total += *a.rpe * session.draft.durationMin;
            }
        }
    }
    for (const DemoMatch& match : data.matches) {
        if (match.draft.date < from || match.draft.date > to) {
            continue;
        }
        for (const DemoAppearance& a : match.appearances) {
            if (a.player == player && a.rpe) {
                total += *a.rpe * (a.minuteOff - a.minuteOn);
            }
        }
    }
    return total;
}

class DemoGeneratorTest : public ::testing::Test {
protected:
    DemoDataset data = generate(kDemoSeed, kStart);
};

TEST_F(DemoGeneratorTest, SameSeedGivesTheGoldenHash) {
    const std::string first = canonical(data);
    EXPECT_EQ(first, canonical(generate(kDemoSeed, kStart)));
    EXPECT_EQ(fnv1a64(first), kGoldenHash) << std::format("{:#018x}", fnv1a64(first));
}

TEST_F(DemoGeneratorTest, OtherSeedGivesOtherData) {
    EXPECT_NE(fnv1a64(canonical(data)), fnv1a64(canonical(generate(kDemoSeed + 1, kStart))));
}

TEST_F(DemoGeneratorTest, TodayIsTwelveWeeksAfterTheStart) {
    EXPECT_EQ(data.today, addDays(kStart, kDemoDays));
}

TEST_F(DemoGeneratorTest, SquadHas22ActiveAnd2ArchivedPlayers) {
    ASSERT_EQ(data.players.size(), 24U);
    int active = 0;
    int goalkeepers = 0;
    std::set<int> numbers;
    std::set<std::string> names;
    for (const DemoPlayer& player : data.players) {
        EXPECT_TRUE(validate(player.draft, 2026).empty()) << player.draft.lastName;
        ASSERT_TRUE(player.draft.shirtNumber.has_value());
        EXPECT_GE(*player.draft.shirtNumber, 1);
        EXPECT_LE(*player.draft.shirtNumber, 99);
        EXPECT_TRUE(numbers.insert(*player.draft.shirtNumber).second) << "duplicate number";
        EXPECT_TRUE(names.insert(player.draft.firstName + " " + player.draft.lastName).second);
        ASSERT_TRUE(player.draft.birthYear.has_value());
        EXPECT_GE(*player.draft.birthYear, 1988);
        EXPECT_LE(*player.draft.birthYear, 2007);
        if (!player.archivedAt) {
            ++active;
            goalkeepers += player.draft.primaryPosition == Position::Gk ? 1 : 0;
        } else {
            EXPECT_LT(*player.archivedAt, data.today);
        }
    }
    EXPECT_EQ(active, 22);
    EXPECT_EQ(goalkeepers, 2);
}

TEST_F(DemoGeneratorTest, StaffIsAnAssistantAndAPhysio) {
    ASSERT_EQ(kDemoStaff.size(), 2U);
    EXPECT_EQ(kDemoStaff[0].role, Role::Assistant);
    EXPECT_EQ(kDemoStaff[1].role, Role::Physio);
    EXPECT_EQ(kDemoStaffPin, "1234");
}

TEST_F(DemoGeneratorTest, EightOpponentsAndOneMatchEverySunday) {
    EXPECT_EQ(data.opponents.size(), 8U);
    std::set<std::string> names;
    for (const OpponentDraft& opponent : data.opponents) {
        EXPECT_TRUE(validate(opponent).empty());
        names.insert(opponent.name);
    }
    EXPECT_EQ(names.size(), 8U);

    int planned = 0;
    for (const DemoMatch& match : data.matches) {
        EXPECT_TRUE(validate(match.draft).empty());
        EXPECT_EQ(std::chrono::weekday{sys_days{match.draft.date}}, std::chrono::Sunday);
        EXPECT_GE(match.opponent, 0);
        EXPECT_LT(match.opponent, 8);
        if (match.status == MatchStatus::Planned) {
            ++planned;
            EXPECT_GE(match.draft.date, data.today);
            EXPECT_LT(match.draft.date, addDays(data.today, 7));
            EXPECT_FALSE(match.goalsFor.has_value());
            EXPECT_TRUE(match.appearances.empty());
            EXPECT_TRUE(match.events.empty());
        } else {
            EXPECT_EQ(match.status, MatchStatus::Finished);
            EXPECT_LT(match.draft.date, data.today);
        }
    }
    EXPECT_EQ(planned, 1);
    EXPECT_EQ(data.matches.size(), 13U); // 12 Sundays in 84 days + the next one
    EXPECT_EQ(data.matches.back().status, MatchStatus::Planned);
}

TEST_F(DemoGeneratorTest, AppearancesAreConsistentWithSubstitutionsAndCards) {
    for (const DemoMatch& match : data.matches) {
        if (match.status != MatchStatus::Finished) {
            continue;
        }
        const std::string when = text(match.draft.date);
        std::map<int, const DemoAppearance*> byPlayer;
        int starters = 0;
        int goalkeepersStarting = 0;
        for (const DemoAppearance& a : match.appearances) {
            EXPECT_TRUE(byPlayer.emplace(a.player, &a).second) << when << " duplicate player";
            EXPECT_GE(a.minuteOn, 0) << when;
            EXPECT_LE(a.minuteOn, a.minuteOff) << when;
            EXPECT_LE(a.minuteOff, kMatchMinutes) << when;
            if (a.started) {
                ++starters;
                EXPECT_EQ(a.minuteOn, 0) << when;
                goalkeepersStarting += a.position == Position::Gk ? 1 : 0;
            }
            EXPECT_FALSE(isOut(data, a.player, match.draft.date, true)) << when;
            const auto& archivedAt = data.players[static_cast<std::size_t>(a.player)].archivedAt;
            EXPECT_TRUE(!archivedAt || match.draft.date < *archivedAt) << when;
            AppearanceDraft draft{PlayerId{1}, a.started, a.minuteOn, a.minuteOff,
                                  a.position,  a.rating,  a.rpe};
            EXPECT_TRUE(validate(draft).empty()) << when;
        }
        EXPECT_EQ(starters, 11) << when;
        EXPECT_EQ(goalkeepersStarting, 1) << when;

        int subs = 0;
        int goals = 0;
        int against = 0;
        std::set<int> leftEarly;
        int previousMinute = 0;
        for (const DemoEvent& e : match.events) {
            EXPECT_GE(e.minute, previousMinute) << when << " events not by minute";
            previousMinute = e.minute;
            if (e.type == MatchEventType::Sub) {
                ++subs;
                ASSERT_TRUE(e.player && e.relatedPlayer) << when;
                const DemoAppearance* on = byPlayer.at(*e.player);
                const DemoAppearance* off = byPlayer.at(*e.relatedPlayer);
                EXPECT_FALSE(on->started) << when;
                EXPECT_EQ(on->minuteOn, e.minute) << when;
                EXPECT_EQ(on->minuteOff, kMatchMinutes) << when;
                EXPECT_EQ(off->minuteOff, e.minute) << when;
                leftEarly.insert(*e.relatedPlayer);
            } else if (e.type == MatchEventType::Red) {
                EXPECT_EQ(byPlayer.at(*e.player)->minuteOff, e.minute) << when;
                leftEarly.insert(*e.player);
            } else if (e.type == MatchEventType::Goal) {
                ++goals;
                const DemoAppearance* scorer = byPlayer.at(*e.player);
                EXPECT_GE(e.minute, scorer->minuteOn) << when;
                EXPECT_LE(e.minute, scorer->minuteOff) << when;
            } else if (e.type == MatchEventType::OpponentGoal) {
                ++against;
                EXPECT_FALSE(e.player.has_value()) << when;
            } else if (e.player) {
                EXPECT_TRUE(byPlayer.contains(*e.player)) << when;
            }
        }
        EXPECT_LE(subs, kMaxSubstitutions) << when;
        EXPECT_EQ(match.goalsFor, goals) << when;
        EXPECT_EQ(match.goalsAgainst, against) << when;
        // Nobody comes on without a substitution, nobody leaves early without one or a red card.
        for (const DemoAppearance& a : match.appearances) {
            if (a.minuteOff < kMatchMinutes) {
                EXPECT_TRUE(leftEarly.contains(a.player)) << when;
            }
        }
        EXPECT_EQ(static_cast<int>(match.appearances.size()), 11 + subs) << when;
    }
}

TEST_F(DemoGeneratorTest, ThreeSessionsAWeekWithPlausibleAttendance) {
    int present = 0;
    int marked = 0;
    int rpeInRange = 0;
    for (const DemoSession& session : data.sessions) {
        EXPECT_TRUE(validate(session.draft).empty());
        const Date date = session.draft.date;
        if (date >= data.today) {
            EXPECT_TRUE(session.attendance.empty()) << text(date);
            continue;
        }
        const auto weekDay = std::chrono::weekday{sys_days{date}};
        EXPECT_TRUE(weekDay == std::chrono::Tuesday || weekDay == std::chrono::Thursday ||
                    weekDay == std::chrono::Friday)
            << text(date);
        for (const DemoAttendance& a : session.attendance) {
            ++marked;
            AttendanceDraft draft{PlayerId{1}, a.status, a.rpe, std::nullopt};
            EXPECT_TRUE(validate(draft).empty());
            if (a.status == AttendanceStatus::Present) {
                ++present;
                ASSERT_TRUE(a.rpe.has_value());
                rpeInRange += (*a.rpe >= 4 && *a.rpe <= 8) ? 1 : 0;
            } else {
                EXPECT_FALSE(a.rpe.has_value()) << "RPE of a player who was not there";
            }
            const auto& archivedAt = data.players[static_cast<std::size_t>(a.player)].archivedAt;
            EXPECT_TRUE(!archivedAt || date < *archivedAt);
        }
    }
    EXPECT_EQ(marked > 0, true);
    const int presentPercent = present * 100 / marked;
    EXPECT_GE(presentPercent, 78);
    EXPECT_LE(presentPercent, 92);
    EXPECT_GE(rpeInRange * 100 / present, 70) << "RPE mostly 4-8";
}

TEST_F(DemoGeneratorTest, TodayAndTheNextWeekHavePlannedSessions) {
    std::vector<Date> planned;
    for (const DemoSession& session : data.sessions) {
        if (session.draft.date >= data.today) {
            planned.push_back(session.draft.date);
        }
    }
    ASSERT_EQ(planned.size(), 4U);
    EXPECT_EQ(planned.front(), data.today);
    for (std::size_t index = 1; index < planned.size(); ++index) {
        EXPECT_GT(planned[index], data.today);
        EXPECT_LE(planned[index], addDays(data.today, 7));
    }
}

TEST_F(DemoGeneratorTest, TodayGetsASessionOnAnyWeekday) {
    for (int shift = 0; shift < 7; ++shift) {
        const DemoDataset shifted = generate(kDemoSeed, addDays(kStart, shift));
        const auto today = std::ranges::count_if(
            shifted.sessions, [&](const DemoSession& s) { return s.draft.date == shifted.today; });
        EXPECT_EQ(today, 1) << shift;
        const auto planned = std::ranges::count_if(
            shifted.matches, [](const DemoMatch& m) { return m.status == MatchStatus::Planned; });
        EXPECT_EQ(planned, 1) << shift;
    }
}

TEST_F(DemoGeneratorTest, UnavailablePlayersAreNeverPresent) {
    for (const DemoSession& session : data.sessions) {
        for (const DemoAttendance& a : session.attendance) {
            if (isOut(data, a.player, session.draft.date, false)) {
                EXPECT_NE(a.status, AttendanceStatus::Present) << text(session.draft.date);
            }
        }
    }
}

TEST_F(DemoGeneratorTest, AvailabilityTellsTheStoriesOfToday) {
    const DemoStory& story = data.story;
    std::map<AvailabilityType, int> types;
    for (const DemoAvailability& entry : data.availability) {
        EXPECT_TRUE(validate(entry.draft).empty());
        ++types[entry.draft.type];
    }
    EXPECT_EQ(types[AvailabilityType::Injury], 3);
    EXPECT_EQ(types[AvailabilityType::Illness], 1);
    EXPECT_EQ(types[AvailabilityType::Suspension], 1);

    const Date nextWeek = addDays(data.today, 7);
    EXPECT_TRUE(isOut(data, story.longInjury, data.today, true));
    EXPECT_TRUE(isOut(data, story.returningSoon, data.today, true));
    EXPECT_FALSE(isOut(data, story.returned, data.today, true));
    for (const DemoAvailability& entry : data.availability) {
        if (entry.player == story.longInjury) {
            EXPECT_GT(*entry.draft.expectedTo, nextWeek);
        }
        if (entry.player == story.returningSoon) {
            EXPECT_GT(*entry.draft.expectedTo, data.today);
            EXPECT_LE(*entry.draft.expectedTo, nextWeek);
        }
    }

    // The red card of the last match keeps the player out of the planned one.
    const DemoMatch& next = data.matches.back();
    const DemoMatch& last = data.matches[data.matches.size() - 2];
    EXPECT_TRUE(std::ranges::any_of(last.events, [&](const DemoEvent& e) {
        return e.type == MatchEventType::Red && e.player == story.suspended;
    }));
    EXPECT_TRUE(isOut(data, story.suspended, next.draft.date, true));
    EXPECT_FALSE(isOut(data, story.suspended, last.draft.date, true));
}

TEST_F(DemoGeneratorTest, OnePlayerHasALoadSpikeInTheLastWeek) {
    const int player = data.story.loadSpike;
    const Date yesterday = addDays(data.today, -1);
    const int acute = load(data, player, addDays(data.today, -7), yesterday);
    const int chronicWeekly = load(data, player, addDays(data.today, -28), yesterday) / 4;
    ASSERT_GT(chronicWeekly, 0);
    // At least twice the mean week of the last 28 days (P11: the EWMA ratio needs that contrast).
    const int ratioPercent = acute * 100 / chronicWeekly;
    EXPECT_GE(ratioPercent, 200) << acute << " vs " << chronicWeekly;

    // High RPE on every session of the last week; before it light or excused.
    for (const DemoSession& session : data.sessions) {
        if (session.draft.date >= data.today || session.attendance.empty()) {
            continue;
        }
        const auto it = std::ranges::find(session.attendance, player, &DemoAttendance::player);
        ASSERT_NE(it, session.attendance.end());
        if (session.draft.date >= addDays(data.today, -7)) {
            EXPECT_EQ(it->status, AttendanceStatus::Present);
            EXPECT_GE(it->rpe.value_or(0), 8);
        } else {
            EXPECT_LE(it->rpe.value_or(0), 3);
        }
    }
    // No minutes before the last match.
    for (std::size_t index = 0; index + 2 < data.matches.size(); ++index) {
        EXPECT_EQ(
            std::ranges::find(data.matches[index].appearances, player, &DemoAppearance::player),
            data.matches[index].appearances.end());
    }
    const DemoMatch& last = data.matches[data.matches.size() - 2];
    const auto it = std::ranges::find(last.appearances, player, &DemoAppearance::player);
    ASSERT_NE(it, last.appearances.end());
    EXPECT_TRUE(it->started);
    EXPECT_EQ(it->minuteOff - it->minuteOn, kMatchMinutes);
}

TEST_F(DemoGeneratorTest, OnePlayerHadLowLoadBeforeReturningFromInjury) {
    const int player = data.story.returned;
    const auto entry = std::ranges::find_if(data.availability, [&](const DemoAvailability& e) {
        return e.player == player && e.draft.type == AvailabilityType::Injury;
    });
    ASSERT_NE(entry, data.availability.end());
    ASSERT_TRUE(entry->draft.actualTo.has_value());
    const Date back = *entry->draft.actualTo;
    const Date from = entry->draft.fromDate;
    EXPECT_LT(back, data.today);

    // Loaded before the injury, nothing in the three weeks before the return.
    EXPECT_GT(load(data, player, addDays(from, -21), addDays(from, -1)), 0);
    EXPECT_EQ(load(data, player, addDays(back, -21), addDays(back, -1)), 0);

    // And the first week back stays below the team median.
    std::vector<int> others;
    for (int other = 0; other < kActivePlayers; ++other) {
        others.push_back(load(data, other, back, addDays(back, 6)));
    }
    std::ranges::sort(others);
    EXPECT_LT(load(data, player, back, addDays(back, 6)), others[others.size() / 2]);
}

TEST(SplitMix64Test, KnownSequenceAndRanges) {
    // Reference values of SplitMix64 with seed 0.
    SplitMix64 rng(0);
    EXPECT_EQ(rng.next(), 0xE220'A839'7B1D'CDAFULL);
    EXPECT_EQ(rng.next(), 0x6E78'9E6A'A1B9'65F4ULL);

    SplitMix64 bounded(42);
    for (int draw = 0; draw < 1000; ++draw) {
        const int value = bounded.uniform(-3, 5);
        EXPECT_GE(value, -3);
        EXPECT_LE(value, 5);
        const int bell = bounded.bell(4, 8);
        EXPECT_GE(bell, 4);
        EXPECT_LE(bell, 8);
    }
}

} // namespace
