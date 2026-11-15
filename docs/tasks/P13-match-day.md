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
