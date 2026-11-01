#include "data/repositories/AvailabilityRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

core::Availability readAvailability(RowReader& row) {
    core::Availability entry;
    entry.id = row.id<core::AvailabilityId>();
    entry.playerId = row.id<core::PlayerId>();
    entry.type = row.enumeration<core::AvailabilityType>();
    entry.fromDate = row.date();
    entry.expectedTo = row.optionalDate();
    entry.actualTo = row.optionalDate();
    entry.description = row.optionalText();
    return entry;
}

} // namespace

core::Result<std::vector<core::Availability>>
AvailabilityRepository::listByPlayer(core::PlayerId player) const {
    return selectAll(
        m_db,
        QStringLiteral("SELECT id, player_id, type, from_date, expected_to, actual_to, "
                       "description FROM availability WHERE player_id = ? "
                       "ORDER BY from_date DESC, id DESC"),
        {idValue(player)}, "list availability of a player", readAvailability);
}

core::Result<std::vector<core::Availability>>
AvailabilityRepository::listActiveOn(core::TeamId team, core::Date date) const {
    // SQL only narrows the rows; isActiveOn() stays the one place that knows when an entry ends.
    const QVariant day = dateValue(date);
    auto entries = selectAll(
        m_db,
        QStringLiteral("SELECT a.id, a.player_id, a.type, a.from_date, a.expected_to, a.actual_to, "
                       "a.description FROM availability AS a "
                       "JOIN player AS p ON p.id = a.player_id "
                       "WHERE p.team_id = ? AND a.from_date <= ? "
                       "AND (p.archived_at IS NULL OR p.archived_at > ?) "
                       "ORDER BY a.player_id, a.from_date, a.id"),
        {idValue(team), day, day}, "list active availability", readAvailability);
    if (entries) {
        std::erase_if(*entries, [&](const core::Availability& entry) {
            return !core::isActiveOn(entry, date);
        });
    }
    return entries;
}

core::Result<core::AvailabilityId>
AvailabilityRepository::create(core::PlayerId player, const core::AvailabilityDraft& draft) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO availability (player_id, type, from_date, expected_to, "
                       "actual_to, description) VALUES (?, ?, ?, ?, ?, ?)"),
        {idValue(player), enumValue(draft.type), dateValue(draft.fromDate),
         optionalDateValue(draft.expectedTo), optionalDateValue(draft.actualTo),
         optionalTextValue(draft.description)},
        "create availability");
    if (!id) {
        return std::unexpected(id.error());
    }
    return core::AvailabilityId{*id};
}

core::Status AvailabilityRepository::update(core::AvailabilityId id,
                                            const core::AvailabilityDraft& draft) {
    QVariantList values{enumValue(draft.type), dateValue(draft.fromDate),
                        optionalDateValue(draft.expectedTo), optionalDateValue(draft.actualTo)};
    if (!draft.description) {
        return changeRow(m_db,
                         QStringLiteral("UPDATE availability SET type = ?, from_date = ?, "
                                        "expected_to = ?, actual_to = ? WHERE id = ?"),
                         values << idValue(id), "update availability");
    }
    return changeRow(m_db,
                     QStringLiteral("UPDATE availability SET type = ?, from_date = ?, "
                                    "expected_to = ?, actual_to = ?, description = ? WHERE id = ?"),
                     values << optionalTextValue(draft.description) << idValue(id),
                     "update availability");
}

core::Status AvailabilityRepository::close(core::AvailabilityId id, core::Date actualTo) {
    return changeRow(m_db, QStringLiteral("UPDATE availability SET actual_to = ? WHERE id = ?"),
                     {dateValue(actualTo), idValue(id)}, "close availability");
}

core::Status AvailabilityRepository::remove(core::AvailabilityId id) {
    return changeRow(m_db, QStringLiteral("DELETE FROM availability WHERE id = ?"), {idValue(id)},
                     "delete availability");
}

} // namespace fdss::data
