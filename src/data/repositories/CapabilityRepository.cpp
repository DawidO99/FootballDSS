#include "data/repositories/CapabilityRepository.hpp"

#include "data/repositories/RepositorySupport.hpp"

namespace fdss::data {

namespace {

// Dates with at least one RPE, of training sessions and of finished matches, through today.
// Positional values: team, today, team, today.
constexpr auto kRpeDates = R"(
    SELECT s.date AS day FROM training_session s
        JOIN attendance a ON a.session_id = s.id
        WHERE s.team_id = ? AND s.date <= ? AND a.status = 'present' AND a.rpe IS NOT NULL
    UNION
    SELECT m.date FROM match m
        JOIN appearance ap ON ap.match_id = m.id
        WHERE m.team_id = ? AND m.date <= ? AND m.status = 'finished' AND ap.rpe IS NOT NULL)";

} // namespace

core::Result<core::DataCounts> CapabilityRepository::counts(core::TeamId team,
                                                            core::Date today) const {
    const QString sql = QStringLiteral(R"(
        WITH rpe_dates AS (%1)
        SELECT
            (SELECT count(*) FROM player WHERE team_id = ? AND archived_at IS NULL),
            (SELECT count(*) FROM match WHERE team_id = ? AND status = 'finished'),
            (SELECT count(*) FROM rpe_dates),
            (SELECT coalesce(CAST(julianday(?) - julianday(min(day)) AS INTEGER) + 1, 0)
                FROM rpe_dates),
            (SELECT count(*) FROM appearance ap JOIN match m ON m.id = ap.match_id
                WHERE m.team_id = ? AND ap.rating IS NOT NULL)
              + (SELECT count(*) FROM player_attribute pa JOIN player p ON p.id = pa.player_id
                WHERE p.team_id = ?)
              + (SELECT count(*) FROM metric_value mv JOIN player p ON p.id = mv.player_id
                WHERE p.team_id = ?),
            (SELECT count(*) FROM match WHERE team_id = ? AND status = 'finished'
                AND goals_for IS NOT NULL AND goals_against IS NOT NULL),
            (SELECT count(*) FROM event_data_file f JOIN match m ON m.id = f.match_id
                WHERE m.team_id = ?))")
                            .arg(QLatin1StringView(kRpeDates));
    const QVariant teamValue = idValue(team);
    const QVariant todayValue = toIsoDate(today);
    return selectOne(m_db, sql,
                     {teamValue, todayValue, teamValue, todayValue, teamValue, teamValue,
                      todayValue, teamValue, teamValue, teamValue, teamValue, teamValue},
                     "count capability data", [](RowReader& row) {
                         core::DataCounts counts;
                         counts.activePlayers = row.integer();
                         counts.finishedMatches = row.integer();
                         counts.rpeDays = row.integer();
                         counts.daysSinceFirstRpe = row.integer();
                         counts.ratingEntries = row.integer();
                         counts.matchesWithResult = row.integer();
                         counts.eventFiles = row.integer();
                         return counts;
                     });
}

} // namespace fdss::data
