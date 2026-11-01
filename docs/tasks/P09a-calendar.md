# P09a — Calendar, opponents, match and session editors

**Branch:** `feature/P09a-calendar`
**Depends on:** P07a and P06b merged (parallel with P07b, P08, P10)
**Owner agent:** Claude (effort Medium)

## Goal
The coach plans matches and training sessions and sees them in a list and a week view.

## Scope — files you may create or change
- `src/services/CalendarService.hpp/.cpp` (+ one member and one construction line in
  `ServiceRegistry` — expected trivial conflicts with P08/P09b/P10)
- `src/viewmodels/CalendarViewModel`, `MatchEditorViewModel`, `SessionEditorViewModel`,
  `OpponentListModel`
- `ui/views/CalendarView.qml`, editors as components in `ui/components/` if reusable
- `tests/services/*`, `tests/viewmodels/*`, CMake lists, `i18n/*.ts`

## Out of scope
The training grid (P09b), Match Day, squad, dashboard.

## Requirements
1. **Calendar**: list mode (upcoming first, past below) and week view (Mon–Sun, ←/→ weeks,
   "Today"). Items: matches (opponent, venue, competition, kick-off, status) and sessions (type, start
   time, duration). "Add match" / "Add session" buttons; Ctrl+N asks which
   (`enabled: root.visible`). Opening an item edits it; a session also offers "Open training sheet",
   which calls `navigateRequested("training", { sessionId: <id> })`.
2. **Match editor**: opponent (`SelectField` + "Add new opponent…" inline; uniqueness via
   `name_key`), date, kick-off time (optional), venue (home/away/neutral), competition (free text,
   suggestions from `listCompetitions`). Status stays `planned` in W1.
3. **Session editor**: date, start time (optional), duration (default 90, 15–240), type, note.
   Delete with undo toast (attendance goes with it).
4. Permissions: `EditCalendar` to add/edit/delete; read-only otherwise.
5. Dates/times exchanged with QML as ISO strings (AGENTS §6).

## Tests
Service: permission per role, opponent uniqueness with Polish letters, session delete cascades,
duration bounds. Viewmodel: week boundaries (Monday start, year change), list ordering.

## Done when
- [ ] A match and a session created from the keyboard appear on the right days in both modes.
- [ ] Section 3 passes.

## Decisions (accepted plan)
