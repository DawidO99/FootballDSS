# P18 — Import of training, matches and metric columns

**Branch:** `feature/P18-import-data`
**Depends on:** P16 and P17 merged (P13 merged too, for match minutes semantics)
**Owner agent:** Claude (effort High); review: Codex

## Goal
A club that kept attendance, RPE and match minutes in Excel brings its history into the app in
minutes, so load monitoring and the dashboard work from day one.

Read first: SPEC §8, P16 Decisions (framework), P17 Decisions (metrics), P13 Decisions (event
semantics).

## Scope — files you may create or change
- `src/core/import/*`, `src/services/import/*` (new import types on the P16 framework)
- small additions to `TrainingRepository`, `MatchRepository`, `MetricRepository` for `import_id`
  (the BACKLOG items about losing `import_id` on save/undo belong here if they block undo)
- `src/viewmodels/ImportWizardViewModel` and models, `ui/views/ImportView.qml`, wizard components
- `resources/import-templates/` (training and match templates, CSV + XLSX)
- fixtures in `tests/data/import/`, tests, CMake lists, `i18n/*.ts`, `qml_smoke`

## Out of scope
StatsBomb JSON (W3), league tables import, wellness import.

## Requirements
1. **Training** (long format, one row per player per session): date, optional start time, optional
   duration and type, player (number or name), status, RPE, minutes. Sessions are found by date + start
   time or created; attendance rows are inserted or updated. Wide format (one column per date) is
   detected and refused with a message pointing to the template, unless the plan proposes a safe
   conversion.
2. **Matches** (one row per player per match): date, opponent, venue, competition, score (for/against),
   player, started, minutes, goals, assists, yellow/red, rating, RPE. Matches found by date + opponent
   or created as `finished`; appearances with `minute_on`/`minute_off` from "minutes" and "started".
   Goals and cards without a minute: the plan proposes how to store them (schema requires a minute) —
   ask, do not invent a rule silently.
3. **Metric columns**: a column not matched to a known field can be mapped to an existing custom metric
   or "create metric" (opens the P17 definition dialog prefilled with the header).
4. Player matching as in P16 (number or name, ambiguity listed); unknown players: skip or create
   (user's choice per import).
5. Validation ranges per SPEC §8 (minutes 0–130 in matches, 0–240 in training, RPE 0–10), dates in the
   formats of SPEC §8 (`dd.MM.yyyy`, `yyyy-MM-dd`, `dd/MM/yyyy`, Excel serial dates in XLSX).
6. Commit, backup, `import_log`, undo import as in P16; undo removes created sessions/matches with their
   child rows and reverts attendance/appearances created by the import.
7. Permission `Import`.

## Tests
Fixtures for both types: CP1250 `;` CSV with decimal comma, XLSX with Excel dates, the same file twice,
a row for an unknown player, ranges, ambiguous names. After importing the training fixture the load
(P11) of a player equals a hand calculation. Undo import restores the database to its previous state.

## Done when
- [ ] Import 4 weeks of training and 3 matches from the templates → Load and Dashboard show data.
- [ ] Undo import → everything from that file is gone, nothing else changed.
- [ ] Section 3 passes; sanitizer CI on `main` after merge.

## Notes for the thesis
Before/after screenshots of the Load screen: the value of import for a club with history in Excel.

## Decisions (accepted plan)
1. **Locations** (P16 decision 1–2 over the packet text): pure parsing in `src/core/parsing/`
   (`ImportValues`, `PlayerMatching`, `ActivityImport`, `TrainingImport`, `MatchImport`); writers in
   `src/services/import/`; templates in `ui/import-templates/` (`training`, `matches`, CSV + XLSX, Polish
   headers, one example row skipped with a note, an "Instrukcja" sheet). No new top-level folder.
2. **Goals, assists and cards** are not stored as `match_event` (the schema needs a minute and none is
   invented). The score goes to `goals_for`/`goals_against`. The columns are recognised; the report and the
   summary say "Not imported: N goals, N assists, N cards — no minute; enter them in the match report" and
   warn that imported yellow cards do not count toward a suspension. Counters in `appearance` are in
   `docs/BACKLOG.md`.
3. **Existing rows**: a switch "Update existing" (off by default). Identical rows are "unchanged". An
   update stores the previous values (attendance, appearance, metric value) in `report_json`; undo restores
   them when the row still holds the imported values, otherwise counts it as not reverted.
4. **Existing match** (date + opponent): with `match_live` or `live` → row error (Match Day is the source
   of truth); `planned` → row error ("finish it in Match Day without the clock or delete it"); `finished`
   without `match_live` → only missing appearances are added; a different score is a warning, the stored
   score stays.
5. **Appearance minutes**: the final minute is the largest "minutes" among the rows of the match (90 when
   none). Starter: 0 → minutes. Substitute with minutes > 0: final − minutes → final. Not started with 0 or
   empty minutes: bench (on/off empty). Started with 0 minutes: row error. A substitute taken off later is
   approximated; the load (RPE × minutes) is exact.
6. **Session**: found by date + start time; without a time column the only session of the day, several →
   row error ("add a start time column"). Duration missing: the largest minutes of its rows (15–240), else
   90; `minutes_override` only when it differs from the duration; different durations of one session in the
   file → row error naming the rows. Empty status with RPE/minutes → `present`; all empty → row skipped with
   a note; RPE/minutes with a status other than `present` → warning, values dropped.
7. **Wide format** (≥ 3 headers are dates) is refused with a message pointing to the template; no
   conversion.
8. **Unknown players → create** needs a name (order switch as P16 decision 9) and the default position
   (P16 decision 8); a number alone is a row error. Undo deletes them when nothing else references them
   (P16 decision 3), after removing the attendance and appearances of the import.
9. **Metrics**: only metrics whose scope fits the type (`training` / `match`). "Create metric" opens the
   P17 dialog prefilled with the header and scope; the definition is written at once and stays after an
   undo of the import (without values). Hidden without `ManageMetrics`.
10. **Undo** removes created sessions/matches with all child rows. When that includes data added after the
    import (notes, metric values, ratings entered by hand), a confirmation names the counts first ("Undo
    also deletes 3 notes and 12 values added after the import. Continue?"); without such data no dialog.
    Opponents created by the import (listed in `report_json`; `opponent` has no `import_id`) are deleted
    when no match references them.
11. **BACKLOG**: `saveAttendance` and `replaceAppearances` keep `import_id` of the same player (fixed
    here); the `import_id` lost by undoing a calendar delete stays in the backlog.
12. **Scope extensions**: `src/viewmodels/ServiceRegistry.cpp`, `OpponentRepository`,
    `ImportLogRepository` (`report_json` fields), `ui/components/MetricDefinitionDialog.qml`,
    `src/viewmodels/ErrorPresenter.cpp`, `docs/BACKLOG.md`. Player matching of the activity imports
    is a separate `core::matchPlayer()` (`PlayerMatching`), not extracted from `SquadImport.cpp`,
    which stays as it was. No migration.
13. **Rating** is a whole number 1–10; "6,5" is a row error ("expected a whole number 1–10"), never
    rounded silently.
14. **Review (Codex)**:
    - *Undo question*: the sessions and matches the import created are stored as written in
      `report_json`; a note added later (`training_session.note`, the match report note) counts as a
      note, any other change of them (moved, another score) as an entry, before the undo deletes
      them.
    - *The plan of now*: every plan matches the players again against the squad (`matchPlayer()`,
      not the match of the validation; a changed row is marked "the player is another one") and
      reads the metric values again against the definitions of now (deleted, another scope, type
      or limits: a row error "check again"). The commit compares that plan with the one the user
      saw: a difference writes nothing and returns to the report. Two rows that become one player
      in a session or match are an error.
    - *Corrected metric values* keep their `import_id` (P17 decision 14); the undo counts them as
      changed after the import and removes them.
    - *Inconsistent group*: when the rows of a session or match disagree on a value (duration, type,
      venue, competition, score), every row of the group is an error, so nothing of it is written.
    - *Database file deleted* while the app runs (SPEC section 8, P16 decision 12): every commit
      and undo of an import checks that the file exists before the backup and before the
      transaction commits; otherwise `Io` (`database_missing`) and nothing is written.
