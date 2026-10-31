# P08 — Squad: player list, player card, availability

**Branch:** `feature/P08-squad`
**Depends on:** P07a and P06b merged (parallel with P07b, P09a, P09b, P10)
**Owner agent:** Codex or Claude in Antigravity (effort Medium/High)

## Goal
The coach manages the squad: a sortable, filterable table, add/edit/archive players with
validation, a player card, and availability entries (injury, illness, suspension, absence) whose
details respect roles.

## Scope — files you may create or change
- `src/services/SquadService.hpp/.cpp` (thin: permission checks + repositories + validation;
  + one member and one construction line in `ServiceRegistry`, `main.cpp`, `qml_smoke`)
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
