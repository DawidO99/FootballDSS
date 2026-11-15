#include "core/demo/DemoGenerator.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>
#include <utility>

namespace fdss::core::demo {

std::uint64_t SplitMix64::next() noexcept {
    m_state += 0x9E37'79B9'7F4A'7C15ULL;
    std::uint64_t z = m_state;
    z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return z ^ (z >> 31U);
}

int SplitMix64::uniform(int lo, int hi) noexcept {
    const auto span =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo)) +
        1U;
    // Values below 2^64 mod span would make the low results more likely.
    const std::uint64_t threshold = (std::uint64_t{0} - span) % span;
    std::uint64_t value = next();
    while (value < threshold) {
        value = next();
    }
    return static_cast<int>(static_cast<std::int64_t>(lo) +
                            static_cast<std::int64_t>(value % span));
}

bool SplitMix64::chance(int percent) noexcept { return uniform(1, 100) <= percent; }

int SplitMix64::bell(int lo, int hi, int rolls) noexcept {
    int sum = 0;
    for (int roll = 0; roll < rolls; ++roll) {
        sum += uniform(0, hi - lo);
    }
    return lo + (sum + rolls / 2) / rolls;
}

namespace {

using std::chrono::days;
using std::chrono::sys_days;
using std::chrono::weekday;

constexpr int kTotalPlayers = kActivePlayers + kArchivedPlayers;
constexpr int kStarters = 11;
constexpr int kNoPlayer = -1;

constexpr std::array<std::string_view, 30> kFirstNames{
    "Adam",       "Bartosz", "Damian",    "Dawid",   "Dominik", "Filip",    "Grzegorz", "Hubert",
    "Jakub",      "Jan",     "Kacper",    "Kamil",   "Karol",   "Krystian", "Łukasz",   "Maciej",
    "Marcin",     "Mateusz", "Michał",    "Mikołaj", "Oskar",   "Patryk",   "Paweł",    "Piotr",
    "Przemysław", "Rafał",   "Sebastian", "Szymon",  "Wiktor",  "Wojciech"};

constexpr std::array<std::string_view, 40> kLastNames{
    "Bielak",   "Chmielarz",  "Czajka",    "Dudziak",  "Florczak",  "Gajda",    "Gołębiowski",
    "Grzelak",  "Jarosz",     "Kaczmarek", "Kępa",     "Kmiecik",   "Kowalik",  "Krawczyk",
    "Kubacki",  "Laskowski",  "Leśniak",   "Machaj",   "Marciniak", "Mazurek",  "Michalak",
    "Nowicki",  "Olszewski",  "Pawlak",    "Pietrzak", "Przybyła",  "Radomski", "Rogala",
    "Sadowski", "Sikora",     "Skiba",     "Sobczak",  "Stępień",   "Szewczyk", "Tomczak",
    "Urban",    "Wawrzyniak", "Wilczek",   "Zając",    "Żurek"};

constexpr std::array<std::string_view, 10> kOpponentNames{
    "LKS Orzeł Wólka Dolna",   "KS Sokół Lipnica Mała",
    "GKS Grom Brzozówka",      "LZS Płomień Zarzecze Górne",
    "KS Unia Kamionna Wola",   "LKS Iskra Olszanka",
    "Victoria Dębowiec Stary", "Błękitni Bukowina Nowa",
    "Huragan Kłodzianka",      "Zryw Topolin"};

// The active players first (2 goalkeepers among them), then the two archived ones.
constexpr std::array<Position, kTotalPlayers> kSquadPositions{
    Position::Gk, Position::Gk, Position::Rb, Position::Rb, Position::Cb, Position::Cb,
    Position::Cb, Position::Cb, Position::Lb, Position::Lb, Position::Dm, Position::Dm,
    Position::Cm, Position::Cm, Position::Cm, Position::Am, Position::Rm, Position::Lm,
    Position::Rw, Position::Lw, Position::St, Position::St, Position::Cm, Position::St};

// 4-4-2.
constexpr std::array<Position, kStarters> kFormation{
    Position::Gk, Position::Rb, Position::Cb, Position::Cb, Position::Lb, Position::Rm,
    Position::Cm, Position::Cm, Position::Lm, Position::St, Position::St};

constexpr std::array<int, 9> kGoalsForTable{0, 1, 1, 2, 2, 2, 3, 3, 4};
constexpr std::array<int, 7> kGoalsAgainstTable{0, 1, 1, 1, 2, 2, 3};
constexpr std::array<int, 5> kKickoffHours{15, 15, 16, 14, 11};

// The template of a weekly session: Tue after the match, Thu the hardest, Fri before the match.
struct SessionTemplate {
    SessionType type = SessionType::Training;
    int durationMin = 75;
    int startMinutes = 18 * 60;
    int rpeLo = 3;
    int rpeHi = 6;
    // The load-spike player before the last week (nullopt: excused) and in the last week. The
    // contrast is large on purpose: the EWMA ratio follows the weekly microcycle, and a smaller
    // spike is red on some weekdays only (P11 decision 2).
    std::optional<int> spikeRpeBefore = 3;
    int spikeRpeLast = 8;
};

// Before the last week the load-spike player trains lightly on Tuesday and Thursday and is
// excused on Friday (work), and does not play.
constexpr SessionTemplate kTuesday{SessionType::Training, 75, 18 * 60, 3, 6, 3, 8};
constexpr SessionTemplate kThursday{SessionType::Training, 90, 18 * 60, 5, 8, 3, 9};
constexpr SessionTemplate kFriday{SessionType::MatchPrep, 60, (17 * 60) + 30, 4, 7,
                                  std::nullopt,           8};

constexpr int kSpikeFullRpe = 9;

Date addDays(Date date, int count) { return Date{sys_days{date} + days{count}}; }

const SessionTemplate* weeklyTemplate(weekday day) {
    if (day == std::chrono::Tuesday) {
        return &kTuesday;
    }
    if (day == std::chrono::Thursday) {
        return &kThursday;
    }
    if (day == std::chrono::Friday) {
        return &kFriday;
    }
    return nullptr;
}

std::optional<Position> secondaryOf(Position position) {
    switch (position) {
    case Position::Rb:
        return Position::Rwb;
    case Position::Cb:
        return Position::Dm;
    case Position::Lb:
        return Position::Lwb;
    case Position::Dm:
        return Position::Cm;
    case Position::Cm:
        return Position::Am;
    case Position::Am:
        return Position::Cm;
    case Position::Rm:
        return Position::Rw;
    case Position::Lm:
        return Position::Lw;
    case Position::Rw:
        return Position::Rm;
    case Position::Lw:
        return Position::Lm;
    case Position::St:
        return Position::Am;
    default:
        return std::nullopt;
    }
}

bool slotAccepts(Position slot, Position position) {
    if (slot == position) {
        return true;
    }
    switch (slot) {
    case Position::Rb:
        return position == Position::Rwb;
    case Position::Lb:
        return position == Position::Lwb;
    case Position::Rm:
        return position == Position::Rw;
    case Position::Lm:
        return position == Position::Lw;
    case Position::Cm:
        return position == Position::Dm || position == Position::Am;
    default:
        return false;
    }
}

// How likely a player in this position scores.
int scoringWeight(Position position) {
    switch (positionGroup(position)) {
    case PositionGroup::Forward:
        return 6;
    case PositionGroup::Midfielder:
        return position == Position::Dm ? 1 : 3;
    case PositionGroup::Defender:
        return 1;
    case PositionGroup::Goalkeeper:
        return 0;
    }
    return 0;
}

class Generator {
public:
    Generator(std::uint64_t seed, Date startDate)
        : m_rng(seed), m_today(addDays(startDate, kDemoDays)) {}

    DemoDataset run() {
        m_data.today = m_today;
        makePlayers();
        makeOpponents();
        pickStory();
        makeAvailability();
        makeCalendar();
        return std::move(m_data);
    }

private:
    Date day(int offsetFromToday) const { return addDays(m_today, offsetFromToday); }

    bool inSquad(int player, Date date) const {
        const auto& archivedAt = m_data.players[static_cast<std::size_t>(player)].archivedAt;
        return !archivedAt || date < *archivedAt;
    }

    // The availability type that keeps the player out on that date, the most serious one first.
    std::optional<AvailabilityType> outOn(int player, Date date) const {
        std::optional<AvailabilityType> result;
        for (const DemoAvailability& entry : m_data.availability) {
            if (entry.player != player) {
                continue;
            }
            Availability availability;
            static_cast<AvailabilityDraft&>(availability) = entry.draft;
            if (isActiveOn(availability, date) && (!result || entry.draft.type < *result)) {
                result = entry.draft.type;
            }
        }
        return result;
    }

    const PlayerDraft& draftOf(int player) const {
        return m_data.players[static_cast<std::size_t>(player)].draft;
    }

    bool isGoalkeeper(int player) const { return draftOf(player).primaryPosition == Position::Gk; }

    void makePlayers() {
        std::array<std::string_view, kLastNames.size()> lastNames = kLastNames;
        std::array<std::string_view, kFirstNames.size()> firstNames = kFirstNames;
        shuffle(firstNames);
        shuffle(lastNames);
        // Goalkeepers wear 1 and 12; the others take numbers from 2 to 35.
        std::vector<int> numbers;
        for (int number = 2; number <= 35; ++number) {
            if (number != 12) {
                numbers.push_back(number);
            }
        }
        shuffle(numbers);
        std::size_t nextNumber = 0;
        int goalkeepers = 0;

        for (int index = 0; index < kTotalPlayers; ++index) {
            DemoPlayer player;
            PlayerDraft& draft = player.draft;
            const Position position = kSquadPositions[static_cast<std::size_t>(index)];
            draft.firstName = std::string(firstNames[static_cast<std::size_t>(index)]);
            draft.lastName = std::string(lastNames[static_cast<std::size_t>(index)]);
            draft.primaryPosition = position;
            if (position == Position::Gk) {
                draft.shirtNumber = goalkeepers++ == 0 ? 1 : 12;
            } else {
                draft.shirtNumber = numbers[nextNumber++];
            }
            draft.birthYear = m_rng.uniform(1988, 2007);
            if (const auto secondary = secondaryOf(position); secondary && m_rng.chance(50)) {
                draft.secondaryPositions.push_back(*secondary);
            }
            draft.foot = footFor(position);
            if (index >= kActivePlayers) {
                player.archivedAt = day(index == kActivePlayers ? -50 : -43);
            }
            m_data.players.push_back(std::move(player));
            // The first goalkeeper is the regular one.
            m_quality.push_back(index == 0 ? 85 : index == 1 ? 35 : m_rng.uniform(40, 90));
        }
    }

    PreferredFoot footFor(Position position) {
        const int roll = m_rng.uniform(1, 100);
        const bool leftSide =
            position == Position::Lb || position == Position::Lm || position == Position::Lw;
        if (leftSide) {
            return roll <= 80 ? PreferredFoot::Left : PreferredFoot::Both;
        }
        if (roll <= 82) {
            return PreferredFoot::Right;
        }
        return roll <= 93 ? PreferredFoot::Left : PreferredFoot::Both;
    }

    void makeOpponents() {
        std::array<std::string_view, kOpponentNames.size()> names = kOpponentNames;
        shuffle(names);
        for (int index = 0; index < kOpponents; ++index) {
            m_data.opponents.push_back(
                OpponentDraft{std::string(names[static_cast<std::size_t>(index)]), std::nullopt});
        }
        m_homeFirst = m_rng.uniform(0, 1);
    }

    void pickStory() {
        DemoStory& story = m_data.story;
        // Of the three central midfielders, the best one is injured for weeks and the weakest one
        // takes his place: the load spike.
        std::array<int, 3> centralMidfielders{12, 13, 14};
        std::ranges::stable_sort(centralMidfielders, [this](int a, int b) {
            return m_quality[static_cast<std::size_t>(a)] > m_quality[static_cast<std::size_t>(b)];
        });
        story.longInjury = centralMidfielders[0];
        story.loadSpike = centralMidfielders[2];
        // A defender, and a winger or striker.
        story.returningSoon = m_rng.uniform(2, 9);
        story.returned = m_rng.uniform(16, 21);
        // The suspended player is the one sent off in the last match.
        story.suspended = kNoPlayer;
        do {
            m_illPlayer = m_rng.uniform(2, kActivePlayers - 1);
        } while (m_illPlayer == story.longInjury || m_illPlayer == story.loadSpike ||
                 m_illPlayer == story.returningSoon || m_illPlayer == story.returned);
    }

    void addAvailability(int player, AvailabilityType type, int from, std::optional<int> expected,
                         std::optional<int> actual, std::string_view description) {
        DemoAvailability entry;
        entry.player = player;
        entry.draft.type = type;
        entry.draft.fromDate = day(from);
        if (expected) {
            entry.draft.expectedTo = day(*expected);
        }
        if (actual) {
            entry.draft.actualTo = day(*actual);
        }
        entry.draft.description = std::string(description);
        m_data.availability.push_back(std::move(entry));
    }

    void makeAvailability() {
        const DemoStory& story = m_data.story;
        addAvailability(story.returned, AvailabilityType::Injury, -45, -17, -14,
                        "Skręcenie stawu skokowego");
        addAvailability(m_illPlayer, AvailabilityType::Illness, -62, -58, -57, "Grypa");
        addAvailability(story.longInjury, AvailabilityType::Injury, -20, 24, std::nullopt,
                        "Naderwanie mięśnia dwugłowego uda");
        addAvailability(story.returningSoon, AvailabilityType::Injury, -10, m_rng.uniform(2, 6),
                        std::nullopt, "Stłuczenie kolana");
    }

    void makeCalendar() {
        const auto sinceSunday =
            static_cast<int>((weekday{sys_days{m_today}} - std::chrono::Sunday).count());
        // The last match played before today and the next one on or after it.
        const int lastPlayedOffset = sinceSunday == 0 ? -7 : -sinceSunday;
        const int plannedOffset = (7 - sinceSunday) % 7;
        int matchNumber = 0;

        for (int offset = -kDemoDays; offset <= 7; ++offset) {
            const Date date = day(offset);
            const weekday weekDay{sys_days{date}};
            const SessionTemplate* weekly = weeklyTemplate(weekDay);

            if (weekDay == std::chrono::Sunday && (offset < 0 || offset == plannedOffset)) {
                if (offset < 0) {
                    playMatch(date, matchNumber++, offset == lastPlayedOffset);
                } else {
                    planMatch(date, matchNumber++);
                }
            }
            if (offset < 0 && weekly != nullptr) {
                pastSession(date, offset, *weekly);
            } else if (offset == 0) {
                plannedSession(date, weekly != nullptr ? *weekly : otherDayTemplate(weekDay));
            } else if (offset > 0 && weekly != nullptr) {
                plannedSession(date, *weekly);
            }
        }

        // The red card of the last match: out for the next one.
        if (m_data.story.suspended != kNoPlayer) {
            addAvailability(m_data.story.suspended, AvailabilityType::Suspension,
                            lastPlayedOffset + 1, plannedOffset, std::nullopt, "Czerwona kartka");
        }
    }

    static SessionTemplate otherDayTemplate(weekday weekDay) {
        if (weekDay == std::chrono::Saturday) {
            return SessionTemplate{SessionType::MatchPrep, 45, 10 * 60, 0, 0, 0, 0};
        }
        if (weekDay == std::chrono::Sunday) {
            return SessionTemplate{SessionType::Recovery, 45, 10 * 60, 0, 0, 0, 0};
        }
        return SessionTemplate{SessionType::Recovery, 45, 18 * 60, 0, 0, 0, 0};
    }

    static TrainingSessionDraft sessionDraft(Date date, const SessionTemplate& weekly) {
        TrainingSessionDraft draft;
        draft.date = date;
        draft.startTime = TimeOfDay{weekly.startMinutes};
        draft.durationMin = weekly.durationMin;
        draft.type = weekly.type;
        return draft;
    }

    void plannedSession(Date date, const SessionTemplate& weekly) {
        m_data.sessions.push_back(DemoSession{sessionDraft(date, weekly), {}});
    }

    void pastSession(Date date, int offset, const SessionTemplate& weekly) {
        const DemoStory& story = m_data.story;
        DemoSession session{sessionDraft(date, weekly), {}};
        for (int player = 0; player < kTotalPlayers; ++player) {
            if (!inSquad(player, date)) {
                continue;
            }
            DemoAttendance attendance{player, AttendanceStatus::Present, std::nullopt};
            const auto out = outOn(player, date);
            if (player == story.loadSpike) {
                attendance.rpe = offset >= -7 ? weekly.spikeRpeLast : weekly.spikeRpeBefore;
                if (!attendance.rpe) {
                    attendance.status = AttendanceStatus::Excused;
                }
            } else if (out == AvailabilityType::Injury) {
                attendance.status = AttendanceStatus::Injured;
            } else if (out == AvailabilityType::Illness) {
                attendance.status = AttendanceStatus::Excused;
            } else if (player == story.returned && offset >= -14 && offset < -7) {
                // The first week back: there every time, but kept light.
                attendance.rpe = m_rng.uniform(2, 4);
            } else {
                const int roll = m_rng.uniform(1, 100);
                if (roll <= 86) {
                    attendance.rpe = m_rng.bell(weekly.rpeLo, weekly.rpeHi);
                } else {
                    attendance.status =
                        roll <= 95 ? AttendanceStatus::Excused : AttendanceStatus::Absent;
                }
            }
            session.attendance.push_back(attendance);
        }
        m_data.sessions.push_back(std::move(session));
    }

    MatchDraft matchDraft(Date date, int matchNumber) {
        MatchDraft draft;
        draft.date = date;
        const int hour = kKickoffHours[static_cast<std::size_t>(
            m_rng.uniform(0, static_cast<int>(kKickoffHours.size()) - 1))];
        draft.kickoffTime = TimeOfDay{hour * 60};
        // The second round reverses the venue of the first one.
        const bool home = ((matchNumber % kOpponents) + m_homeFirst) % 2 == 0;
        const bool secondRound = (matchNumber / kOpponents) % 2 == 1;
        draft.venue = home != secondRound ? Venue::Home : Venue::Away;
        draft.competition = std::string(kDemoCompetition);
        return draft;
    }

    void planMatch(Date date, int matchNumber) {
        DemoMatch match;
        match.opponent = matchNumber % kOpponents;
        match.draft = matchDraft(date, matchNumber);
        match.status = MatchStatus::Planned;
        m_data.matches.push_back(std::move(match));
    }

    static bool onPitch(const DemoAppearance& appearance, int minute) {
        return appearance.minuteOn <= minute &&
               (minute < appearance.minuteOff || appearance.minuteOff == kMatchMinutes);
    }

    // The best fit by quality with some luck; ties go to the lower index.
    int pickBest(const std::vector<int>& candidates) {
        int best = kNoPlayer;
        int bestScore = -1;
        for (const int candidate : candidates) {
            const int score = m_quality[static_cast<std::size_t>(candidate)] + m_rng.uniform(0, 30);
            if (score > bestScore) {
                best = candidate;
                bestScore = score;
            }
        }
        return best;
    }

    int pickAny(const std::vector<int>& candidates) {
        if (candidates.empty()) {
            return kNoPlayer;
        }
        return candidates[static_cast<std::size_t>(
            m_rng.uniform(0, static_cast<int>(candidates.size()) - 1))];
    }

    void playMatch(Date date, int matchNumber, bool lastPlayed) {
        const DemoStory& story = m_data.story;
        DemoMatch match;
        match.opponent = matchNumber % kOpponents;
        match.draft = matchDraft(date, matchNumber);
        match.status = MatchStatus::Finished;

        std::vector<int> available;
        for (int player = 0; player < kTotalPlayers; ++player) {
            // The first match after the return from injury is watched from the stands.
            const bool easingBack = player == story.returned && date >= day(-14) && date < day(-7);
            if (inSquad(player, date) && !outOn(player, date) && !easingBack) {
                available.push_back(player);
            }
        }
        std::vector<bool> picked(kTotalPlayers, false);
        auto& appearances = match.appearances;

        // The load-spike player does not play until the last match, which he plays in full.
        if (!lastPlayed) {
            picked[static_cast<std::size_t>(story.loadSpike)] = true;
        }
        bool spikePlaced = !lastPlayed;
        for (const Position slot : kFormation) {
            int chosen = kNoPlayer;
            if (slot == Position::Cm && !spikePlaced) {
                chosen = story.loadSpike;
                spikePlaced = true;
            } else {
                std::vector<int> fitting;
                std::vector<int> outfield;
                for (const int player : available) {
                    if (picked[static_cast<std::size_t>(player)] ||
                        (slot == Position::Gk) != isGoalkeeper(player)) {
                        continue;
                    }
                    outfield.push_back(player);
                    const PlayerDraft& draft = draftOf(player);
                    if (slotAccepts(slot, draft.primaryPosition) ||
                        std::ranges::any_of(draft.secondaryPositions, [slot](Position position) {
                            return slotAccepts(slot, position);
                        })) {
                        fitting.push_back(player);
                    }
                }
                chosen = pickBest(fitting.empty() ? outfield : fitting);
            }
            picked[static_cast<std::size_t>(chosen)] = true;
            appearances.push_back(DemoAppearance{chosen, true, 0, kMatchMinutes, slot, {}, {}});
        }

        // A red card in the last match, for the suspension.
        int sentOff = kNoPlayer;
        int redMinute = 0;
        if (lastPlayed) {
            std::vector<int> candidates;
            for (const DemoAppearance& appearance : appearances) {
                if (!isGoalkeeper(appearance.player) && appearance.player != story.loadSpike &&
                    appearance.player != story.returned) {
                    candidates.push_back(appearance.player);
                }
            }
            sentOff = pickAny(candidates);
            redMinute = m_rng.uniform(55, 80);
            for (DemoAppearance& appearance : appearances) {
                if (appearance.player == sentOff) {
                    appearance.minuteOff = redMinute;
                }
            }
            m_data.story.suspended = sentOff;
            match.events.push_back(DemoEvent{redMinute, MatchEventType::Red, sentOff, {}});
        }

        substitutions(match, available, picked, lastPlayed, sentOff);
        goalsAndCards(match, sentOff);
        ratings(match);
        std::ranges::stable_sort(match.events, {}, &DemoEvent::minute);
        m_data.matches.push_back(std::move(match));
    }

    void substitutions(DemoMatch& match, const std::vector<int>& available,
                       std::vector<bool>& picked, bool lastPlayed, int sentOff) {
        const DemoStory& story = m_data.story;
        std::vector<int> bench;
        for (const int player : available) {
            if (!picked[static_cast<std::size_t>(player)] && !isGoalkeeper(player)) {
                bench.push_back(player);
            }
        }
        const int count =
            std::min(m_rng.uniform(2, kMaxSubstitutions), static_cast<int>(bench.size()));

        std::vector<int> minutes;
        while (static_cast<int>(minutes.size()) < count) {
            minutes.push_back(m_rng.uniform(46, 88));
        }
        std::ranges::stable_sort(minutes);

        auto& appearances = match.appearances;
        for (const int minute : minutes) {
            std::vector<int> outCandidates;
            for (std::size_t index = 0; index < static_cast<std::size_t>(kStarters); ++index) {
                const DemoAppearance& starter = appearances[index];
                if (!isGoalkeeper(starter.player) && starter.player != sentOff &&
                    starter.minuteOff == kMatchMinutes &&
                    !(lastPlayed && starter.player == story.loadSpike)) {
                    outCandidates.push_back(static_cast<int>(index));
                }
            }
            if (outCandidates.empty()) {
                break;
            }
            const int playerOn = pickBest(bench);
            if (playerOn == kNoPlayer) {
                break;
            }
            std::erase(bench, playerOn);
            const int outIndex = pickAny(outCandidates);
            DemoAppearance& off = appearances[static_cast<std::size_t>(outIndex)];
            off.minuteOff = minute;
            picked[static_cast<std::size_t>(playerOn)] = true;
            match.events.push_back(DemoEvent{minute, MatchEventType::Sub, playerOn, off.player});
            const Position position = off.position;
            appearances.push_back(
                DemoAppearance{playerOn, false, minute, kMatchMinutes, position, {}, {}});
        }
    }

    void goalsAndCards(DemoMatch& match, int sentOff) {
        const auto& appearances = match.appearances;
        const int goalsFor = kGoalsForTable[static_cast<std::size_t>(
            m_rng.uniform(0, static_cast<int>(kGoalsForTable.size()) - 1))];
        const int goalsAgainst = kGoalsAgainstTable[static_cast<std::size_t>(
            m_rng.uniform(0, static_cast<int>(kGoalsAgainstTable.size()) - 1))];

        for (int goal = 0; goal < goalsFor; ++goal) {
            const int minute = m_rng.uniform(1, kMatchMinutes);
            int total = 0;
            for (const DemoAppearance& appearance : appearances) {
                if (onPitch(appearance, minute)) {
                    total += scoringWeight(appearance.position);
                }
            }
            int roll = m_rng.uniform(1, total);
            int scorer = kNoPlayer;
            for (const DemoAppearance& appearance : appearances) {
                if (!onPitch(appearance, minute)) {
                    continue;
                }
                roll -= scoringWeight(appearance.position);
                if (roll <= 0) {
                    scorer = appearance.player;
                    break;
                }
            }
            std::optional<int> assist;
            if (m_rng.chance(65)) {
                std::vector<int> candidates;
                for (const DemoAppearance& appearance : appearances) {
                    if (onPitch(appearance, minute) && appearance.player != scorer &&
                        appearance.position != Position::Gk) {
                        candidates.push_back(appearance.player);
                    }
                }
                if (const int player = pickAny(candidates); player != kNoPlayer) {
                    assist = player;
                }
            }
            match.events.push_back(DemoEvent{minute, MatchEventType::Goal, scorer, assist});
        }
        for (int goal = 0; goal < goalsAgainst; ++goal) {
            match.events.push_back(DemoEvent{m_rng.uniform(1, kMatchMinutes),
                                             MatchEventType::OpponentGoal, std::nullopt,
                                             std::nullopt});
        }

        const int yellows = m_rng.uniform(0, 3);
        std::vector<int> booked;
        for (int card = 0; card < yellows; ++card) {
            const int minute = m_rng.uniform(10, kMatchMinutes);
            std::vector<int> candidates;
            for (const DemoAppearance& appearance : appearances) {
                if (onPitch(appearance, minute) && appearance.player != sentOff &&
                    std::ranges::find(booked, appearance.player) == booked.end()) {
                    candidates.push_back(appearance.player);
                }
            }
            if (const int player = pickAny(candidates); player != kNoPlayer) {
                booked.push_back(player);
                match.events.push_back(DemoEvent{minute, MatchEventType::Yellow, player, {}});
            }
        }
        match.goalsFor = goalsFor;
        match.goalsAgainst = goalsAgainst;
    }

    void ratings(DemoMatch& match) {
        const DemoStory& story = m_data.story;
        for (DemoAppearance& appearance : match.appearances) {
            int rating = m_rng.bell(5, 7);
            for (const DemoEvent& event : match.events) {
                if (event.type == MatchEventType::Goal && event.player == appearance.player) {
                    ++rating;
                }
                if (event.type == MatchEventType::Red && event.player == appearance.player) {
                    rating = 3;
                }
            }
            appearance.rating = std::min(rating, 10);

            const int minutes = appearance.minuteOff - appearance.minuteOn;
            if (appearance.player == story.loadSpike) {
                appearance.rpe = kSpikeFullRpe; // he plays the last match only
            } else if (minutes >= 60) {
                appearance.rpe = m_rng.bell(6, 9);
            } else if (minutes >= 30) {
                appearance.rpe = m_rng.bell(5, 8);
            } else {
                appearance.rpe = m_rng.bell(3, 6);
            }
        }
    }

    template <class Range> void shuffle(Range& range) {
        // Fisher-Yates with our own generator: std::shuffle differs between standard libraries.
        const int size = static_cast<int>(std::size(range));
        for (int index = size - 1; index > 0; --index) {
            const int other = m_rng.uniform(0, index);
            std::swap(range[static_cast<std::size_t>(index)],
                      range[static_cast<std::size_t>(other)]);
        }
    }

    SplitMix64 m_rng;
    Date m_today;
    DemoDataset m_data;
    std::vector<int> m_quality;
    int m_homeFirst = 0;
    int m_illPlayer = 0;
};

} // namespace

DemoDataset generate(std::uint64_t seed, Date startDate) {
    return Generator(seed, startDate).run();
}

} // namespace fdss::core::demo
