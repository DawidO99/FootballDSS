-- FootballDSS schema, version 1. docs/SPEC.md section 6 documents the same tables and columns.
--
-- The runner (src/data/db/Migrations.hpp) splits this file at every semicolon that ends a line.
-- Migration files must not contain a semicolon inside literals or triggers, and every statement
-- ends its own line with one.
--
-- All tables are STRICT (SQLite 3.37 or newer): without it INTEGER columns also store 1.5 or 'junk'.
-- Logical columns hold 0 or 1.
--
-- Dates are ISO text (YYYY-MM-DD)."x IS date(x)" rejects anything else, including impossible dates,
-- and accepts NULL. Times of day are HH:MM; the extra "< 24:00" is needed because the pattern alone
-- allows hours up to 29. CHECK cannot use the current date, so the upper limit of birth_year is
-- checked by the core validation.

CREATE TABLE schema_version (
    id      INTEGER PRIMARY KEY CHECK (id = 1),
    version INTEGER NOT NULL CHECK (version >= 0)
) STRICT;

CREATE TABLE import_log (
    id          INTEGER PRIMARY KEY,
    timestamp   TEXT NOT NULL,
    file        TEXT NOT NULL,
    hash        TEXT NOT NULL,
    type        TEXT NOT NULL CHECK (length(type) > 0),
    rows_ok     INTEGER NOT NULL DEFAULT 0 CHECK (rows_ok >= 0),
    rows_failed INTEGER NOT NULL DEFAULT 0 CHECK (rows_failed >= 0),
    report_json TEXT
) STRICT;

CREATE TABLE club (
    id   INTEGER PRIMARY KEY,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0)
) STRICT;

CREATE TABLE team (
    id       INTEGER PRIMARY KEY,
    club_id  INTEGER NOT NULL REFERENCES club (id) ON DELETE RESTRICT,
    name     TEXT NOT NULL CHECK (length(trim(name)) > 0),
    category TEXT NOT NULL CHECK (category IN ('senior', 'u19', 'u17', 'u15', 'u13', 'u11', 'other')),
    season   TEXT NOT NULL CHECK (season GLOB '[0-9][0-9][0-9][0-9]/[0-9][0-9]')
) STRICT;
CREATE INDEX ix_team_club_id ON team (club_id);

CREATE TABLE staff_user (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL CHECK (length(trim(name)) > 0),
    role            TEXT NOT NULL CHECK (role IN ('coach', 'assistant', 'physio', 'analyst')),
    pin_hash        BLOB NOT NULL,
    salt            BLOB NOT NULL,
    iterations      INTEGER NOT NULL CHECK (iterations > 0),
    failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK (failed_attempts >= 0),
    locked_until    TEXT,
    active          INTEGER NOT NULL DEFAULT 1 CHECK (active IN (0, 1))
) STRICT;

CREATE TABLE player (
    id                  INTEGER PRIMARY KEY,
    team_id             INTEGER NOT NULL REFERENCES team (id) ON DELETE RESTRICT,
    first_name          TEXT NOT NULL CHECK (length(trim(first_name)) BETWEEN 1 AND 50),
    last_name           TEXT NOT NULL CHECK (length(trim(last_name)) BETWEEN 1 AND 50),
    shirt_number        INTEGER CHECK (shirt_number BETWEEN 1 AND 99),
    birth_year          INTEGER CHECK (birth_year >= 1950),
    primary_position    TEXT NOT NULL CHECK (primary_position IN ('gk', 'rb', 'cb', 'lb', 'rwb', 'lwb', 'dm', 'cm', 'am', 'rm', 'lm', 'rw', 'lw', 'st')),
    secondary_positions TEXT NOT NULL DEFAULT '',
    foot                TEXT NOT NULL DEFAULT 'unknown' CHECK (foot IN ('left', 'right', 'both', 'unknown')),
    notes               TEXT,
    archived_at         TEXT CHECK (archived_at IS date(archived_at)),
    import_id           INTEGER REFERENCES import_log (id) ON DELETE RESTRICT
) STRICT;
CREATE INDEX ix_player_team_id ON player (team_id);
CREATE INDEX ix_player_import_id ON player (import_id);
-- An archived player frees the number, and any number of players can have none.
CREATE UNIQUE INDEX ux_player_team_shirt ON player (team_id, shirt_number)
    WHERE archived_at IS NULL AND shirt_number IS NOT NULL;

CREATE TABLE availability (
    id          INTEGER PRIMARY KEY,
    player_id   INTEGER NOT NULL REFERENCES player (id) ON DELETE RESTRICT,
    type        TEXT NOT NULL CHECK (type IN ('injury', 'illness', 'suspension', 'absence')),
    from_date   TEXT NOT NULL CHECK (from_date IS date(from_date)),
    expected_to TEXT CHECK (expected_to IS date(expected_to) AND expected_to >= from_date),
    actual_to   TEXT CHECK (actual_to IS date(actual_to) AND actual_to >= from_date),
    description TEXT
) STRICT;
CREATE INDEX ix_availability_player_id ON availability (player_id);
CREATE INDEX ix_availability_from_date ON availability (from_date);

CREATE TABLE opponent (
    id       INTEGER PRIMARY KEY,
    name     TEXT NOT NULL CHECK (length(trim(name)) > 0),
    name_key TEXT NOT NULL UNIQUE,
    notes    TEXT
) STRICT;

CREATE TABLE match (
    id            INTEGER PRIMARY KEY,
    team_id       INTEGER NOT NULL REFERENCES team (id) ON DELETE RESTRICT,
    opponent_id   INTEGER NOT NULL REFERENCES opponent (id) ON DELETE RESTRICT,
    date          TEXT NOT NULL CHECK (date IS date(date)),
    kickoff_time  TEXT CHECK (kickoff_time GLOB '[0-2][0-9]:[0-5][0-9]' AND kickoff_time < '24:00'),
    venue         TEXT NOT NULL CHECK (venue IN ('home', 'away', 'neutral')),
    competition   TEXT,
    goals_for     INTEGER CHECK (goals_for >= 0),
    goals_against INTEGER CHECK (goals_against >= 0),
    status        TEXT NOT NULL DEFAULT 'planned' CHECK (status IN ('planned', 'live', 'finished')),
    formation     TEXT,
    import_id     INTEGER REFERENCES import_log (id) ON DELETE RESTRICT
) STRICT;
CREATE INDEX ix_match_team_id ON match (team_id);
CREATE INDEX ix_match_opponent_id ON match (opponent_id);
CREATE INDEX ix_match_date ON match (date);
CREATE INDEX ix_match_import_id ON match (import_id);

CREATE TABLE appearance (
    id         INTEGER PRIMARY KEY,
    match_id   INTEGER NOT NULL REFERENCES match (id) ON DELETE CASCADE,
    player_id  INTEGER NOT NULL REFERENCES player (id) ON DELETE RESTRICT,
    started    INTEGER NOT NULL DEFAULT 0 CHECK (started IN (0, 1)),
    minute_on  INTEGER CHECK (minute_on BETWEEN 0 AND 130),
    minute_off INTEGER CHECK (minute_off BETWEEN 0 AND 130 AND minute_off >= minute_on),
    position   TEXT CHECK (position IN ('gk', 'rb', 'cb', 'lb', 'rwb', 'lwb', 'dm', 'cm', 'am', 'rm', 'lm', 'rw', 'lw', 'st')),
    rating     INTEGER CHECK (rating BETWEEN 1 AND 10),
    rpe        INTEGER CHECK (rpe BETWEEN 0 AND 10),
    import_id  INTEGER REFERENCES import_log (id) ON DELETE RESTRICT,
    UNIQUE (match_id, player_id)
) STRICT;
CREATE INDEX ix_appearance_player_id ON appearance (player_id);
CREATE INDEX ix_appearance_import_id ON appearance (import_id);

CREATE TABLE match_event (
    id                INTEGER PRIMARY KEY,
    match_id          INTEGER NOT NULL REFERENCES match (id) ON DELETE CASCADE,
    minute            INTEGER NOT NULL CHECK (minute BETWEEN 0 AND 130),
    type              TEXT NOT NULL CHECK (type IN ('goal', 'assist', 'yellow', 'red', 'sub', 'opponent_goal')),
    player_id         INTEGER REFERENCES player (id) ON DELETE RESTRICT,
    related_player_id INTEGER REFERENCES player (id) ON DELETE RESTRICT,
    CHECK (type = 'opponent_goal' OR player_id IS NOT NULL)
) STRICT;
CREATE INDEX ix_match_event_match_id ON match_event (match_id);
CREATE INDEX ix_match_event_player_id ON match_event (player_id);
CREATE INDEX ix_match_event_related_player_id ON match_event (related_player_id);

CREATE TABLE training_session (
    id           INTEGER PRIMARY KEY,
    team_id      INTEGER NOT NULL REFERENCES team (id) ON DELETE RESTRICT,
    date         TEXT NOT NULL CHECK (date IS date(date)),
    start_time   TEXT CHECK (start_time GLOB '[0-2][0-9]:[0-5][0-9]' AND start_time < '24:00'),
    duration_min INTEGER NOT NULL CHECK (duration_min BETWEEN 15 AND 240),
    type         TEXT NOT NULL CHECK (type IN ('training', 'match_prep', 'recovery', 'gym', 'other')),
    note         TEXT,
    import_id    INTEGER REFERENCES import_log (id) ON DELETE RESTRICT
) STRICT;
CREATE INDEX ix_training_session_team_id ON training_session (team_id);
CREATE INDEX ix_training_session_date ON training_session (date);
CREATE INDEX ix_training_session_import_id ON training_session (import_id);

CREATE TABLE attendance (
    id               INTEGER PRIMARY KEY,
    session_id       INTEGER NOT NULL REFERENCES training_session (id) ON DELETE CASCADE,
    player_id        INTEGER NOT NULL REFERENCES player (id) ON DELETE RESTRICT,
    status           TEXT NOT NULL CHECK (status IN ('present', 'absent', 'excused', 'injured')),
    rpe              INTEGER CHECK (rpe BETWEEN 0 AND 10),
    minutes_override INTEGER CHECK (minutes_override BETWEEN 0 AND 240),
    import_id        INTEGER REFERENCES import_log (id) ON DELETE RESTRICT,
    UNIQUE (session_id, player_id),
    CHECK (status = 'present' OR (rpe IS NULL AND minutes_override IS NULL))
) STRICT;
CREATE INDEX ix_attendance_player_id ON attendance (player_id);
CREATE INDEX ix_attendance_import_id ON attendance (import_id);

CREATE TABLE wellness (
    id        INTEGER PRIMARY KEY,
    player_id INTEGER NOT NULL REFERENCES player (id) ON DELETE RESTRICT,
    date      TEXT NOT NULL CHECK (date IS date(date)),
    sleep     INTEGER NOT NULL CHECK (sleep BETWEEN 1 AND 5),
    fatigue   INTEGER NOT NULL CHECK (fatigue BETWEEN 1 AND 5),
    soreness  INTEGER NOT NULL CHECK (soreness BETWEEN 1 AND 5),
    stress    INTEGER NOT NULL CHECK (stress BETWEEN 1 AND 5),
    mood      INTEGER NOT NULL CHECK (mood BETWEEN 1 AND 5),
    UNIQUE (player_id, date)
) STRICT;
CREATE INDEX ix_wellness_date ON wellness (date);

CREATE TABLE metric_definition (
    id               INTEGER PRIMARY KEY,
    key              TEXT NOT NULL UNIQUE CHECK (length(key) > 0),
    name_pl          TEXT NOT NULL,
    name_en          TEXT NOT NULL,
    value_type       TEXT NOT NULL CHECK (value_type IN ('integer', 'decimal', 'boolean')),
    unit             TEXT,
    min              REAL,
    max              REAL,
    aggregation      TEXT NOT NULL CHECK (aggregation IN ('sum', 'avg', 'min', 'max', 'last')),
    scope            TEXT NOT NULL CHECK (scope IN ('match', 'training', 'player')),
    higher_is_better INTEGER NOT NULL DEFAULT 1 CHECK (higher_is_better IN (0, 1)),
    CHECK (min <= max)
) STRICT;

CREATE TABLE metric_value (
    id         INTEGER PRIMARY KEY,
    metric_id  INTEGER NOT NULL REFERENCES metric_definition (id) ON DELETE RESTRICT,
    player_id  INTEGER NOT NULL REFERENCES player (id) ON DELETE RESTRICT,
    match_id   INTEGER REFERENCES match (id) ON DELETE RESTRICT,
    session_id INTEGER REFERENCES training_session (id) ON DELETE RESTRICT,
    date       TEXT NOT NULL CHECK (date IS date(date)),
    value      REAL NOT NULL,
    import_id  INTEGER REFERENCES import_log (id) ON DELETE RESTRICT,
    CHECK (match_id IS NULL OR session_id IS NULL)
) STRICT;
CREATE INDEX ix_metric_value_metric_id ON metric_value (metric_id);
CREATE INDEX ix_metric_value_player_id ON metric_value (player_id);
CREATE INDEX ix_metric_value_match_id ON metric_value (match_id);
CREATE INDEX ix_metric_value_session_id ON metric_value (session_id);
CREATE INDEX ix_metric_value_date ON metric_value (date);
CREATE INDEX ix_metric_value_import_id ON metric_value (import_id);

CREATE TABLE player_attribute (
    id        INTEGER PRIMARY KEY,
    player_id INTEGER NOT NULL REFERENCES player (id) ON DELETE RESTRICT,
    attribute TEXT NOT NULL CHECK (length(attribute) > 0),
    rating    INTEGER NOT NULL CHECK (rating BETWEEN 1 AND 10),
    date      TEXT NOT NULL CHECK (date IS date(date))
) STRICT;
CREATE INDEX ix_player_attribute_player_id ON player_attribute (player_id);
CREATE INDEX ix_player_attribute_date ON player_attribute (date);

CREATE TABLE lineup (
    id            INTEGER PRIMARY KEY,
    match_id      INTEGER REFERENCES match (id) ON DELETE RESTRICT,
    template_name TEXT,
    formation     TEXT NOT NULL,
    tactic_json   TEXT NOT NULL,
    CHECK ((match_id IS NULL) <> (template_name IS NULL))
) STRICT;
CREATE INDEX ix_lineup_match_id ON lineup (match_id);

CREATE TABLE event_data_file (
    id       INTEGER PRIMARY KEY,
    match_id INTEGER NOT NULL REFERENCES match (id) ON DELETE RESTRICT,
    provider TEXT NOT NULL CHECK (length(provider) > 0),
    path     TEXT NOT NULL,
    hash     TEXT NOT NULL
) STRICT;
CREATE INDEX ix_event_data_file_match_id ON event_data_file (match_id);
