# P13b — Live tactics: formation changes during the match

**Branch:** `feature/P13b-live-tactics`
**Depends on:** P13 merged (P12 board components)
**Owner agent:** Claude (effort High); review: Codex

## Goal
During the match the coach sees the team on a tactical board (who plays where right now) and can
change the formation or move players between positions; every change is recorded with its minute, so
the report and later analysis know which shape the team played and when.

Read first: P12 Decisions (board, zones, `.tactic.json`), P13 Decisions (periods, minutes, events,
undo), SPEC §6.

## Scope — files you may create or change
- `src/data/migrations/0003_match_tactics.sql` (table `match_tactic_change`: `match_id`, `minute`,
  `added_time`, `formation`, `tactic_json`; the plan proposes the exact columns) and repository code
- `src/core/algorithms/MatchMinutes.*` (only: the shape on the pitch at a minute = saved lineup + subs
  + tactic changes), `src/core/domain/*` as needed
- `src/services/MatchDayService.*` (record/undo a tactic change in the same undo stack as events)
- `src/viewmodels/MatchDayViewModel.*`, a live board model (reuse `TacticsBoardModel` where possible)
- `ui/views/MatchDayView.qml` ("Tactics" view next to the tiles), reuse `TacticsPitch`, `PlayerToken`
- tests, CMake lists, `i18n/*.ts`, `qml_smoke`, `docs/SPEC.md` §6

## Requirements
1. **Live board**: the current XI in their current positions (lineup from P12, substitutes take the
   position of the player they replaced, sent-off players removed); cards and goals as small badges.
2. **Changes**: pick another formation (players stay on matching positions as in P12
   `changeFormation`), drag a player to another zone, swap two players. A change is applied with
   "Confirm" (minute field defaults to the current match minute, editable like events in P13), stored
   as one row, shown on the timeline ("Change of shape: 4-4-2 → 4-2-3-1"), undoable as the last action.
3. Substitutions keep working from the tiles and from the board (drag a bench player onto a token).
4. The board respects AGENTS §6 hit targets and the keyboard path from P12.
5. Read-only for roles without `RunMatchDay`.
6. P14 shows the shapes used with their minute ranges; that part belongs to P14.

## Tests
Core: shape at minute N across subs, a red card and two tactic changes. Service: record, undo,
atomicity, permission. `qml_smoke`: change the formation at 30', the timeline shows it, undo removes it.

## Done when
- [ ] During a match: 4-4-2 → 4-2-3-1 at 60', a substitute replaces a striker, the board shows the new
      shape; reopening the app shows the same.
- [ ] Section 3 passes.

## Decisions (accepted plan)
