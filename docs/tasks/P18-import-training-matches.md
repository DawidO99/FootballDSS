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
