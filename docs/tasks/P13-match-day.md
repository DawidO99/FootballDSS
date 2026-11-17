# P13 — Match Day: live clock, events, automatic minutes

**Branch:** `feature/P13-match-day`
**Depends on:** P12 merged (starts from the saved lineup)
**Owner agent:** Claude (effort High); review: Codex

## Goal
During the match one person on the bench records everything with large buttons: clock, goals,
assists, cards, substitutions. Minutes for every player come out automatically, nothing is lost if the
laptop sleeps or the app closes, and the last action can be undone.

Read first: SPEC §3 item 6, §4 (Match Day: fullscreen, large buttons, undo), §6 (`match`,
`appearance`, `match_event`), P10 Decisions (event semantics), AGENTS §6 (hit targets 44 px).

## Scope — files you may create or change
- `src/data/migrations/0002_match_live.sql` (live state: half, half length, start timestamps of each
  half; the plan proposes the exact table) and its repository code
- `src/core/algorithms/MatchMinutes.hpp/.cpp` (pure): lineup + events → appearances (minutes) and score
- `src/services/MatchDayService.hpp/.cpp` (+ `ServiceRegistry` lines)
- `src/viewmodels/MatchDayViewModel` and list models
- `ui/views/MatchDayView.qml`, Match Day components in `ui/components/`
- `ui/views/CalendarView.qml` / `PreMatchView.qml` (only: "Start Match Day" entry points)
- `docs/SPEC.md` §6 (only: event semantics, see requirement 4)
- tests, CMake lists, `i18n/*.ts`, gallery, `qml_smoke`

## Out of scope
Ratings, match RPE, report note and post-match corrections (P14); statistics beyond the score.

## Requirements
1. **Start**: from a planned match with a saved lineup without errors (P12). Without a lineup or with
   errors: a message and a button to Pre-match. Start asks for the half length (default by team
   category: senior 45, U19 45, U17 40, U15 35, U13 30, U11 25, other 45; editable).
2. **Clock**: start 1st half, end 1st half, start 2nd half, end match. Shows `mm:ss` and stoppage time
   as `45+2`. Stored event minute = elapsed match minute (0–130, SPEC). The clock is derived from
   persisted start timestamps (injectable `Clock`), so closing and reopening the app resumes the
   running match correctly; the app opens Match Day on a live match after login.
3. **Events** (each saved immediately, one transaction per action): goal (scorer, optional assist),
   opponent goal, yellow, second yellow → automatic red, red, substitution (player in, player out).
   Status `live` while running, `finished` at the end with `goals_for`/`goals_against` set.
4. **Semantics** (write them into SPEC §6 under `match_event`): goal: `player_id` = scorer,
   `related_player_id` = assistant (no separate `assist` rows from Match Day); sub: `player_id` = player
   coming on, `related_player_id` = player going off; red after a second yellow is a `red` row with
   `related_player_id` empty.
5. **Minutes**: appearances are recomputed from lineup + events by `MatchMinutes` after every action and
   at the end (single source of truth: events). Starters from 0, substitutes from their minute, sent-off
   players until the card, everyone on the pitch until the final minute. A substituted player cannot come
   back; a sent-off player cannot be substituted.
6. **Undo last action** (button, always visible): removes the last event (or the last clock step) and
   recomputes. Confirmation is not needed; the undone action is named in a toast.
7. **Screen**: fullscreen toggle, score and clock on top, players on the pitch and bench as large tiles
   (≥ `Theme.hitTouch`), event timeline. Every action reachable by mouse/touch; keyboard shortcuts only
   as accelerators (AGENTS §6). Leaving the section or logging out during a live match asks (leave guard).
8. Permission `RunMatchDay`; other roles see the live match read-only.

## Tests
Core: `MatchMinutes` for starters, subs, sub of a sub, red card, second yellow, stoppage time, extra
events after the end rejected, invalid sub (player already off). Service: each action persists and
recomputes atomically; undo; resume after "restart" with a `FakeClock` (time passes while closed);
permission per role. `qml_smoke`: a full short match by simulated clicks.

## Done when
- [ ] Demo data → planned match → lineup → Match Day: 2 goals, a sub, a yellow, a second yellow; end;
      minutes in the database match a hand calculation.
- [ ] Close the app at minute 20, reopen 5 minutes later: the clock shows ~25:00.
- [ ] Section 3 passes; sanitizer CI on `main` after merge.

## Notes for the thesis
Record a short GIF of Match Day. Describe "events as the single source of truth" for minutes.

## Decisions (accepted plan)
1. **Scope extensions** (accepted): `ui/Main.qml` (leave guard of Match Day; after a login a live match
   opens Match Day), `src/data/repositories/MatchRepository.*` (`setStatus`, `findLive`, `added_time`),
   `src/data/repositories/MatchLiveRepository.*`, `src/viewmodels/ErrorPresenter.cpp`,
   `ui/components/Icon.qml`, `ui/theme/Theme.qml`, `ui/views/ComponentGalleryView.qml`,
   `ui/components/MatchEditorDialog.qml` (the Calendar entry), `docs/BACKLOG.md`, `src/data/CMakeLists.txt`,
   `src/core/domain/Enums.*` and `MatchEvent.hpp` (`ClockStep`, `addedTime`), the schema-version tests
   (`MigrationsTest`, `SchemaTest`, `DatabaseManagerTest`, `BackupTest`, `EnumsSchemaTest`).
2. **Migration 0002**: `match_live` (`id`, `match_id` UNIQUE → `match` CASCADE, `half_length_min` 10–60),
   `match_clock_step` (`match_id` → `match_live.match_id` CASCADE, `step`, `at` UTC ISO datetime,
   `after_event_id` without a foreign key, UNIQUE (`match_id`, `step`)) and
   `ALTER TABLE match_event ADD COLUMN added_time INTEGER CHECK (added_time >= 1)`. `after_event_id` tells
   undo whether an event or a clock step came last (`match_event` has no time column).
3. **Event minute** (changed against the plan): `minute` = full minutes elapsed, cut at the end of the
   half: the first half at most `halfLength`, the second at most 2 × `halfLength` (no other cut at 130).
   `added_time` holds the minutes over the end of the half, for display only. A whole match is
   2 × `halfLength` minutes of play whatever the stoppage time; that is the final minute of `appearance`.
4. **Display**: stored 0-based; shown the football way + 1: 00:30 → 1', 44:20 → 45', 45:30 in stoppage
   time → 45+1', 47:10 → 45+3', in the second half 90+N'. Core tests cover these limits. The on-screen
   clock is `mm:ss` and `45+2:10` in stoppage time.
5. **Minutes while live**: players still on the pitch have `minute_off` empty until the end (P11 counts no
   load from a live match); the tiles show the minutes played so far, counted by the viewmodel.
6. **Two steps to start**: "Start Match Day" (dialog with the half length: senior 45, U19 45, U17 40,
   U15 35, U13 30, U11 25, other 45; status `live`, clock at 00:00), then "Start 1st half" at the
   kick-off. Undo goes back as far as before the kick-off; "Back to planned" (live → planned) only before
   the kick-off and without events.
7. **Half-time break**: only substitutions (at `halfLength`); goals and cards only while a half runs.
   Cards in the break, own goals, cards for the bench and a limit of substitutions go to `docs/BACKLOG.md`.
8. **Undo of a second yellow** removes both rows (`yellow` + automatic `red`) as one action.
9. **Undo of the end** makes the match `live` again and clears `goals_for`/`goals_against`; the clock is
   again counted from the start of the second half.
10. **One live match per team**: starting a second one is `Conflict` (`another_live_match`).
11. **Calendar entry**: the match dialog of the Calendar has "Open Match Day" for a planned or live match
    ("Save and open Match Day" when the form is editable); Pre-match has "Open Match Day" next to Save
    once the lineup is saved.
12. **Full screen** toggles the window (F11); leaving the section or logging out restores it.
13. **Leave guard**: a live match while Match Day is shown asks "Leave the live match?" (the clock keeps
    running); the same question when the window is closed. Only one page is guarded in `Main.qml`, so the
    question comes only from the Match Day section (BACKLOG).
14. **Start and the validator**: the rules that are errors do not depend on the load (HighLoad is a
    warning), so the start checks the lineup synchronously without `LoadService`; players no longer in
    the squad count as errors.
15. **Read-only roles** see the live match; the view reads it again when shown. Recomputing keeps the
    rating and RPE of `appearance` (P14); a substitute takes the position of the player going off.
