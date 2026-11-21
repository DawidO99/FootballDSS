-- FootballDSS schema, version 3: live tactics (packet P13b). docs/SPEC.md section 6 documents it.
--
-- match_tactic_change: a change of the shape during the match, one row per Match Day action. The 11
-- slots of the saved lineup keep their index for the whole match; a substitute takes the slot of the
-- player going off, a sent-off player leaves his slot empty. tactic_json holds the new places of the
-- 11 slots (.tactic.json version 1, every playerId null, no bench): who stands where comes from the
-- lineup and the events. formation: the name counted from the occupied slots when the change was
-- recorded (no translated text); the shape shown is always counted again from the lineup, the events
-- and the changes, since a red card entered later may change it. minute and added_time follow
-- match_event; seq is the action of the undo stack (match_live.next_seq).

CREATE TABLE match_tactic_change (
    id          INTEGER PRIMARY KEY,
    match_id    INTEGER NOT NULL REFERENCES match (id) ON DELETE CASCADE,
    minute      INTEGER NOT NULL CHECK (minute BETWEEN 0 AND 130),
    added_time  INTEGER CHECK (added_time >= 1),
    formation   TEXT NOT NULL CHECK (length(formation) BETWEEN 1 AND 20),
    tactic_json TEXT NOT NULL,
    seq         INTEGER NOT NULL CHECK (seq >= 1),
    UNIQUE (match_id, seq)
) STRICT;
CREATE INDEX ix_match_tactic_change_match_id ON match_tactic_change (match_id);
