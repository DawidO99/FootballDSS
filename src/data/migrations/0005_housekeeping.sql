-- FootballDSS schema, version 5: housekeeping (packet P19). docs/SPEC.md section 6 documents it.
--
-- staff_user.must_change_pin: 1 after a Coach chose the PIN (a new user or a reset PIN); the user
-- sets their own PIN at the next login, and nothing else is allowed before that.

ALTER TABLE staff_user ADD COLUMN must_change_pin INTEGER NOT NULL DEFAULT 0
    CHECK (must_change_pin IN (0, 1));
