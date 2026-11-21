# P14 — Match report: ratings, match RPE, note, corrections

**Branch:** `feature/P14-match-report`
**Depends on:** P13b merged
**Owner agent:** Claude (effort Medium/High); review: Codex

## Goal
After the whistle the coach rates every player 1–10, enters match RPE, writes a short note and can fix
mistakes made during the match; the player card shows each player's matches.

Read first: SPEC §4 (Match report), §6 (`appearance` rating and RPE), P13 Decisions.

## Scope — files you may create or change
- `src/data/migrations/0004_match_report.sql` (`match.report_note TEXT NULL`) and repository code
- `src/services/MatchReportService.hpp/.cpp` (+ `ServiceRegistry` lines)
- `src/viewmodels/MatchReportViewModel` and list models; `PlayerCardViewModel` / a new
  `PlayerMatchesModel` for the "Matches" tab
- `ui/views/MatchReportView.qml` (a section without a sidebar item, opened with
  `navigationArgs.matchId`), report components in `ui/components/`; the "Matches" tab of
  `ui/views/PlayerCardView.qml`
- `ui/views/CalendarView.qml` (only: a finished match opens the report instead of the read-only editor),
  `ui/views/MatchDayView.qml` (only: "End match" goes to the report)
- tests, CMake lists, `i18n/*.ts`, `qml_smoke`

## Added after the P13 manual test (accepted by the human)
- **Finished match details**: opening a finished match from the Calendar shows the report with all
  statistics (score, timeline incl. notes and shape changes, per-player minutes, goals, assists, cards,
  shapes used with minute ranges).
- **Post-match entry**: P13 added a clockless Match Day mode (minutes typed by hand, default for a
  match dated before today). P14 only adds the entry point: a planned match in the past opens Match Day
  in clockless mode from the Calendar ("Uzupełnij po meczu"); no second implementation of events or
  minutes. Coaches often have no time on the bench.
- **Corrections** (requirement 4) cover every event type, notes and shape changes.

## Out of scope
PDF export (W3), opponent profile (W3), statistics charts.

## Requirements
1. **Header**: score, opponent, date, competition, event timeline (minute, icon + word, players).
2. **Ratings grid**: every player who played (minutes from P13): minutes (read-only), rating 1–10, RPE
   0–10; keyboard like the training grid (digits, `1`+`0` = 10, Tab/Enter, Esc, Ctrl+S with a visible
   Save button; reuse P09b components where they fit). Leaving with changes asks.
3. **Note**: multi-line text (`TextAreaField`), plain text.
4. **Corrections**: add an event with a minute, delete an event; minutes and score are recomputed by
   `MatchMinutes` (P13). Deleting an event asks first only if it removes a goal (AGENTS §6 rule on data).
5. **Player card "Matches" tab**: date, opponent, result, minutes, goals, assists, cards, rating, RPE;
   newest first; opens the report.
6. Match load (P11) now includes the entered RPE without further work; check it in a test.
7. Permission `EditLineup` for ratings, note and corrections; read-only for other roles.

## Tests
Service: ratings and RPE ranges, correction recomputes minutes and score atomically, permission per
role. Viewmodel: grid keyboard path, dirty tracking. Load: a match with RPE changes the player's acute
load.

## Done when
- [ ] Finish a match in Match Day → report opens → 14 ratings by keyboard → saved → player card
      "Matches" shows them.
- [ ] Add a forgotten substitution afterwards: minutes change accordingly.
- [ ] Section 3 passes.

## Notes for the thesis
Screenshot of the report; the correction flow shows robustness against user error.

## Decisions (accepted plan)
1. **Scope extensions** (accepted): `src/services/MatchRecompute.hpp/.cpp` (shared with Match Day: the
   recomputation of the appearances that keeps ratings and RPE, and the rows of one event action after a
   full replay) and `src/services/MatchDayService.*` (only switched to it, no change of behaviour);
   `src/viewmodels/MatchTimelineRows.hpp/.cpp` (the timeline rows of Match Day and the report) and
   `src/viewmodels/MatchDayViewModel.cpp` (only switched to it); `src/viewmodels/NavigationModel.*`,
   `ui/components/Sidebar.qml`, `ui/Main.qml` (the section `matchreport` without a sidebar item, with the
   leave guard); `src/viewmodels/ErrorPresenter.cpp`; `ui/components/MatchTimeline.qml` (deleting events
   and shape changes, off by default); `ui/components/MatchEditorDialog.qml` (only the label "Fill in
   after the match" for a planned match before today); `ui/views/ComponentGalleryView.qml`;
   `docs/SPEC.md` §6; `docs/BACKLOG.md`.
2. **Matches without Match Day** (demo data P10, import P18: no `match_live` or no saved lineup): the
   sheet is rebuilt from `appearance` (starters: `minute_on` 0, with their position; bench:
   the players who came on, then the rest of the active squad, so a forgotten substitution of a player
   who did not play can be added), the final minute is the largest `minute_off`, or 2 × 45 without one.
   When the stored events replay on that sheet and give the stored minutes and score, corrections work as for a match of Match Day. Only with
   inconsistent data the report has no corrections and says why; ratings, RPE and notes stay editable.
3. **seq of corrections**: events and notes added in the report have `seq` NULL (rows not written by
   Match Day), so undo in Match Day still takes back the end of the match. A row restored from the undo
   toast keeps its original `seq`.
4. **Shape changes** are only deleted in the report (with undo); adding one after the match → BACKLOG.
   Notes: add, edit and delete.
5. **Dependent events**: deleting an event that a later one needs (the substitution of a later scorer,
   the first yellow of a later second yellow) is refused (`Conflict`, `event_dependent`), naming the later
   event. A second yellow and its red go together. Editing an event = delete + add.
6. **Final minute** stays as stored (`core::finalMinute()`, also `ended_early_minute`); an event after it
   is `after_final_minute`. A red card added afterwards that leaves fewer than 7 does not end the match
   early.
7. **Report note**: `match.report_note`, 1–2000 characters after trimming (`CHECK` in 0004), empty = NULL.
8. **One Save** (Ctrl+S) for ratings, RPE and note: one dirty state, one leave guard. Corrections are
   saved at once (as in Match Day) and keep the unsaved ratings.
9. **Post-match entry**: only the label of the existing "Open Match Day" button for a planned match before
   today ("Fill in after the match"); Match Day already defaults to the mode without the clock there.
10. **Undo of the end from the toast** after the report opened: the report shows that the match is live
    again, with "Back to Match Day" instead of the grid.
11. **Rating** 1–10: a lone `0` in the rating column is refused (row error), `1` + `0` = 10. Rating and RPE
    are optional.
12. **Statistics**: assists = goals with the player as `related_player_id` plus `assist` rows (imports);
    cards = the number of `yellow` and `red` rows (a second yellow = 2 yellow + 1 red). The shapes of the
    report are split only by shape changes (names as `core::shapeChangeNames()`).
13. **Verification**: section 3 with `ctest --preset dev-local` and `ctest --preset dev-local-ci`.
14. **Found while implementing** (scope extensions, no change of behaviour elsewhere):
    `src/viewmodels/MatchEditorViewModel.*` (`beforeToday`, for the label of decision 9: QML has no
    "today"), `src/viewmodels/MatchDayModels.*` (role `changeId` of the timeline, 0 in Match Day),
    `tests/qml_components/tst_matchratingsgrid.qml`. Whether deleting an event changes the score is read
    by the viewmodel from the event type (goal, opponent goal), not by a service call.
15. **Review (Codex)**:
    - *Undo of a deleted substitution*: the delete keeps the rating and RPE of the appearances it takes
      (`DeletedEvents`); Undo writes them back with the events in one transaction. Unsaved ratings of a
      player a correction takes away stay in the viewmodel and come back with him; dirty counts them.
    - *Correction dialog*: the players offered are computed at every opening, at every change of a
      field and whenever the report is read again (`MatchCorrectionDialog.refresh()`), not by bindings.
    - *Undo and another report*: the undo state carries its match; Undo restores there, and the report
      shown reads again only when it is that match.
    - *Try again*: the viewmodel keeps the match asked for when reading it fails; "Try again" reads it
      again and the unsaved ratings stay.
    - *Starter* = `minute_on` 0, in the rebuilt sheet and in the check of the stored minutes (`started`
      is not compared).
    - *No guessed format*: a match without Match Day takes a minute 0 to its final minute, without added
      time (`MatchReport::format` is empty).
    - *Gallery exception*: `MatchCorrectionDialog` needs `MatchReportViewModel` and so the services;
      the gallery also runs in `qml_components` without them, so the dialog is not in the gallery. It is
      covered by `qml_smoke` on the real report.
