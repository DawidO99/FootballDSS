-- FootballDSS schema, version 5: housekeeping (packet P19). docs/SPEC.md section 6 documents it.
--
-- staff_user.must_change_pin: 1 after a Coach chose the PIN (a new user or a reset PIN); the user
-- sets their own PIN at the next login, and nothing else is allowed before that.

ALTER TABLE staff_user ADD COLUMN must_change_pin INTEGER NOT NULL DEFAULT 0
    CHECK (must_change_pin IN (0, 1));

-- player.primary_position may be NULL: a player whose position is not given ("—"). ALTER TABLE cannot
-- drop NOT NULL, so the table is built anew by the procedure of the SQLite documentation; the
-- migration runner switches foreign keys off around it and runs foreign_key_check before the commit
-- (P19 decision 3). Same columns, constraints and indexes as in version 1 otherwise.

CREATE TABLE player_new (
    id                  INTEGER PRIMARY KEY,
    team_id             INTEGER NOT NULL REFERENCES team (id) ON DELETE RESTRICT,
    first_name          TEXT NOT NULL CHECK (length(trim(first_name)) BETWEEN 1 AND 50),
    last_name           TEXT NOT NULL CHECK (length(trim(last_name)) BETWEEN 1 AND 50),
    shirt_number        INTEGER CHECK (shirt_number BETWEEN 1 AND 99),
    birth_year          INTEGER CHECK (birth_year >= 1950),
    primary_position    TEXT CHECK (primary_position IN ('gk', 'rb', 'cb', 'lb', 'rwb', 'lwb', 'dm', 'cm', 'am', 'rm', 'lm', 'rw', 'lw', 'st')),
    secondary_positions TEXT NOT NULL DEFAULT '',
    foot                TEXT NOT NULL DEFAULT 'unknown' CHECK (foot IN ('left', 'right', 'both', 'unknown')),
    notes               TEXT,
    archived_at         TEXT CHECK (archived_at IS date(archived_at)),
    import_id           INTEGER REFERENCES import_log (id) ON DELETE RESTRICT
) STRICT;

INSERT INTO player_new (id, team_id, first_name, last_name, shirt_number, birth_year,
                        primary_position, secondary_positions, foot, notes, archived_at, import_id)
    SELECT id, team_id, first_name, last_name, shirt_number, birth_year, primary_position,
           secondary_positions, foot, notes, archived_at, import_id
    FROM player;

DROP TABLE player;

ALTER TABLE player_new RENAME TO player;

CREATE INDEX ix_player_team_id ON player (team_id);
CREATE INDEX ix_player_import_id ON player (import_id);
-- An archived player frees the number, and any number of players can have none.
CREATE UNIQUE INDEX ux_player_team_shirt ON player (team_id, shirt_number)
    WHERE archived_at IS NULL AND shirt_number IS NOT NULL;
