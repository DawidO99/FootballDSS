#include "data/repositories/PlayerRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

const auto kSelectPlayer =
    QStringLiteral("SELECT id, team_id, first_name, last_name, shirt_number, birth_year, "
                   "primary_position, secondary_positions, foot, notes, archived_at FROM player ");

core::Player readPlayer(RowReader& row) {
    core::Player player;
    player.id = row.id<core::PlayerId>();
    player.teamId = row.id<core::TeamId>();
    player.firstName = row.text();
    player.lastName = row.text();
    player.shirtNumber = row.optionalInteger();
    player.birthYear = row.optionalInteger();
    player.primaryPosition = row.enumeration<core::Position>();
    player.secondaryPositions = row.positions();
    player.foot = row.enumeration<core::PreferredFoot>();
    player.notes = row.optionalText();
    player.archivedAt = row.optionalDate();
    return player;
}

} // namespace

core::Result<std::vector<core::Player>> PlayerRepository::list(core::TeamId team,
                                                               ArchivedPlayers archived) const {
    const QString sql =
        archived == ArchivedPlayers::Include
            ? kSelectPlayer + QStringLiteral("WHERE team_id = ? ORDER BY id")
            : kSelectPlayer + QStringLiteral("WHERE team_id = ? AND archived_at IS NULL "
                                             "ORDER BY id");
    return selectAll(m_db, sql, {idValue(team)}, "list players", readPlayer);
}

core::Result<core::Player> PlayerRepository::get(core::PlayerId id) const {
    return selectOne(m_db, kSelectPlayer + QStringLiteral("WHERE id = ?"), {idValue(id)},
                     "get player", readPlayer);
}

core::Result<core::PlayerId> PlayerRepository::create(core::TeamId team,
                                                      const core::PlayerDraft& draft) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO player (team_id, first_name, last_name, shirt_number, "
                       "birth_year, primary_position, secondary_positions, foot, notes) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        {idValue(team), nameValue(draft.firstName), nameValue(draft.lastName),
         optionalValue(draft.shirtNumber), optionalValue(draft.birthYear),
         enumValue(draft.primaryPosition), textValue(positionsToDbText(draft.secondaryPositions)),
         enumValue(draft.foot), optionalTextValue(draft.notes)},
        "create player");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::PlayerId{*id};
}

core::Status PlayerRepository::update(core::PlayerId id, const core::PlayerDraft& draft) {
    return changeRow(
        m_db,
        QStringLiteral("UPDATE player SET first_name = ?, last_name = ?, shirt_number = ?, "
                       "birth_year = ?, primary_position = ?, secondary_positions = ?, foot = ?, "
                       "notes = ? WHERE id = ?"),
        {nameValue(draft.firstName), nameValue(draft.lastName), optionalValue(draft.shirtNumber),
         optionalValue(draft.birthYear), enumValue(draft.primaryPosition),
         textValue(positionsToDbText(draft.secondaryPositions)), enumValue(draft.foot),
         optionalTextValue(draft.notes), idValue(id)},
        "update player");
}

core::Status PlayerRepository::archive(core::PlayerId id, core::Date date) {
    if (auto supported = checkSupportedDate(date, "archived_at"); !supported) {
        return supported;
    }
    return changeRow(
        m_db,
        QStringLiteral("UPDATE player SET archived_at = COALESCE(archived_at, ?) WHERE id = ?"),
        {dateValue(date), idValue(id)}, "archive player");
}

core::Status PlayerRepository::unarchive(core::PlayerId id) {
    return changeRow(m_db, QStringLiteral("UPDATE player SET archived_at = NULL WHERE id = ?"),
                     {idValue(id)}, "unarchive player");
}

} // namespace fdss::data
