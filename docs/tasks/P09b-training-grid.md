# P09b — Training session grid: attendance and RPE

**Branch:** `feature/P09b-training-grid`
**Depends on:** P07a and P06b merged; P09a not required (sessions can be seeded in tests), but
merge after P09a to use its "Open training sheet"
**Owner agent:** Claude (effort High) — the most used screen in the app

## Goal
Attendance and RPE for the whole squad in about a minute, keyboard only.

## Scope — files you may create or change
- `src/services/TrainingService.hpp/.cpp` (+ one member and one construction line in
  `ServiceRegistry`)
- `src/viewmodels/TrainingSessionViewModel` (the grid model), `SessionPickerModel`
- `ui/views/TrainingView.qml`, grid cell components in `ui/components/`
- `tests/services/*`, `tests/viewmodels/*`, CMake lists, `i18n/*.ts`

## Out of scope
Calendar and editors (P09a), load calculations and charts (W2).

## Requirements
1. **Session selection**: `navigationArgs.sessionId` if present; otherwise the most recent session on
   or before today; a `SelectField` to switch; empty state "No sessions yet — add one in Calendar"
   with a button that navigates there.
2. **Grid** rows = non-archived players (number, name); columns: status (`SegmentedControl`:
   present / absent / excused / injured), RPE (0–10), minutes (default = session duration,
   override 0–240), load = RPE × minutes (read-only). Players with an active injury/illness start as
   `injured`; "Mark all present" button. RPE and minutes are cleared and disabled for absent,
   excused and injured.
3. **Keyboard**: a digit in the RPE cell sets it (`1` then `0` within 600 ms = 10), Tab → next column,
   Enter → same column next row, Esc reverts the cell, Ctrl+S saves (a visible "Save" button in the 
   sheet header does the same; it is disabled when nothing changed) (`enabled: root.visible`); focus
   stays in the grid after saving. Leaving with unsaved changes asks once.
4. **Save** = `saveAttendance` (one transaction). Errors per row shown in the row (`ErrorPresenter`).
5. Permission `EditTraining`; read-only otherwise.

## Tests
Service: validation (RPE 0–10, minutes 0–240, empty unless present), permission per role, atomic
save. Viewmodel: default status for injured players, computed load, dirty tracking, digit-entry
timing for 10.

## Done when
- [ ] 20 players filled keyboard-only in about 60 s, saved, reopened with the same values.
- [ ] Section 3 passes.

## Notes for the thesis
Time the 20-player entry with a stopwatch: a usability number for the evaluation chapter.

## Decisions (accepted plan)

Accepted by the human on 2026-11-04. These override the packet text above (AGENTS §11).

1. **Scope extension (AGENTS §10).** Needed by the requirements of the packet itself:
   `src/viewmodels/NavigationModel`, `ui/Main.qml`, `ui/components/TopBar.qml`, `Sidebar.qml` if it has to
   change, `src/viewmodels/ErrorPresenter.cpp`, `src/core/domain/Validation.hpp/.cpp`,
   `ui/components/SelectField.qml`, `ui/views/ComponentGalleryView.qml`, `tests/qml_components/*`,
   `tests/qml_smoke/main.cpp`, `docs/BACKLOG.md`.
2. **Save on a sheet that was never saved.** A session with no saved row has an active Save
   (`canSave`), because otherwise "everyone present" could not be saved at all. The question on leaving
   depends only on the changes of the user (`dirty`), so looking at a sheet never asks.
3. **"Mark all present" skips** players with an active injury, illness or absence on the date of the
   session; the tooltip says so, and each of them can still be changed by hand.
4. **Empty state without `EditCalendar`:** text only, asking the coach or assistant to add a session, as
   P09a decision 6. With `EditCalendar`: text and a button to the calendar.
5. **Only future sessions:** the nearest one opens. Saving in a future session is allowed (BACKLOG).
6. **`AttendanceGrid` instead of `DataTable`:** a `Repeater` in `FocusFlickable`. `DataTable` is a `ListView`,
   which destroys delegates off screen with their focus and text, and its Enter means "open the row".
7. **Default status, for rows without a saved row only** (what the coach saved always wins), from the entries
   active on the date of the session: injury or illness → `injured`; absence → `excused`; suspension →
   `present`. Overlapping entries: injured, then excused. Tested in the viewmodel.

**Additions of the human.**
- A. Closing the window asks only when `dirty`; without changes it closes at once. `qml_smoke` checks both,
  and every wait in it has a time limit and a message: nothing may wait forever on Windows.
- B. Status labels at 1280×720: when they must be shortened, the full name goes to `Accessible.name` and a tooltip.
- C. To `docs/BACKLOG.md`: `import_id` lost on save, no date of joining, RPE saved in a future session.

**Defaults accepted with the plan.**
- An injury counts on the date of the session, not today.
- An override equal to the duration of the session is stored empty, so the row follows a later change of
  the duration.
- `present` without RPE is valid; the load then shows "—".
- Minutes out of range reach the viewmodel: the row shows the error at once and a save refuses them.
  `NumberField` would keep the old value, and Ctrl+S would save something else than what is shown.
- `1` sets 1 at once; `0` within 600 ms (600 included) in the same row makes it 10. Later or in another row `0`
  is 0. `1` then `5` is 5.
- Esc restores what the cell held when it got focus; on a status it also restores the RPE and minutes which
  that change cleared.
- Delete/Backspace clears the RPE. Empty minutes mean the duration of the session.
- Tab skips disabled cells. Enter goes to the nearest row where that column is enabled; on the last one the focus
  stays. The first enabled RPE gets the focus when a sheet opens.
- A click focuses a cell, so RPE is not keyboard-only.
- "Asks once": one dialog per attempt to leave (Save / Discard changes / Stay), for the sidebar, the button of the
  empty state, logout, closing the window and another session in the `SelectField`. A failed save stays on the
  sheet with the errors in the rows.
- Coming back from the sidebar keeps the chosen session. The view reloads when shown, unless it has changes.
- A `sessionId` that does not exist: a toast and the default session.
- Row order: shirt number, players without one last, then surname with `QCollator`.

**Beyond the accepted plan.**
- **No toast after a save, a "Saved" `StatusBadge` in the header instead.** The focus stays in the grid after a
  save, and a toast would cover its last rows for 10 s.
- **An empty state for a session with no players,** with a button to the squad for `EditSquad` and the text alone
  without: otherwise the sheet would be an empty table, against AGENTS §6.
- **The status labels stay in full.** At 1280×720 the Polish words fit, so decision B did not apply. `qml_smoke`
  checks it with the widest word selected, in both languages.
- `AttendanceGrid` is split into `AttendanceGridRow` and `AttendanceGridHeading`, so `RpeCell` and `MinutesCell` stay
  components of their own. `TrainingSessionViewModel::firstErrorRow()` leads the focus to a row a save refused.

**Scope extension of the implementation:** `ui/components/AttendanceGridRow.qml`, `AttendanceGridHeading.qml`,
`ui/components/Sidebar.qml` (an `objectName` for the test only), `tests/viewmodels/ErrorPresenterTest.cpp`.
