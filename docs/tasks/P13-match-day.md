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
   `ui/components/MatchEditorDialog.qml` (the Calendar entry), `ui/components/AppDialog.qml` (`touch`
   variant, review), `src/viewmodels/MatchDayTexts.hpp/.cpp` (the `tr()` texts of Match Day: minutes,
   clock, phases, events, player states), `docs/BACKLOG.md`, `src/data/CMakeLists.txt`,
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
16. **Review (Codex)**:
    - *Undo of the end with another live match*: refused (`Conflict`, `undo_end_another_live`); the
      viewmodel names the other match ("First finish it, or undo its start, in the match against …").
      Taking the other match back to planned makes the undo possible again.
    - *Undo tooltip*: a `PlainToolTip` (it names players and the opponent).
    - *Inconsistent history*: a match with `match_live` whose stored events do not fit its lineup is an
      explicit error (`Database`, `match_history`, shown as the load error), never its lineup at 0:0;
      also for every action on it. Only matches without `match_live` (data from outside Match Day) show
      the lineup and the stored score.
    - *Second yellow*: recognised by counting the yellow cards of the player before a `red` row without
      a related player, in replay order (`core::secondYellowReds`), not by neighbouring rows or minutes;
      a yellow and a straight red at the same minute is a red card. Its undo removes the red row and
      that second yellow by id (`MatchRepository::deleteEvent`).
    - *Dialogs*: `AppDialog.touch` gives the header and footer buttons `Theme.hitTouch`; the start dialog
      and the leave question use it, and qml_smoke measures them.
    - *Atomicity*: tests force an SQL failure (temporary triggers) after the event is written, between a
      second yellow and its red, at the end of the match and while undoing; the database stays as before
      the action. Restart: a file database closed and opened again with new services resumes the clock.
    - *Yellow card on a tile*: icon and the words "Yellow card", also in the accessible name.
    - *Shortcuts* G/O/Y/R/S/Ctrl+Z (and F11, Esc) are off while any dialog or popup holds the focus and
      while a text field does; qml_smoke checks both.
17. **Manual test of the human (before merge; 0002 is not on `main`, so it was changed in place).** Where
    it differs, this replaces decisions 2, 3, 6, 7 and 8.
    - *A. Minute of every action*: goal, opponent goal, cards, substitution and note have a "Minute" field
      (`23`, `45+2`), the current minute until edited (in a break: the last minute played, for a
      substitution the start of the next part). Any minute already played can be entered, not the future;
      a late event goes through a full replay. When a later stored event no longer fits (a red card for a
      player who scored later; a yellow that would make a later card a second one without its red,
      `SecondYellowWithoutRed`), the action is refused, naming that event
      (`MatchDayService::checkEvent`). The order of the match puts the added time of a part before the
      start of the next part (also `ORDER BY` of `listEvents`). The opponent goal is a pending action with
      a minute and Save.
    - *Undo of the action entered last*: instead of `after_event_id` (decision 2) every action has a `seq`
      of the match on `match_event`, `match_note` and `match_clock_step`, and `match_live.finished_seq`
      for the finish; undo removes everything with the highest `seq`. The rows of a second yellow share
      it. This deviates from the column list of the manual test because notes must join the same stack as
      events and clock steps.
    - *B. Match format*: 1–4 parts × 10–60 minutes, at most 130 minutes in all (the `CHECK` of the minutes
      of 0001), presets 2×45, 2×40, 2×35, 2×30, 2×25, 3×30, 4×20 and a custom one; the default by category
      as before (2 × the old half length). `match_live(periods, period_length)`,
      `match_clock_step(period, kind start|end, at, seq)`. Minutes are cut at the end of the current part,
      `added_time` over it ("30+2'", "60+1'"); a whole match is parts × length. Texts: "1st/2nd half" with
      2 parts, otherwise "1st/2nd/3rd/4th period"; the end of the last part is "End match".
    - *C. Minimum of players*: `core::kMinPlayersOnPitch = 7`; "On the pitch: N" under the score (with a
      warning icon at 7 or fewer). An action that leaves 7 shows a warning toast; fewer than 7 opens
      "By the laws of the game the match cannot go on… End the match?" and a banner with End match. Until
      then events and clock steps (except the end of the last part) are refused (`too_few_players`); notes
      and undo stay. `MatchDayService::endMatch()` ends the running part and finishes the match with
      `match_live.ended_early = 1`: its final minute is the minute of the end (in a break, the end of the
      part played), not parts × length.
    - *D. Notes*: `match_note` (minute, `added_time`, optional player, text 1–500, plain text, `seq`),
      `MatchNoteRepository`. Button "Note" (N) with a dialog (minute, player, text); on the timeline with
      an icon and the word "Note", with Edit and Delete. Adding a note is in the undo stack; editing keeps
      its place; deleting has a 10 s undo toast that restores it with its `seq`. Notes never change the
      score or the minutes.
    - *Scope extensions*: `src/core/domain/MatchNote.hpp`, `Ids.hpp` (`MatchNoteId`),
      `src/data/repositories/MatchNoteRepository.*`, `ui/components/MatchNoteDialog.qml`.
18. **Re-review (Codex, 06a0c13)**:
    - *seq never used twice*: `match_live.next_seq`, taken and increased by every action (also one undone
      later or a deleted note), instead of max(seq) + 1. A note restored from its toast keeps its old seq,
      so undo after "note → delete → opponent goal → restore" takes back the goal only, and says so.
    - *One way to end*: `MatchDayService::endNow()` for the clock button "End match", the banner and the
      dialog. With fewer than 7 players the end is early and `ended_early_minute` holds its minute
      (replaces the flag `ended_early`); with 7 or more a normal end (parts × length), and `endMatch()`
      then only while the last part runs (`match_not_over`).
    - *Players at the minute*: the tiles of a pending action are selectable by the state of the match
      at its minute (`core::situationAt`, replay up to it) for the scorer, the assistant, the cards and
      both players of a substitution; editing the minute recomputes them. A tile chosen by where the
      player was then shows it ("At 11': on the pitch").
    - *Final minute*: never before a stored event; an early end in a break is the end of the part played
      (the minute of a substitution in it), not the clock at the end of that part
      (`core::earlyEndMinute`).
19. **Manual test of the human (basics, before merge; 0002 changed in place)**:
    - *Start dialog*: the format fields (parts, minutes per part) stand one under the other with their
      labels, so they fit at the minimum window in Polish and English (qml_smoke measures both); the
      error of the sum ("at most 130 minutes in all") shows only once the values are finished: when
      neither field has the focus, or at Start. Single-line fields of a dialog leave Enter to
      `AppDialog` (an own `onAccepted` saved twice).
    - *Set time*: a visible button by the clock (`Theme.hitTouch`) opens a dialog for the time as the
      clock shows it (`23:41`, `67:30` in the second half, `45+2:10` in stoppage time; the field holds the
      current clock). The start of the running part moves so the clock shows that time and runs on
      (`MatchDayService::setClock`), also right after "Start 1st half" pressed late. Limits: at most
      the length of the part + 30 min (`clock_time`), never before the minute of an event of that part
      (`clock_before_event`). Not in the undo stack: the toast has Undo (`restoreClock`, the start before).
    - *Without the clock*: `match_live.clockless`; the start dialog chooses "With the clock" (default) or
      "Without the clock" (default for a match before today). No stopwatch, no clock steps, no "Set
      time"; every action needs its minute (`minute_required`), any minute of the format
      (`minute_out_of_match` outside it; added time 1–30, `core::kMaxAddedTime`), in any order as long as
      each event fits those already entered (a goal of a substitute needs his substitution first: the
      full replay refuses it otherwise, naming the conflict). "End match" is always there: a normal end
      with 7 players or more, otherwise an early end at the last event or the minute typed in the dialog
      (if later). "Back to planned" while nothing is entered. Resume, undo, notes, checks and the minutes
      of play as with the clock.
    - *P14*: "entering a match after it was played" is Match Day without the clock, opened from the
      Calendar (the match dialog has "Open Match Day"; for a match before today the start offers the mode
      without the clock). P14 adds ratings, RPE and the note of the report on top.

