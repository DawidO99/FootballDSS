#include "services/LoadService.hpp"

#include "core/domain/Permissions.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/Clock.hpp"
#include "services/Session.hpp"

#include <QtConcurrentRun>

#include <chrono>
#include <utility>

namespace fdss::services {

namespace {

struct PlayerInput {
    core::Player player;
    std::vector<core::LoadEntry> entries;
};

// On the thread pool now or, given the gates of a hold, once it is released.
template <class Work> auto start(std::vector<QPromise<void>>* held, Work work) {
    if (held == nullptr) {
        return QtConcurrent::run(std::move(work));
    }
    QPromise<void>& gate = held->emplace_back();
    gate.start();
    return gate.future().then(QtFuture::Launch::Async, std::move(work));
}

core::DateRange historyRange(core::Date today) {
    using std::chrono::days;
    using std::chrono::sys_days;
    return core::DateRange{core::Date{sys_days{today} - days{LoadService::kLoadHistoryDays}},
                           today};
}

} // namespace

LoadService::LoadService(data::PlayerRepository& players, data::TrainingRepository& training,
                         data::MatchRepository& matches, Session& session, const Clock& clock)
    : m_players(players), m_training(training), m_matches(matches), m_session(session),
      m_clock(clock) {}

const core::LoadThresholds& LoadService::thresholds() const { return m_thresholds; }

core::Result<QFuture<TeamLoad>> LoadService::teamLoad(core::TeamId team) const {
    if (auto allowed = m_session.require(core::Permission::ViewAnalysis); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto players = m_players.list(team, data::ArchivedPlayers::Exclude);
    if (!players) {
        return std::unexpected(players.error());
    }
    const core::Date today = m_clock.today();
    const core::DateRange range = historyRange(today);
    auto matchEntries = readMatchEntries(team, range);
    if (!matchEntries) {
        return std::unexpected(matchEntries.error());
    }

    std::vector<PlayerInput> inputs;
    inputs.reserve(players->size());
    for (core::Player& player : *players) {
        PlayerInput input{std::move(player), {}};
        if (auto read = readSessionEntries(input.player.id, range, input.entries); !read) {
            return std::unexpected(read.error());
        }
        if (auto found = matchEntries->find(input.player.id); found != matchEntries->end()) {
            input.entries.insert(input.entries.end(), found->second.begin(), found->second.end());
        }
        inputs.push_back(std::move(input));
    }

    std::vector<QPromise<void>>* const gates = m_holding ? &m_held : nullptr;
    return start(gates, [inputs = std::move(inputs), today, thresholds = m_thresholds]() mutable {
        TeamLoad result{today, thresholds, {}};
        result.players.reserve(inputs.size());
        for (PlayerInput& input : inputs) {
            result.players.push_back(
                PlayerLoadRow{std::move(input.player),
                              core::evaluateLoad(input.entries, today, thresholds).assessment});
        }
        return result;
    });
}

core::Result<QFuture<PlayerLoadDetail>> LoadService::playerLoad(core::PlayerId id) const {
    if (auto allowed = m_session.require(core::Permission::ViewAnalysis); !allowed) {
        return std::unexpected(allowed.error());
    }
    auto player = m_players.get(id);
    if (!player) {
        return std::unexpected(player.error());
    }
    const core::Date today = m_clock.today();
    const core::DateRange range = historyRange(today);
    auto matchEntries = readMatchEntries(player->teamId, range);
    if (!matchEntries) {
        return std::unexpected(matchEntries.error());
    }
    PlayerInput input{std::move(*player), {}};
    if (auto read = readSessionEntries(id, range, input.entries); !read) {
        return std::unexpected(read.error());
    }
    if (auto found = matchEntries->find(id); found != matchEntries->end()) {
        input.entries.insert(input.entries.end(), found->second.begin(), found->second.end());
    }

    std::vector<QPromise<void>>* const gates = m_holding ? &m_held : nullptr;
    return start(gates, [input = std::move(input), today, thresholds = m_thresholds]() mutable {
        core::PlayerLoad load = core::evaluateLoad(input.entries, today, thresholds);
        return PlayerLoadDetail{std::move(input.player), today, thresholds, std::move(load)};
    });
}

void LoadService::holdResults() { m_holding = true; }

void LoadService::releaseResults() {
    m_holding = false;
    for (QPromise<void>& gate : std::exchange(m_held, {})) {
        gate.finish();
    }
}

core::Result<LoadService::EntriesByPlayer>
LoadService::readMatchEntries(core::TeamId team, core::DateRange range) const {
    auto matches = m_matches.list(team, range);
    if (!matches) {
        return std::unexpected(matches.error());
    }
    EntriesByPlayer entries;
    for (const core::Match& match : *matches) {
        // A planned match has no minutes yet, a live one not the final ones (P13).
        if (match.status != core::MatchStatus::Finished) {
            continue;
        }
        const auto appearances = m_matches.listAppearances(match.id);
        if (!appearances) {
            return std::unexpected(appearances.error());
        }
        for (const core::Appearance& appearance : *appearances) {
            if (const auto entry = core::matchLoadEntry(appearance, match.date)) {
                entries[appearance.playerId].push_back(*entry);
            }
        }
    }
    return entries;
}

core::Status LoadService::readSessionEntries(core::PlayerId player, core::DateRange range,
                                             std::vector<core::LoadEntry>& entries) const {
    const auto rows = m_training.listAttendanceByPlayer(player, range);
    if (!rows) {
        return std::unexpected(rows.error());
    }
    for (const core::PlayerAttendance& row : *rows) {
        if (const auto entry = core::sessionLoadEntry(row)) {
            entries.push_back(*entry);
        }
    }
    return {};
}

} // namespace fdss::services
