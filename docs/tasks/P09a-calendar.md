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

Accepted by the human on 2026-11-03. These override the packet text above (AGENTS §11).

1. **`ServiceRegistry` gets four members:** `m_opponents`, `m_matches`, `m_training` and `m_calendar`, like
   P08 decision 14. P09b and P10 use these repositories instead of adding their own.
2. **Undo of a delete writes a kept copy back.** The delete runs at once (AGENTS §6); `CalendarViewModel`
   keeps a copy of the last deleted item, and Undo saves it again in one transaction with its child rows.
3. **Matches can be deleted too,** with the same undo toast, only while `planned`.
4. **A match that is not `planned` opens read-only,** with its status and without "Delete". No score: the
   packet does not list it.
5. **A new opponent is saved at once** by "Add", and becomes the selected one. Cancelling the match does not
   undo it.
6. **An empty calendar without `EditCalendar` shows text only,** the same exception to AGENTS §6 as P08
   decision 3. The "Import a schedule" button of the placeholder goes.
7. **"Open training sheet" is there for every role,** in read-only mode too. It is disabled while the form
   has unsaved changes, with a visible hint.
8. **←/→ are visible buttons with their keys in the tooltip.** The keys go through `Keys` on the view and
   only in the week mode, because `SegmentedControl` and `DateField` use the same keys themselves.
9. **Deleting a session with attendance asks first.** "This session has saved attendance for N players" in
   an `AppDialog` with a danger button, then the delete and the undo toast as in decision 2. A session
   without attendance goes straight to the toast. An accepted exception to AGENTS §6 (confirm only for bulk):
   attendance takes a coach minutes to enter. N comes from the service, in a read before the delete;
   `deleteSession` does not change.
10. **The opponents in `SelectField` are sorted with `QCollator`** (Polish letters), like the surnames of P08.
11. **The limitations of decisions 2 and 5 go to `docs/BACKLOG.md`:** a new id after Undo, `import_id` and
    `formation` lost by it, no way to correct an opponent's name.

**Defaults accepted with the plan.**
- "Upcoming" is a date on or after today, also when today's kick-off has passed. A past match that is still
  `planned` stays under "Past" and reads "Planned".
- The date of a new item is today, or the Monday of the shown week when that week does not hold today. After
  a save the view moves to the week of the saved item.
- Ctrl+N is a small dialog with the choice Match / Session (Tab, Enter). Both buttons show Ctrl+N in the tooltip.
- Competition has no length limit, because SPEC gives none. "Liga" and "liga" are two different suggestions:
  the column has no `name_key`.
- The duration steps by 15 minutes with the arrows; any value from 15 to 240 can be typed.
- Reading the calendar needs a logged-in user of any role, like P08 decision 10.

**Beyond the accepted plan.**
- `ui/components/MatchStatusBadge.qml`: the list needs the status as color + icon + word, the same way as
  `PlayerStatusBadge`.
- `CalendarService::attendanceCount()`, `CalendarViewModel::attendanceCount()` and
  `deleteConfirmationText()`, for decision 9. The text has two forms, because "1 zawodnika" and
  "%1 zawodników" split the same way as "1 player" and "%1 players", and the English `.ts` stays unfinished.
- `CalendarViewModel::rowOf()`, so the list selects the saved item, which may be far down.
- `MatchEditorViewModel::opponentName` and `clearNewOpponentError()`, and
  `SessionEditorViewModel::durationStep`, so these are not logic or magic numbers in QML.
- `OpponentListModel` has no `reload()`: `MatchEditorViewModel` reads the opponents and hands them over, like
  `SquadListModel`. `CalendarViewModel` has no `itemsReloaded`: nothing needs it.
- `TimeField` has no `optional`: both uses are optional, so it always is. `hasInvalidText` and `showTime()`
  were added: Enter must not save text that is not a time yet as "no time".

**Scope extension:** `src/viewmodels/CalendarListModel`, `CalendarWeekModel`, `CalendarTexts`, `IsoDates`,
`ErrorPresenter`, `src/core/domain/Validation.hpp/.cpp`, `ui/components/TimeField.qml`,
`SuggestionField.qml`, `MatchStatusBadge.qml`, `CalendarWeekGrid.qml`, `MatchEditorDialog.qml`,
`SessionEditorDialog.qml`,
`DataTable.qml` (optional `sectionRole`), `ui/views/ComponentGalleryView.qml`,
`tests/qml_components/*`, `tests/qml_smoke/main.cpp`, `docs/BACKLOG.md`.

**Decisions after the review.**
- **A tooltip with user data is a `PlainToolTip`,** new in the kit and the gallery: the attached
  `ToolTip.text` guesses rich text, so `<a href='x'>Club</a>` became a link. The rule is in AGENTS §6. The only
  tooltip of `ui/` that shows user data is the item of the week. The position tooltip of P08 holds translated
  names only and uses it too, so one way stays; the other attached tooltips are fixed texts.
- **"Open training sheet" also stays off while a field holds text it refused** (date, time, duration), with the
  same hint. Such text is not in the viewmodel, so `dirty` cannot see it.
- **`SuggestionField` hides only the suggestion identical to the text.** "Liga" stays when "liga" is typed.
- **The items of a day in `CalendarWeekModel` stay a `QVariantList`:** an accepted exception to AGENTS §5, for
  2–3 read-only items.
- **The week shows the same empty state as the list** when the whole calendar is empty, with a button for
  `EditCalendar` and the text alone without. A single empty week stays a grid. `CalendarViewModel::empty` is new.
- **Decision 9 is now a rule of AGENTS §6,** no longer an exception.
- **`CalendarWeekGrid` takes a JavaScript array too,** so the gallery and the tests need no model.

**Scope extension of the review:** `AGENTS.md`, `ui/components/PlainToolTip.qml`, `DangerButton.qml`,
`ui/views/SquadView.qml`.
