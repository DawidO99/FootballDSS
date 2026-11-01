# P08 — Squad: player list, player card, availability

**Branch:** `feature/P08-squad`
**Depends on:** P07a and P06b merged (parallel with P07b, P09a, P09b, P10)
**Owner agent:** Claude (effort Medium)

## Goal
The coach manages the squad: a sortable, filterable table, add/edit/archive players with
validation, a player card, and availability entries (injury, illness, suspension, absence) whose
details respect roles.

## Scope — files you may create or change
- `src/services/SquadService.hpp/.cpp` (thin: permission checks + repositories + validation;
  + one member and one construction line in `ServiceRegistry`)
- `src/viewmodels/SquadViewModel.hpp/.cpp` (list model + filter/sort/search),
  `PlayerEditorViewModel`, `PlayerCardViewModel`, `AvailabilityViewModel`
- `ui/views/SquadView.qml`, `ui/views/PlayerCardView.qml` (or a component shown from SquadView),
  dialogs in `ui/components/` if reusable
- `tests/services/*`, `tests/viewmodels/*`, CMake lists, `i18n/*.ts`

## Out of scope
Load, matches and notes tabs content (they show an `EmptyState` saying what will appear there),
import, demo data, dashboard.

## Requirements
1. **Table** (`DataTable`): number, name, position (short code + full name tooltip), age (from birth
   year), status badge (derived status for today; color + icon + word). Sort by any column; filter
   by status group and position group (`SegmentedControl`/`SelectField`); search by name or number;
   "Show archived" toggle. Empty squad → `EmptyState` with "Add player" (and later "Import").
2. **Add/edit player** (`AppDialog`, Ctrl+N opens it): first/last name, shirt number (optional),
   birth year (optional), primary position, secondary positions (multi-select), foot, notes.
   Validation via core `validate()`; duplicate number shows the error on the field. Save with Enter.
   Ctrl+N uses `enabled: root.visible` (AGENTS §6).
3. **Archive** with undo toast (10 s); archived players disappear unless "Show archived".
4. **Player card** (open with Enter/double-click): header (number, name, position, age, status),
   tabs Overview / Load / Matches / Notes. Overview: data + availability history (newest first) +
   "Add availability".
5. **Availability**: type, from, expected return (optional), description (optional), "Mark as
   returned" sets the actual end date. Description visible and editable only with
   `ViewAvailabilityDetails` (Coach, Physio); other roles see type and dates. Who may add/edit:
   `EditAvailability`. Saving an entry as an Assistant must not erase the description
   (repository `update` with `description = nullopt`).
6. Permissions: without `EditSquad` the add/edit/archive actions are hidden; services reject anyway.
7. Sorting/filtering is done in C++ (e.g. `QSortFilterProxyModel` in the viewmodel), not in QML.

## Tests
Service: permission denial per role; archive/unarchive; availability description hidden from an
Assistant at the service boundary (the returned DTO has no description), not only in QML.
Viewmodel: filter + search + sort combination over a small seeded model.

## Done when
- [ ] Add 3 players from the keyboard only (Ctrl+N, Tab, Enter).
- [ ] Duplicate shirt number shows a field error; archive + undo works.
- [ ] Physio sees an injury description, Assistant does not.
- [ ] Section 3 passes.

## Decisions (accepted plan)

Accepted by the human on 2026-11-01. These override the packet text above (AGENTS §11).

1. **Status groups:** All / Available / Unavailable. Archived players pass the status filter only on
   All.
2. **No column chooser in P08**, although SPEC §4 lists it. It goes to `docs/BACKLOG.md` (W2).
3. **Empty squad without `EditSquad`:** text only, no button. This is an accepted exception to AGENTS
   §6: those roles have no next step of their own. The "Import players" button of the placeholder goes.
4. **An archived player shows an "Archived" badge** instead of a status, because
   `AvailabilityRepository::listActiveOn` leaves archived players out and they would read "Available".
5. **No deleting of availability entries in P08.** It goes to `docs/BACKLOG.md`.
6. **A description from a role without `ViewAvailabilityDetails`:** `PermissionDenied` with field
   `description`, not silently dropped (AGENTS §12). The viewmodel never sends one.
7. **"Mark as returned" is a dialog with a date**, default today, minimum `from_date`.
8. **Multi-line fields:** Enter saves, Shift+Enter breaks the line. The notes field has the help text
   "Shift+Enter: new line".
9. **Position codes are international in both languages** (GK, RB…), the translated full name is in
   the tooltip.
10. **Reading the squad needs a logged-in user of any role.** SPEC §5 has no read permission.
11. **Editing an archived player is allowed**, otherwise a shirt number conflict on restore could not be
    solved. "Add availability" for an archived player is hidden in the UI only.
12. **`SquadView` handles `navigationArgs.playerId`:** it opens the card of that player at once, for the
    dashboard links of W2.
13. **The card has a visible back button** ("← Squad" / "← Kadra"). Esc is only its shortcut, shown in
    the tooltip (AGENTS §6).
14. **Three members in `ServiceRegistry`:** `m_players`, `m_availability`, `m_squad`. P09a, P09b and P10
    use these two repositories instead of adding their own, and `IsoDates` is the shared date helper of
    the viewmodels.
15. **`Validation.hpp`:** the shirt number and birth year limits only move to the header, so the fields
    need no magic numbers. No change of behaviour.

**Scope extension:** `src/viewmodels/SquadListModel`, `SquadFilterModel`, `AvailabilityListModel`,
`SquadTexts`, `IsoDates`, `ErrorPresenter`, `src/core/domain/Validation.hpp/.cpp`,
`ui/components/MultiSelectField.qml`, `TextAreaField.qml`, `PlayerStatusBadge.qml`,
`PlayerEditorDialog.qml`, `AvailabilityDialog.qml`, `ui/views/ComponentGalleryView.qml`,
`docs/BACKLOG.md`, `tests/qml_smoke/main.cpp`, `tests/qml_components/*`.

**Beyond the accepted plan.**
- `ui/components/CheckBoxField.qml` and the `check` icon in `Icon.qml`: "Show archived" needs a checkbox,
  and the kit had none (AGENTS §6). `MultiSelectField` uses it for its rows.
- `SquadViewModel::filtersActive` instead of `squadEmpty`: an empty table means "no match" when the
  search or a group narrows it, also in a squad of archived players only.
- `SquadViewModel::playersReloaded`, so the view selects the same player after the list is read anew.
- `shirtNumberChanged` and `birthYearChanged` on `PlayerEditorViewModel`: reading the value of a
  `NumberField` anew drops the text it refused, so it must not happen when another field changes.
- `AvailabilityViewModel::defaultReturnDate()` and `clearErrors()`, so the default of decision 7 is not
  logic in QML.

**Known limitations.**
- The status is computed when the list is read. `SquadView` reads it again whenever it becomes visible,
  but a screen left open over midnight keeps yesterday's status until then.
- The age is the current year minus the birth year, because only the year is stored (AGENTS §7), so it
  can be one too high before the birthday.
