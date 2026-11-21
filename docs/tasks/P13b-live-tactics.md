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
1. **Slots keep their identity** for the whole match: the 11 slots of the saved lineup (P12) keep
   their index. A substitute takes the slot of the player going off (with its current position and
   place); a sent-off player leaves the slot empty (not drawn on the board). A tactic change stores
   only the places of the 11 slots (position, x, y): `tactic_json` is `.tactic.json` v1
   (`data/readers/TacticJson`) with every `playerId` null and no bench. Swapping two players exchanges
   the places of their slots; a drag to another zone is `moveSlot` (P12); a formation gives the slots
   new positions. Events entered late never conflict with stored tactic changes, and a substitution and
   a tactic change at the same minute commute.
2. **Migration 0003**: `match_tactic_change` (`id`, `match_id` → `match` CASCADE, `minute` 0–130,
   `added_time` ≥ 1 *null*, `formation` TEXT 1–20, `tactic_json` TEXT, `seq` NOT NULL ≥ 1,
   UNIQUE (`match_id`, `seq`)), STRICT, index on `match_id`. `formation` is the name counted from the
   occupied slots when the change was recorded (no "(custom)" suffix); the shape shown (timeline, P14)
   is always counted again by `core::shapeAt()`, since a red card entered later may change it.
   `match.formation` keeps the starting formation.
3. **Core**: `core::shapeAt()` (the shape at a minute: saved lineup + substitutions + red cards +
   tactic changes) and `core::shapeChangeNames()` in `MatchMinutes`; `changeShape()`, `swapPlaces()`,
   `moveLiveSlot()` and `liveFormationName()` in `Formations` (scope extension). The P12
   `changeFormation` sends players without a matching position to the bench, which a live match cannot
   do: `changeShape()` keeps everyone on the pitch. With fewer than eleven the empty slots take the
   positions of the formation left over: the goalkeeper when nobody is in goal, then the ones furthest
   forward (ten players in 4-4-2 play 4-4-1). The other positions first take the occupied slots of the
   same position, left to right (as P12); the remaining occupied slots take the free positions from the
   own goal up, left to right, the GK position only when nothing else is left. An empty slot never
   blocks a position (the slot of a sent-off goalkeeper). `liveFormationName()` counts the occupied slots
   only ("4-4-1" with ten); a catalogue name only with eleven.
4. **`appearance.position`** (scope extension of `MatchMinutes`): a substitute gets the position of his
   slot at the minute he came on, tactic changes included; starters keep the lineup position. Recording
   and undoing a tactic change recompute the appearances in the same transaction.
5. **Service**: `MatchDayService::recordTacticChange(match, shape, time)` is one action of the undo stack
   (next `seq`); time as for a substitution (in a break: the minute that starts the next part; without
   the clock a minute is required). Refused: not live [`match_not_live`], before the kick-off
   [`match_not_running`], fewer than 7 players [`too_few_players`], at a time before a stored tactic
   change [`tactic_change_later`, Conflict; the message names that change; inserting a change before a
   later one goes to `docs/BACKLOG.md`], a shape that is not 11 slots with coordinates 0–1 and at most one
   goalkeeper on the pitch [`tactic_shape`], the same places as the shape at that time
   [`tactic_unchanged`]. `undoLast()` takes it back (`MatchDayAction::Kind::TacticChange`).
6. **Draft and Confirm**: the first edit of the board starts the pending action `tactic` (the P13 minute
   field, current minute by default, editable; Confirm/Enter saves, Cancel drops the draft). The draft
   is dropped when the view is left, by undo and by any other action; when another action drops it a
   toast says "The unconfirmed change of shape was discarded". The leave guard text stays.
7. **Substitution from the board**: dropping a bench player on a token starts the same pending
   substitution as the tiles of P13 with both players chosen (minute field, current by default;
   Enter/Save), never a write at once. While a shape draft is pending the drop is refused with a message.
8. **No goalkeeper**: moving the goalkeeper out of the GK zone without a new one is allowed, but the
   board shows a warning "No goalkeeper on the pitch" (icon + text), also after a red card for the
   goalkeeper, until a player stands in the GK zone. No block.
9. **Screen**: a `SegmentedControl` "Players / Tactics" in the title row switches the area of the tiles;
   the timeline stays. The Tactics view: `TacticsPitch` (empty slots hidden: `showEmptySlots`; tokens
   laid out in the height left by a token: `fitTokens`, so a low pitch never pushes the goalkeeper onto
   the defence), the formation field, the position field and Swap of the selected slot (keyboard path
   of P12), the bench as drag sources (`MatchPlayerTile.dragEnabled`), small badges for cards and goals
   on the tokens (icon + number, words in the tooltip and accessible name). In Tactics the pending
   action (prompt, minute, Confirm/Save, Cancel) is in the column next to the board instead of the bar
   above it, so the board keeps its size at 1280×720; a token tapped while an action waits for a player
   on the pitch (goal, card, player going off) chooses that player.
   Read-only without `RunMatchDay`, before the kick-off and after the end. Timeline: "Change of shape"
   with "4-4-2 → 4-2-3-1" (same name: "4-4-2: positions changed").
10. **Scope extensions** (accepted): `src/core/algorithms/Formations.*`, `src/core/domain/Ids.hpp`,
    `src/core/domain/MatchTacticChange.hpp`, `src/data/repositories/MatchTacticRepository.*`,
    `src/viewmodels/ServiceRegistry.*`, `src/viewmodels/PreMatchViewModel.cpp` (new board roles),
    `LineupModels.*`, `MatchDayModels.*`, `ErrorPresenter.cpp`, `MatchDayTexts.*`, `LineupTexts.*`,
    `ui/components/TacticsPitch.qml`, `PlayerToken.qml`, `MatchPlayerTile.qml`, `Icon.qml`,
    `ui/theme/Theme.qml`, `ui/views/ComponentGalleryView.qml`, `tests/qml_components/tst_tacticspitch.qml`,
    the schema-version tests, `src/data/CMakeLists.txt`, `docs/BACKLOG.md`.
11. **Review (Codex)**:
    - *Position of a substitute*: the place of his slot right after he came on, before the later events
      of the same minute (a red card or his own substitution in that minute no longer leaves the
      position before the tactic change).
    - *The latest stored action*: `core::latestRecordedTime()` over events, notes and tactic changes
      (with added time). The final minute of an early end (`core::earlyEndMinute`), "set time" and its
      undo (`core::minElapsedFor`, refused with `clock_before_event`) and the end offered without the
      clock use it. Choice for Codex's scenario (without the clock: a change at 60', red cards entered
      late at 20' leave six): the end is at 60', never before a stored action, as P13 for events.
    - *Swap on the board*: leaving Match Day and every switch of Players / Tactics end a swap started
      with the swap button.
    - *Undo of the toasts*: restoring a deleted note and the undo of "set time" drop a draft with the
      same notice as the other actions.
    - *Messages*: `tactic_shape` and `tactic_unchanged` are InvalidInput texts of `ErrorPresenter`.
    - *Minimum window*: the board spreads the rows of the shape evenly over its height (`pitchY` of the
      live board) and a name is never wider than the gap to the next token of its row (`labelRoom`,
      `TacticsPitch.fitTokens`); qml_smoke measures that the circles and names of the tokens do not
      intersect in 4-4-2 and 3-5-2 at 1280×720.
12. **CI-only Match Day failures** (fix): the smoke compared the Confirm button with the English text,
    but the language the earlier scenarios leave follows the system one (English on a Polish desktop,
    Polish on CI), and it measured the pitch before an asynchronous xcb resize had landed; the early
    return then left the view in Tactics with a draft, so every later step failed. Not an application
    bug. Texts are compared with their translation, resizes are awaited (`resizeAndWait`), and the
    Match Day scenario is split into independent steps (start, events, tactics, break and leave,
    the whole match, read-only), each with its own match of today prepared through the services and a
    clean view (dialogs closed, tiles, no pending action). `qml_smoke --without-focus` runs Match Day
    while another window holds the focus; keys activate the window first, as a user's click would.
    `dev-local-ci` runs in the C locale as CI does, and AGENTS §3 asks for it from P13b on.
