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

Accepted by the human on 2026-11-01. These override the packet text above (AGENTS §11).

1. **`ChangePinViewModel`, a fourth viewmodel.** Every role may change its own PIN, `StaffViewModel` is
   for the Coach only, `AppState` may only lose members, and QML may not call services.
2. **Deactivating your own account is disabled in your own row, with a tooltip.** It ends the session
   (P07a decision 7) and `reactivate` needs a user with `ManageStaff`, so its undo toast could not work
   (AGENTS §6). The rule in `AuthService` stays. Changing your own role in the dialog still works and
   leads to the profile picker.
3. **A new season starts on July 1.** From July: `Y/Y+1`, before July: `Y-1/Y`. The field stays editable.
4. **A "Reactivate" button.** After the 10 s of the undo toast a deactivated user could not be brought
   back otherwise. The action follows the selected row.
5. **PIN texts without `%n`.** `FootballDSS_en.ts` stays `unfinished` (AGENTS §6), so plural forms would
   show "1 second(s)" in English.
6. **Viewmodels do not know `AppState`.** They signal success and the view calls `AppState.refresh()`.
7. **`Toasts.dismiss()` on every change of session**, so an undo toast does not reach the next user.

**Scope extension:** `src/viewmodels/ChangePinViewModel`, `StaffListModel` (shared by the picker and
Staff), `ErrorPresenter` (`pin_repeat`, `pinCheckMessage()`), `ui/theme/Theme.qml` (sizes),
`ui/components/PinField.qml`, `ui/views/ComponentGalleryView.qml`, `tests/qml_components/*`,
`tests/CMakeLists.txt`, `tests/qml_smoke/main.cpp` beyond `exerciseStateChanges` (the placeholder branch
and the screenshots), `TopBar` (`AppState.teamName` instead of "My team"), the placeholder text in
`SettingsView`.

**Beyond the agreed interfaces.**
- `loadError` (a text) instead of `loadFailed` on the picker, also on `StaffViewModel`: the viewmodels read
  their list in the constructor, before the view can connect to `errorOccurred`.
- `StaffListModel::rowAt()` and `rowOf()`: a `QAbstractListModel` cannot be read by row from QML, and the
  same person has to stay selected after the list is read anew.
- `TopBar::openUserMenu()`, `openChangePinDialog()`, `Main.qml` `setupWizard`, `profilePicker`,
  `SettingsView.staffSection`: for `qml_smoke`.
- `SignalCount` in `ViewModelFixture.hpp` instead of `QSignalSpy`, so `fdss_vm_tests` does not link Qt Test.
- The tooltip on the disabled "Deactivate" sits on a wrapper with a `HoverHandler`: a disabled button gets
  no hover.

**Review decisions (one round).**
- The lockout counts down locally (`m_lockSeconds - 1`, a tick per second) with no database read. The
  database is read only when a profile is selected, and a login answer sets the time left. A failed read
  gives `errorOccurred` and leaves the timer running, so the PIN field cannot stay disabled after the time
  is up. The service stays the authority: a countdown that ended early gets the true time from the next
  answer. A countdown can also run late, because a timer does not tick while the computer sleeps: the
  field then stays disabled until the countdown ends or the profile is selected again.
- A tile that gets the focus scrolls into view (`revealTile`), covered by `exerciseTileScrolling`.
- `StaffViewModel` checks `ManageStaff` itself before every change (`checkAllowed`), and before the input,
  and does not call the service otherwise (AGENTS §4). The tests tell the two refusals apart by the log.
- Rejected: driving saves through the dialogs and the toast in `qml_smoke`. The manual test covers it.

**Removed tests.** `AppStateTest` loses `TestClubButtonGoesFromSetupToReady`,
`TestClubButtonLogsTheCoachBackIn` and `TestClubButtonReportsACoachWithAnotherPin`: they test the
temporary API this packet removes. The wizard and picker viewmodel tests take over what they covered.

**Known limitations (planned for W2).**
- L1: a failed read at startup shows the wizard. `AppState` starts at `NeedsSetup`, keeps its state on a
  failed read, and the toast from its constructor reaches nobody. An existing club is safe because
  "Create" ends in `Conflict`, but the user walks through the wizard for nothing.
- L2: the season `2026/28` passes validation, because `validate()` checks only the format.
- L3: there is no language switch on the profile picker, only in the wizard and the top bar. The choice
  is remembered.
