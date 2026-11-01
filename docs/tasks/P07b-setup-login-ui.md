# P07b — First-run wizard, profile picker, staff settings, user menu

**Branch:** `feature/P07b-setup-ui`
**Depends on:** P07a and P06b merged
**Owner agent:** Claude in Antigravity or Codex (effort Medium/High)

## Goal
The screens on top of P07a: a new installation walks the coach through a wizard, every later
start shows the profile picker with PIN, and the Coach manages staff in Settings.

## Scope — files you may create or change
- `src/viewmodels/SetupWizardViewModel`, `ProfilePickerViewModel`, `StaffViewModel`
- `ui/views/SetupWizardView.qml`, `ui/views/ProfilePickerView.qml`,
  `ui/views/SettingsView.qml` (Staff section only), `ui/components/TopBar.qml` (user chip menu),
  new components `PinPad`, `ProfileTile` (+ gallery entries)
- `ui/Main.qml` (replace the P07a placeholders with the real views)
- `src/viewmodels/AppState.hpp/.cpp` (only: remove the temporary `debugBuild` and
  `createTestClubAndLogin`), `tests/qml_smoke/main.cpp` (only: `exerciseStateChanges` goes through the
  real viewmodels instead)
- If a service turns out to be needed: one member and one construction line in `ServiceRegistry`
- root `CMakeLists.txt`, `tests/viewmodels/*`, `i18n/*.ts`

## Out of scope
Services (P07a), demo data option (P10), squad/calendar/training.

## Requirements
1. **Wizard** (`NeedsSetup`), 4 steps with Back/Next and per-step validation: (1) welcome + language;
   (2) club name, team name, category, season (default from today, e.g. `2026/27`); (3) coach name +
   PIN + repeat PIN; (4) summary → "Create" (P10 adds "Load demo data" here). Enter = Next.
2. **Profile picker** (`NeedsLogin`): tiles of active staff (name, translated role, initials avatar),
   PIN by keyboard or on-screen `PinPad` (hit targets `hitTouch`), wrong PIN with remaining
   attempts, lockout message with countdown. Enter submits, Esc clears.
3. **Settings → Staff** (only with `ManageStaff`; others don't see the section): `DataTable` of staff
   (name, role, active), add/edit in `AppDialog`, deactivate with undo toast, reset PIN.
4. **Top bar** user chip: name + role; menu "Change PIN", "Log out".
5. Views and actions the role may not use are hidden/disabled via `AppState.can()`.
6. Errors shown through `ErrorPresenter` texts, never `detail`.

## Tests
Viewmodel: wizard step validation (PIN mismatch, empty names, season format), picker lockout text.

## Done when
- [ ] Delete `club.sqlite` → wizard → shell; restart → picker → PIN → shell.
- [ ] Assistant: no Staff section in Settings. 5 wrong PINs → 30 s lockout, survives restart.
- [ ] Section 3 passes (qml_smoke covers all three states).

## Notes for the thesis
Screenshots of the wizard steps and the profile picker; ADR-007 explains the security trade-off.

## Decisions (accepted plan)
