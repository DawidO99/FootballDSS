# P19 — W2 housekeeping

**Branch:** `chore/P19-housekeeping`
**Depends on:** P11–P18 merged
**Owner agent:** Claude (effort High); review: Codex

## Goal
Close the open items of `docs/BACKLOG.md` that a coach will notice, plus small test and CI chores,
before `v0.3-W2`.

Read first: `docs/BACKLOG.md`, the Decisions of the packets that created each item (named below).

## Scope — files you may create or change
Whatever each item needs, plus `src/data/migrations/0005_housekeeping.sql` (the only migration of this
packet), tests, `i18n/*.ts`, `.github/workflows/ci.yml` (item 13), `docs/BACKLOG.md` (remove done
items), `AGENTS.md` §6 (item 12 only).

## Requirements
In this order. If time runs out, stop after a finished item and report what is left.

1. **Season validation** (P07b): years 1900–2100, second part = first + 1; core, setup wizard and team
   edit.
2. **Forced PIN change** (P07a/P07b): after a Coach resets someone's PIN, that user must set their own
   PIN at the next login (`staff_user.must_change_pin`, migration 0005).
3. **Optional primary position** (P16 manual test): a player may have no primary position ("—", not
   given); `player.primary_position` becomes nullable in migration 0005.
   - Squad: shows "—", filters (a player without a position matches no position group), sorting puts
     empty last; PlayerEditor allows "—".
   - Lineup validator (P12): OutOfPosition does not apply to a player without a position; the tooltip
     says the position is not given.
   - Slot picker (P12): a "Bez pozycji" group after "Inne pozycje".
   - Tactics board and Match Day (P12, P13b): work with such players.
   - Squad import (P16): the default position for rows without a position is "—" (instead of ŚP);
     the row warning stays ("pozycja nie podana — uzupełnij w Kadrze").
   - Demo data unchanged.
4. **Startup read error** (P07b L1): a read error at startup shows the error screen, not the wizard.
5. **Log file and single instance** (P05): log in AppData/logs (rotating, last 5 files); a second
   start shows "FootballDSS już działa" and exits (`QLockFile`).
6. **Expected return passed** (P06): the player card shows a hint for open injuries/illnesses whose
   expected return date has passed.
7. **Delete an availability entry** entered by mistake (P08): undo toast; the description is kept on
   undo, also when the deleting role cannot see it.
8. **Thresholds in Settings** (P11, P12): load thresholds and the yellow-card threshold editable by
   roles with `ManageSettings`; checked in C++.
9. **Squad position column** (P08): at most 3 secondary codes + "+N"; codes matching the active filter
   first; tooltip unchanged.
10. **Help on F1** with a visible "?" button in the top bar (AGENTS §6 shortcut rule): a short overview
    of the sections and their shortcuts.
11. **Opponent name correction** from the match editor (P09a); uniqueness as today.
12. **Remove the spike charts view** (ADR-006): `SpikeChartsView`, Ctrl+Shift+G and its references;
    update the shortcut exception in AGENTS §6.
13. **Tests and CI**:
    - `qml_smoke`: CTest TIMEOUT 600 s; per-scenario progress output (start/done with ms).
    - The chart hover test (P13 report) independent of the real cursor position.
    - Manual CI job (`workflow_dispatch`) with GC stress (`QV4_GC_TIMELIMIT=1`) and a switch that
      disables `useNonIncrementalGarbageCollector()` in the test programs (P12).

Items that stay in `BACKLOG.md`: import counters for goals/cards, cards at half-time, own goals,
cards for the bench, substitution limit, editing events, a tactic change before a later one, the
card threshold per competition, import history, PIN recovery code, column chooser, player join date,
the coach hierarchy question.

## Tests
One test per item where it has logic (1, 2, 3, 4, 5, 6, 7, 8, 9, 11). Migration 0005: upgrade test from
schema 4 with existing data (positions kept, `must_change_pin` false). CI items verified by the run.

## Done when
- [ ] Every finished item checked by hand once; `BACKLOG.md` updated.
- [ ] Section 3 passes; the GC stress job runs once by hand.

## Decisions (accepted plan)
