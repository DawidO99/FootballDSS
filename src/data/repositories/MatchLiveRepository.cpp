#include "data/repositories/MatchLiveRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

#include <string_view>
#include <utility>

namespace fdss::data {

namespace {

ClockStepRow readStep(RowReader& row) {
    ClockStepRow step;
    step.step.period = row.integer();
    step.step.kind = row.enumeration<core::ClockStepKind>();
    // `at` is NOT NULL, so an empty value cannot come back.
    step.at = row.optionalTimestamp().value_or(core::Timestamp{});
    step.seq = row.integer();
    return step;
}

const auto kSelectStep = QStringLiteral("period, kind, at, seq");

} // namespace

core::Status MatchLiveRepository::create(core::MatchId match, core::MatchFormat format) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO match_live (match_id, periods, "
                                 "period_length) VALUES (?, ?, ?)"),
                  {idValue(match), format.periods, format.periodLength}, "create live match");
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
    auto rows = selectAll(m_db,
                          QStringLiteral("SELECT periods, period_length, finished_seq, "
                                         "ended_early_minute FROM match_live WHERE match_id = ?"),
                          {idValue(match)}, kContext, [](RowReader& row) {
                              LiveMatchRow live;
                              live.format.periods = row.integer();
                              live.format.periodLength = row.integer();
                              live.finishedSeq = row.optionalInteger();
                              live.endedEarlyAt = row.optionalInteger();
                              return live;
                          });
    if (!rows) {
        return std::unexpected(rows.error());
    }
    if (rows->empty()) {
        return std::optional<LiveMatchRow>{};
    }
    auto steps = selectAll(m_db,
                           QStringLiteral("SELECT ") + kSelectStep +
                               QStringLiteral(" FROM match_clock_step WHERE match_id = ? "
                                              "ORDER BY period, kind = 'end', id"),
                           {idValue(match)}, kContext, readStep);
    if (!steps) {
        return std::unexpected(steps.error());
    }
    LiveMatchRow live = std::move(rows->front());
    live.steps = std::move(*steps);
    return std::optional<LiveMatchRow>{std::move(live)};
}

core::Status MatchLiveRepository::appendStep(core::MatchId match, core::ClockStep step,
                                             core::Timestamp at, int seq) {
    const auto id =
        insertRow(m_db,
                  QStringLiteral("INSERT INTO match_clock_step (match_id, period, kind, "
                                 "at, seq) VALUES (?, ?, ?, ?, ?)"),
                  {idValue(match), step.period, enumValue(step.kind), toIsoTimestamp(at), seq},
                  "append clock step");
    if (!id) {
        return std::unexpected(id.error());
    }
    return {};
}

core::Result<std::vector<ClockStepRow>> MatchLiveRepository::deleteSteps(core::MatchId match,
                                                                         int seq) {
    return selectAll(m_db,
                     QStringLiteral("DELETE FROM match_clock_step WHERE match_id = ? AND seq = ? "
                                    "RETURNING ") +
                         kSelectStep,
                     {idValue(match), seq}, "delete clock steps", readStep);
}

core::Status MatchLiveRepository::setFinished(core::MatchId match, std::optional<int> seq,
                                              std::optional<int> endedEarlyAt) {
    return changeRow(
        m_db,
        QStringLiteral("UPDATE match_live SET finished_seq = ?, ended_early_minute = ? "
                       "WHERE match_id = ?"),
        {optionalValue(seq), optionalValue(seq ? endedEarlyAt : std::nullopt), idValue(match)},
        "set live match finished");
}

core::Result<int> MatchLiveRepository::takeSeq(core::MatchId match) {
    return selectOne(m_db,
                     QStringLiteral("UPDATE match_live SET next_seq = next_seq + 1 "
                                    "WHERE match_id = ? RETURNING next_seq - 1"),
                     {idValue(match)}, "take the seq of an action",
                     [](RowReader& row) { return row.integer(); });
}

} // namespace fdss::data
