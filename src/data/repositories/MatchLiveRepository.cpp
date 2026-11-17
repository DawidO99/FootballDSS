#include "data/repositories/MatchLiveRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

#include <string_view>

namespace fdss::data {

namespace {

ClockStepRow readStep(RowReader& row) {
    ClockStepRow step;
    step.step = row.enumeration<core::ClockStep>();
    // `at` is NOT NULL, so an empty value cannot come back.
    step.at = row.optionalTimestamp().value_or(core::Timestamp{});
    step.afterEvent = row.optionalId<core::MatchEventId>();
    return step;
}

} // namespace

core::Status MatchLiveRepository::create(core::MatchId match, int halfLength) {
    const auto id = insertRow(
        m_db, QStringLiteral("INSERT INTO match_live (match_id, half_length_min) VALUES (?, ?)"),
        {idValue(match), halfLength}, "create live match");
    if (!id) {
        return std::unexpected(id.error());
    }
    return {};
}

core::Status MatchLiveRepository::remove(core::MatchId match) {
    return changeRow(m_db, QStringLiteral("DELETE FROM match_live WHERE match_id = ?"),
                     {idValue(match)}, "remove live match");
}

core::Result<std::optional<LiveMatchRow>> MatchLiveRepository::find(core::MatchId match) const {
    constexpr std::string_view kContext = "find live match";
    auto halfLength =
        selectAll(m_db, QStringLiteral("SELECT half_length_min FROM match_live WHERE match_id = ?"),
                  {idValue(match)}, kContext, [](RowReader& row) { return row.integer(); });
    if (!halfLength) {
        return std::unexpected(halfLength.error());
    }
    if (halfLength->empty()) {
        return std::optional<LiveMatchRow>{};
    }
    auto steps = selectAll(m_db,
                           QStringLiteral("SELECT step, at, after_event_id FROM match_clock_step "
                                          "WHERE match_id = ? ORDER BY id"),
                           {idValue(match)}, kContext, readStep);
    if (!steps) {
        return std::unexpected(steps.error());
    }
    return std::optional<LiveMatchRow>{LiveMatchRow{halfLength->front(), std::move(*steps)}};
}

core::Status MatchLiveRepository::appendStep(core::MatchId match, core::ClockStep step,
                                             core::Timestamp at,
                                             std::optional<core::MatchEventId> afterEvent) {
    const auto id = insertRow(
        m_db,
        QStringLiteral("INSERT INTO match_clock_step (match_id, step, at, "
                       "after_event_id) VALUES (?, ?, ?, ?)"),
        {idValue(match), enumValue(step), toIsoTimestamp(at), optionalIdValue(afterEvent)},
        "append clock step");
    if (!id) {
        return std::unexpected(id.error());
    }
    return {};
}

core::Result<ClockStepRow> MatchLiveRepository::deleteLastStep(core::MatchId match) {
    return selectOne(m_db,
                     QStringLiteral("DELETE FROM match_clock_step WHERE id = "
                                    "(SELECT MAX(id) FROM match_clock_step WHERE match_id = ?) "
                                    "RETURNING step, at, after_event_id"),
                     {idValue(match)}, "delete last clock step", readStep);
}

} // namespace fdss::data
