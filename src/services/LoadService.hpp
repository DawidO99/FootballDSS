#pragma once

#include "core/algorithms/Load.hpp"
#include "core/domain/DateTypes.hpp"
#include "core/domain/Ids.hpp"
#include "core/domain/Player.hpp"
#include "core/util/Error.hpp"

#include <QFuture>
#include <QPromise>

#include <map>
#include <vector>

namespace fdss::data {
class MatchRepository;
class PlayerRepository;
class TrainingRepository;
} // namespace fdss::data

namespace fdss::services {

class Clock;
class Session;

struct PlayerLoadRow {
    core::Player player;
    core::LoadAssessment assessment;
};

struct TeamLoad {
    core::Date today{};
    core::LoadThresholds thresholds;
    // Players who are not archived, in the order of the repository.
    std::vector<PlayerLoadRow> players;
};

struct PlayerLoadDetail {
    core::Player player;
    core::Date today{};
    core::LoadThresholds thresholds;
    core::PlayerLoad load;
};

// Load monitoring (SPEC section 7.1, packet P11). Needs ViewAnalysis, which every role has, and
// writes nothing. Reads on the calling thread, which must be the main thread because the database
// connection lives there, and computes on the global thread pool (AGENTS section 5): a read failure
// is the unexpected value, the computation itself cannot fail. Sessions of the last
// kLoadHistoryDays days through today count (present rows), and finished matches of that time.
class LoadService {
public:
    // Older entries would change the EWMA by less than 10^-11 (the chronic weight after a year).
    static constexpr int kLoadHistoryDays = 365;

    LoadService(data::PlayerRepository& players, data::TrainingRepository& training,
                data::MatchRepository& matches, Session& session, const Clock& clock);

    // The defaults of SPEC section 7.1 until P19 reads them from the settings.
    [[nodiscard]] const core::LoadThresholds& thresholds() const;

    [[nodiscard]] core::Result<QFuture<TeamLoad>> teamLoad(core::TeamId team) const;
    // Also for an archived player: the card shows the history.
    [[nodiscard]] core::Result<QFuture<PlayerLoadDetail>> playerLoad(core::PlayerId player) const;

    // Tests only. While held, the computation of every read waits for releaseResults(), so a test
    // sees a screen during a read whatever the speed of the machine: a result that is ready before
    // the viewmodel attaches its continuation comes in at once, inside the call that started the
    // read. Holding again while held changes nothing.
    void holdResults();
    void releaseResults();

private:
    using EntriesByPlayer = std::map<core::PlayerId, std::vector<core::LoadEntry>>;

    [[nodiscard]] core::Result<EntriesByPlayer> readMatchEntries(core::TeamId team,
                                                                 core::DateRange range) const;
    // Appends to `entries`.
    [[nodiscard]] core::Status readSessionEntries(core::PlayerId player, core::DateRange range,
                                                  std::vector<core::LoadEntry>& entries) const;

    data::PlayerRepository& m_players;
    data::TrainingRepository& m_training;
    data::MatchRepository& m_matches;
    Session& m_session;
    const Clock& m_clock;
    core::LoadThresholds m_thresholds;
    bool m_holding = false;
    // A gate per read started while held, opened by releaseResults(): a future runs only one
    // continuation, so the reads cannot share one.
    mutable std::vector<QPromise<void>> m_held;
};

} // namespace fdss::services
