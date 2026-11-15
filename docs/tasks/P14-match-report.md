# P14 — Match report: ratings, match RPE, note, corrections

**Branch:** `feature/P14-match-report`
**Depends on:** P13 merged
**Owner agent:** Claude (effort Medium); review: Codex

## Goal
After the whistle the coach rates every player 1–10, enters match RPE, writes a short note and can fix
mistakes made during the match; the player card shows each player's matches.

Read first: SPEC §4 (Match report), §6 (`appearance` rating and RPE), P13 Decisions.

## Scope — files you may create or change
- `src/data/migrations/0003_match_report.sql` (`match.report_note TEXT NULL`) and repository code
- `src/services/MatchReportService.hpp/.cpp` (+ `ServiceRegistry` lines)
- `src/viewmodels/MatchReportViewModel` and list models; `PlayerCardViewModel` / a new
  `PlayerMatchesModel` for the "Matches" tab
- `ui/views/MatchReportView.qml` (a section without a sidebar item, opened with
  `navigationArgs.matchId`), report components in `ui/components/`; the "Matches" tab of
  `ui/views/PlayerCardView.qml`
- `ui/views/CalendarView.qml` (only: a finished match opens the report instead of the read-only editor),
  `ui/views/MatchDayView.qml` (only: "End match" goes to the report)
- tests, CMake lists, `i18n/*.ts`, `qml_smoke`

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
