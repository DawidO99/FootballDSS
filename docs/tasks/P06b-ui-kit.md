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
