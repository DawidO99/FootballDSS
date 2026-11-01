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
