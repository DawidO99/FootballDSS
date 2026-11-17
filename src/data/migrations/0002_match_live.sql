-- FootballDSS schema, version 2: Match Day (packet P13). docs/SPEC.md section 6 documents it.
--
-- match_live: a match that Match Day has started, with the length of a half in minutes.
-- match_clock_step: when each step of the clock happened (UTC ISO datetime, as lockout timestamps).
-- after_event_id is the highest match_event id of the match when the step was taken (no foreign key:
-- the event may be undone later). Undo compares it with the last event to find the last action,
-- because match_event has no time column.
-- match_event.added_time: minutes over the end of the half, for display only; minute then holds the
-- end of the half.

CREATE TABLE match_live (
    id              INTEGER PRIMARY KEY,
    match_id        INTEGER NOT NULL UNIQUE REFERENCES match (id) ON DELETE CASCADE,
    half_length_min INTEGER NOT NULL CHECK (half_length_min BETWEEN 10 AND 60)
) STRICT;

CREATE TABLE match_clock_step (
    id             INTEGER PRIMARY KEY,
    match_id       INTEGER NOT NULL REFERENCES match_live (match_id) ON DELETE CASCADE,
    step           TEXT NOT NULL CHECK (step IN ('first_half_start', 'first_half_end', 'second_half_start', 'match_end')),
    at             TEXT NOT NULL,
    after_event_id INTEGER,
    UNIQUE (match_id, step)
) STRICT;

ALTER TABLE match_event ADD COLUMN added_time INTEGER CHECK (added_time >= 1);
