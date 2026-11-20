-- FootballDSS schema, version 2: Match Day (packet P13). docs/SPEC.md section 6 documents it.
--
-- match_live: a match that Match Day has started, in its format: 1-4 parts of 10-60 minutes, at most
-- 130 minutes in all (the minutes of match_event and appearance). next_seq: the seq of the next
-- action; it only grows, so a seq is never used again, not even after undo or a deleted note.
-- finished_seq: the action that finished the match; empty while live. ended_early_minute: the final
-- minute of a match finished with fewer than 7 players (the minute of its end, in a break the end of
-- the part played, never before a stored event); empty for a normal end (parts x length).
-- clockless: Match Day without the clock (minutes typed, e.g. entered after the match); then there
-- are no clock steps.
-- match_clock_step: the start or the end of a part (UTC ISO datetime, as lockout timestamps); "set
-- clock" moves the start of the running part.
-- match_note: a note with its minute; it never changes the score or the minutes.
-- seq (match_event, match_note, match_clock_step): the order of entry of the actions of Match Day in
-- one match, from match_live.next_seq. The rows of one action share it (a second yellow and its
-- red); undo takes back the rows with the highest seq, whatever their minute. A note restored after
-- a delete keeps its seq. Empty for rows written otherwise.
-- match_event.added_time: minutes over the end of the part, for display only; minute then holds the
-- end of the part.

CREATE TABLE match_live (
    id            INTEGER PRIMARY KEY,
    match_id      INTEGER NOT NULL UNIQUE REFERENCES match (id) ON DELETE CASCADE,
    periods       INTEGER NOT NULL CHECK (periods BETWEEN 1 AND 4),
    period_length INTEGER NOT NULL CHECK (period_length BETWEEN 10 AND 60),
    clockless     INTEGER NOT NULL DEFAULT 0 CHECK (clockless IN (0, 1)),
    next_seq      INTEGER NOT NULL DEFAULT 1 CHECK (next_seq >= 1),
    finished_seq  INTEGER CHECK (finished_seq >= 1),
    ended_early_minute INTEGER CHECK (ended_early_minute BETWEEN 0 AND 130),
    CHECK (periods * period_length <= 130)
) STRICT;

CREATE TABLE match_clock_step (
    id       INTEGER PRIMARY KEY,
    match_id INTEGER NOT NULL REFERENCES match_live (match_id) ON DELETE CASCADE,
    period   INTEGER NOT NULL CHECK (period BETWEEN 1 AND 4),
    kind     TEXT NOT NULL CHECK (kind IN ('start', 'end')),
    at       TEXT NOT NULL,
    seq      INTEGER NOT NULL CHECK (seq >= 1),
    UNIQUE (match_id, period, kind)
) STRICT;

CREATE TABLE match_note (
    id         INTEGER PRIMARY KEY,
    match_id   INTEGER NOT NULL REFERENCES match (id) ON DELETE CASCADE,
    minute     INTEGER NOT NULL CHECK (minute BETWEEN 0 AND 130),
    added_time INTEGER CHECK (added_time >= 1),
    player_id  INTEGER REFERENCES player (id) ON DELETE RESTRICT,
    text       TEXT NOT NULL CHECK (length(trim(text)) BETWEEN 1 AND 500),
    seq        INTEGER CHECK (seq >= 1)
) STRICT;
CREATE INDEX ix_match_note_match_id ON match_note (match_id);
CREATE INDEX ix_match_note_player_id ON match_note (player_id);

ALTER TABLE match_event ADD COLUMN added_time INTEGER CHECK (added_time >= 1);
ALTER TABLE match_event ADD COLUMN seq INTEGER CHECK (seq >= 1);
