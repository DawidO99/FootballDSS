# P06b — UI kit and component gallery

**Branch:** `feature/P06b-ui-kit`
**Depends on:** nothing new (can run in parallel with P05/P06)
**Owner agent:** Claude in Antigravity or Codex (effort Medium/High)

## Goal
A consistent set of reusable QML components so every later screen is assembled, not re-styled.
A hidden gallery view shows all of them in both themes and languages.

## Scope — files you may create or change
- `ui/components/*.qml` (new components below; small fixes to existing ones allowed)
- `ui/views/ComponentGalleryView.qml`
- `ui/Main.qml` (only: Ctrl+Shift+K toggles the gallery, same pattern as the charts spike; `ToastHost`
  instance at window level)
- `ui/theme/Theme.qml` (new size tokens only, e.g. `controlHeight`, `rowHeight`, `dialogWidth*`,
  `toastDuration`)
- root `CMakeLists.txt` (QML file list), `i18n/*.ts`
- `tests/qml_smoke/main.cpp` (only if the gallery needs to be opened explicitly)

## Out of scope
C++ viewmodels, data, any real screen content.

## Components and their API (keep the API small; document it in a header comment of each file)
| Component | Purpose / key API |
| --- | --- |
| `DataTable` | `ListView`-based table over a `QAbstractListModel`. `columns: [{ role, title, width (fraction or px), align, delegate? }]`, header click → `sortRequested(role, ascending)` (sorting is done by the viewmodel), `currentIndex`, `activated(index)` on Enter/double-click, ↑/↓/Home/End/PageUp/PageDown, optional `emptyState` item, sticky header, alternating rows from Theme |
| `FormField` | label + required marker + content slot + help text + error text (`error: string`, empty = valid) |
| `TextInputField` | styled `TextField` with `validator`, `maximumLength`, error state |
| `NumberField` | integer input with `from`/`to`, ↑/↓ and buttons, only digits accepted, empty allowed when `optional` |
| `DateField` | `date` is an ISO string `yyyy-MM-dd` (empty = none; AGENTS §6); text input in the app language's short date format + calendar popup (`MonthGrid`, `DayOfWeekRow`); `date`, `minimumDate`, `maximumDate`, `optional`; invalid text shows an error, never a silent change |
| `SelectField` | styled `ComboBox`, `model`, `textRole`, `valueRole`, `currentValue` |
| `SearchField` | text field with search icon and clear button, `textChanged` debounced 200 ms |
| `SegmentedControl` | 2–5 exclusive options, keyboard ←/→, `currentValue` |
| `AppDialog` | modal dialog: title, content slot, primary/secondary/danger buttons, Enter = primary, Esc = cancel, focus trapped |
| `Toasts` (QML singleton) + `ToastHost` | views call `Toasts.show(text, undoText, undoCallback)` / `Toasts.info(text)` / `Toasts.error(text)`; the single `ToastHost` in `Main.qml` renders them; one toast at a time; 10 s; undo button |
| `Card` | surface container with optional title and actions row |
| `AppTabs` | styled `TabBar` + content `StackLayout` |
| `IconButton`, `SecondaryButton`, `DangerButton` | consistent with `PrimaryButton`; `IconButton` has a tooltip and `Accessible.name` |

Shortcuts inside components/gallery use `enabled: root.visible`.
Every component: colors/sizes from `Theme`, keyboard focus visible, `Accessible.name`/`role`, all
strings `qsTr()`, `hitMin` respected (and a `touch: bool` property where it matters to use `hitTouch`).

## Requirements
1. **Gallery** (`Ctrl+Shift+K`): every component in its normal, focused, disabled and error states;
   `DataTable` with ~30 sample rows and a `StatusBadge` cell; a dialog and a toast that can be triggered.
   Sample data lives in the gallery file only.
2. `qml_smoke` instantiates the gallery in both languages and themes without warnings.
3. No business logic, no C++.

## Done when
- [ ] Gallery screenshots (dark/light × PL/EN) in the report or saved by `qml_smoke --screenshots`.
- [ ] Tab order through the gallery is sensible; Esc closes the dialog; toast undo fires its callback.
- [ ] Section 3 passes.

## Decisions (accepted plan)
1. Theme gets new size tokens (`controlHeight`, `controlHeightTouch`, `rowHeight`, `tableHeaderHeight`,
   `fieldWidth`, `dialogWidthS/M/L`, `toastWidth`, `toastDuration`, `searchDebounce`, `tooltipDelay`,
   `focusRingWidth`, `iconSize.s/m/l`) and four new colors for dark + light: `rowAlt`, `rowHover`,
   `selection`, `scrim`.
2. Input fields share one pattern: `error` (external, from the viewmodel), read-only `validationError`
   (own parsing/range check) and `effectiveError` (either of them; drives the red border). Values are
   bound from the viewmodel; user edits are reported through a separate signal (`valueModified`,
   `dateEdited`, `valueActivated`, `activated`) to avoid binding loops.
3. `FormField` uses `mandatory` instead of `required` (QML keyword).
4. `SearchField` root is a `FocusScope` with a read-only, debounced `text` (so `textChanged` is
   debounced); `clear()` and Esc clear it immediately.
5. `DateField` display format: PL `dd.MM.yyyy`, EN uses en_GB `dd/MM/yyyy`; the year always has four
   digits. The outside value is always ISO `yyyy-MM-dd`.
6. Focused state in the gallery is shown by Tab navigation; the first field gets focus when the gallery
   opens. The gallery is split into tabs (Buttons, Inputs, Table, Containers & feedback); `qml_smoke`
   saves one screenshot per tab × language × theme, the last tab with the sample dialog and a toast.
7. `qml_smoke` opens the gallery in every language × theme and checks automatically that toast undo
   fires its callback (counter in the gallery + `Toasts.undo()`).
8. `AppDialog`: the primary button does not close the dialog (the caller closes it after the viewmodel
   accepts); Esc and the secondary button reject.
9. `Toasts`: a new toast replaces the current one (its undo is dropped, the action stays done); the
   10 s timer pauses while hovered; a callback that can no longer run is skipped without an error.
10. `DataTable` columns always fit the table width (no horizontal scroll in W1); `emptyState` is a
    `Component`; custom cells derive from `DataTableCell`.
11. Existing components stay untouched except `PrimaryButton` (`touch`, `Theme.controlHeight`) and
    `Icon` (new icons). `DatabaseErrorView`'s second button becomes a `SecondaryButton`; `TopBar` is
    not changed (P07b).
12. `touch` exists on the buttons, `SegmentedControl`, `NumberField` and `DataTable`.
