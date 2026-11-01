#include "data/repositories/ClubRepository.hpp"

#include "data/db/Transaction.hpp"
#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

core::Club readClub(RowReader& row) {
    core::Club club;
    club.id = row.id<core::ClubId>();
    club.name = row.text();
    return club;
}

core::Team readTeam(RowReader& row) {
    core::Team team;
    team.id = row.id<core::TeamId>();
    team.clubId = row.id<core::ClubId>();
    team.name = row.text();
    team.category = row.enumeration<core::TeamCategory>();
    team.season = row.text();
    return team;
}

} // namespace

core::Result<core::Club> ClubRepository::getClub() const {
    return selectOne(m_db, QStringLiteral("SELECT id, name FROM club ORDER BY id LIMIT 1"), {},
                     "get club", readClub);
}

core::Result<core::ClubId> ClubRepository::createClub(const core::ClubDraft& draft) {
    auto transaction = Transaction::begin(m_db);
    if (!transaction) {
        return std::unexpected(transaction.error());
    }
    const auto existing = getClub();
    if (existing) {
        return std::unexpected(
            core::Error{core::ErrorCode::Conflict, "the database already has a club", {}});
    }
    if (existing.error().code != core::ErrorCode::NotFound) {
        return std::unexpected(existing.error());
    }
    const auto id = insertRow(m_db, QStringLiteral("INSERT INTO club (name) VALUES (?)"),
                              {nameValue(draft.name)}, "create club");
    if (!id) {
        return std::unexpected(id.error());
    }
    if (auto status = transaction->commit(); !status) {
        return std::unexpected(status.error());
    }
    return core::ClubId{*id};
}

core::Result<std::vector<core::Team>> ClubRepository::listTeams(core::ClubId club) const {
    return selectAll(m_db,
                     QStringLiteral("SELECT id, club_id, name, category, season FROM team "
                                    "WHERE club_id = ? ORDER BY id"),
                     {idValue(club)}, "list teams", readTeam);
}

core::Result<core::TeamId> ClubRepository::createTeam(core::ClubId club,
                                                      const core::TeamDraft& draft) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO team (club_id, name, category, season) "
                       "VALUES (?, ?, ?, ?)"),
        {idValue(club), nameValue(draft.name), enumValue(draft.category), textValue(draft.season)},
        "create team");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::TeamId{*id};
}

core::Status ClubRepository::updateTeam(core::TeamId team, const core::TeamDraft& draft) {
    return changeRow(
        m_db, QStringLiteral("UPDATE team SET name = ?, category = ?, season = ? WHERE id = ?"),
        {nameValue(draft.name), enumValue(draft.category), textValue(draft.season), idValue(team)},
        "update team");
}

} // namespace fdss::data
