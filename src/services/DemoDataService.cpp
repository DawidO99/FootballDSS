#include "services/DemoDataService.hpp"

#include "core/demo/DemoGenerator.hpp"
#include "core/domain/Permissions.hpp"
#include "core/domain/Validation.hpp"
#include "data/db/Transaction.hpp"
#include "data/repositories/AvailabilityRepository.hpp"
#include "data/repositories/MatchRepository.hpp"
#include "data/repositories/OpponentRepository.hpp"
#include "data/repositories/PlayerRepository.hpp"
#include "data/repositories/StaffRepository.hpp"
#include "data/repositories/TrainingRepository.hpp"
#include "services/AuthService.hpp"
#include "services/Clock.hpp"
#include "services/ServiceSupport.hpp"
#include "services/Session.hpp"

#include <chrono>
#include <cstddef>
#include <string>

namespace fdss::services {

namespace {

namespace demo = core::demo;

template <class Id> Id at(const std::vector<Id>& ids, int index) {
    return ids[static_cast<std::size_t>(index)];
}

std::optional<core::PlayerId> playerAt(const std::vector<core::PlayerId>& ids,
                                       std::optional<int> index) {
    if (!index) {
        return std::nullopt;
    }
    return at(ids, *index);
}

} // namespace

DemoDataService::DemoDataService(data::DatabaseManager& db, data::PlayerRepository& players,
                                 data::AvailabilityRepository& availability,
                                 data::OpponentRepository& opponents,
                                 data::MatchRepository& matches, data::TrainingRepository& training,
                                 data::StaffRepository& staff, const AuthService& auth,
                                 const Session& session, const Clock& clock)
    : m_db(db), m_players(players), m_availability(availability), m_opponents(opponents),
      m_matches(matches), m_training(training), m_staff(staff), m_auth(auth), m_session(session),
      m_clock(clock) {}

core::Result<DemoSummary> DemoDataService::load(core::TeamId team) {
    if (auto allowed = m_session.require(core::Permission::ManageStaff); !allowed) {
        return std::unexpected(allowed.error());
    }
    if (auto empty = ensureEmpty(team); !empty) {
        return std::unexpected(empty.error());
    }
    const core::Date today = m_clock.today();
    const demo::DemoDataset data =
        demo::generate(demo::kDemoSeed, core::Date{std::chrono::sys_days{today} -
                                                   std::chrono::days{demo::kDemoDays}});

    // Before the transaction: the write lock is not held for the length of a hash.
    std::vector<core::PinHash> staffPins;
    for (std::size_t index = 0; index < demo::kDemoStaff.size(); ++index) {
        auto hash = m_auth.hashPin(demo::kDemoStaffPin);
        if (!hash) {
            return std::unexpected(hash.error());
        }
        staffPins.push_back(std::move(*hash));
    }

    auto transaction = data::Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    if (auto stored = store(team, data, staffPins); !stored) {
        return std::unexpected(stored.error());
    }
    if (auto committed = transaction->commit(); !committed) {
        return std::unexpected(committed.error());
    }
    return DemoSummary{static_cast<int>(data.players.size()),
                       static_cast<int>(data.sessions.size()),
                       static_cast<int>(data.matches.size())};
}

core::Status DemoDataService::ensureEmpty(core::TeamId team) const {
    const auto players = m_players.list(team, data::ArchivedPlayers::Include);
    if (!players) {
        return std::unexpected(players.error());
    }
    const auto matches = m_matches.list(team, core::DateRange{});
    if (!matches) {
        return std::unexpected(matches.error());
    }
    const auto sessions = m_training.list(team, core::DateRange{});
    if (!sessions) {
        return std::unexpected(sessions.error());
    }
    if (!players->empty() || !matches->empty() || !sessions->empty()) {
        return std::unexpected(
            core::Error{core::ErrorCode::Conflict, "the team already has data", {}});
    }
    return {};
}

core::Status DemoDataService::store(core::TeamId team, const demo::DemoDataset& data,
                                    const std::vector<core::PinHash>& staffPins) {
    const int currentYear = static_cast<int>(data.today.year());

    std::vector<core::PlayerId> players;
    for (const demo::DemoPlayer& player : data.players) {
        if (auto valid = firstError(core::validate(player.draft, currentYear)); !valid) {
            return valid;
        }
        const auto id = m_players.create(team, player.draft);
        if (!id) {
            return std::unexpected(id.error());
        }
        players.push_back(*id);
        if (player.archivedAt) {
            if (auto archived = m_players.archive(*id, *player.archivedAt); !archived) {
                return archived;
            }
        }
    }

    for (const demo::DemoAvailability& entry : data.availability) {
        if (auto valid = firstError(core::validate(entry.draft)); !valid) {
            return valid;
        }
        if (const auto id = m_availability.create(at(players, entry.player), entry.draft); !id) {
            return std::unexpected(id.error());
        }
    }

    std::vector<core::OpponentId> opponents;
    for (const core::OpponentDraft& opponent : data.opponents) {
        if (auto valid = firstError(core::validate(opponent)); !valid) {
            return valid;
        }
        const auto id = m_opponents.create(opponent);
        if (!id) {
            return std::unexpected(id.error());
        }
        opponents.push_back(*id);
    }

    for (const demo::DemoMatch& match : data.matches) {
        core::MatchDraft draft = match.draft;
        draft.opponentId = at(opponents, match.opponent);
        if (auto valid = firstError(core::validate(draft)); !valid) {
            return valid;
        }
        const auto id = m_matches.create(team, draft);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (match.status == core::MatchStatus::Planned) {
            continue;
        }
        std::vector<core::AppearanceDraft> appearances;
        for (const demo::DemoAppearance& a : match.appearances) {
            core::AppearanceDraft appearance{at(players, a.player),
                                             a.started,
                                             a.minuteOn,
                                             a.minuteOff,
                                             a.position,
                                             a.rating,
                                             a.rpe};
            if (auto valid = firstError(core::validate(appearance)); !valid) {
                return valid;
            }
            appearances.push_back(appearance);
        }
        if (auto replaced = m_matches.replaceAppearances(*id, appearances); !replaced) {
            return replaced;
        }
        for (const demo::DemoEvent& e : match.events) {
            const core::MatchEventDraft event{e.minute, e.type, playerAt(players, e.player),
                                              playerAt(players, e.relatedPlayer)};
            if (auto valid = firstError(core::validate(event)); !valid) {
                return valid;
            }
            if (const auto eventId = m_matches.appendEvent(*id, event); !eventId) {
                return std::unexpected(eventId.error());
            }
        }
        if (auto result =
                m_matches.setResult(*id, match.goalsFor, match.goalsAgainst, match.status);
            !result) {
            return result;
        }
    }

    for (const demo::DemoSession& session : data.sessions) {
        if (auto valid = firstError(core::validate(session.draft)); !valid) {
            return valid;
        }
        const auto id = m_training.create(team, session.draft);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (session.attendance.empty()) {
            continue;
        }
        std::vector<core::AttendanceDraft> attendance;
        for (const demo::DemoAttendance& a : session.attendance) {
            core::AttendanceDraft row{at(players, a.player), a.status, a.rpe, std::nullopt};
            if (auto valid = firstError(core::validate(row)); !valid) {
                return valid;
            }
            attendance.push_back(row);
        }
        if (auto saved = m_training.saveAttendance(*id, attendance); !saved) {
            return saved;
        }
    }

    for (std::size_t index = 0; index < demo::kDemoStaff.size(); ++index) {
        core::StaffUserDraft member;
        member.name = std::string(demo::kDemoStaff[index].name);
        member.role = demo::kDemoStaff[index].role;
        if (auto valid = firstError(core::validate(member)); !valid) {
            return valid;
        }
        if (const auto id = m_staff.create(member, staffPins[index]); !id) {
            return std::unexpected(id.error());
        }
    }
    return {};
}

} // namespace fdss::services
