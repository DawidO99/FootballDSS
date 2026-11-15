# P19 — W2 housekeeping

**Branch:** `chore/P19-housekeeping`
**Depends on:** P11–P18 merged
**Owner agent:** Claude (effort Medium); review: Codex

## Goal
Close the open items of `docs/BACKLOG.md` that matter for coaches and for a stable release before
`v0.3-W2`.

Read first: `docs/BACKLOG.md`, Decisions of the packets that created each item.

## Scope — files you may create or change
Whatever each item needs, plus `src/data/migrations/0004_housekeeping.sql` (the only migration of this
packet), tests, `i18n/*.ts`, `.github/workflows/ci.yml` (items 9–10), `docs/BACKLOG.md` (remove done
items).

## Requirements (in this order; if time runs out, stop after a finished item and report)
1. Season validation: years 1900–2100, second part = first + 1 (core + wizard + team edit).
2. Forced PIN change: after a Coach resets someone's PIN, that user must set their own PIN at the next
   login (`staff_user.must_change_pin`, migration 0004).
3. Startup read error shows the error screen, not the wizard.
4. Log file in AppData/logs (rotating, last 5 files) and a single-instance lock (`QLockFile`): a second
   start shows "FootballDSS is already running".
5. Player card: "expected return date has passed" hint for open injuries/illnesses.
6. Delete an availability entry entered by mistake (with undo toast; description kept on undo).
7. Load thresholds and the yellow-card threshold editable in Settings (Coach only).
8. Squad position column: at most 3 secondary codes + "+N", codes matching the active filter first.
9. CI: compiler cache (`sccache`) for Windows and Linux builds.
10. `qml_smoke`: CTest timeout 600 s and per-scenario progress output.
11. Help overview on F1 with a visible "?" button in the top bar (AGENTS §6 shortcut rule).
12. Opponent name correction from the match editor (repository `update` exists).

Items left in `BACKLOG.md` after this packet: recovery code, column chooser, player join date, RPE in
future sessions, status segment shift, coach hierarchy question — unless the plan argues one of them in.

## Tests
One test per item where it has logic (1, 2, 4, 5, 6, 7, 8); CI items verified by the run times in the
report.

## Done when
- [ ] Every finished item checked by hand once; `BACKLOG.md` updated.
- [ ] Section 3 passes; CI time on Windows reported before/after `sccache`.

## Decisions (accepted plan)
