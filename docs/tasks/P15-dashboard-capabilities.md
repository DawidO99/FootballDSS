# P15 — Dashboard and adaptive capabilities

**Branch:** `feature/P15-dashboard`
**Depends on:** P11 merged; merge after P14 (last-5 form uses finished matches and ratings)
**Owner agent:** Claude (effort Medium); review: Codex

## Goal
The first screen after login answers "what needs my attention" (next match, who is unavailable, load
signals, form), and every feature shows whether it is available or what data is still missing — the
"one system from B-klasa to pro" idea of SPEC §2.

Read first: SPEC §2 (capability model), §4 (Dashboard, top bar "days to next match").

## Scope — files you may create or change
- `src/core/algorithms/Capabilities.hpp/.cpp` (pure: counts → state + hint data)
- `src/services/CapabilityService.hpp/.cpp`, `DashboardService.hpp/.cpp` (+ `ServiceRegistry` lines)
- `src/viewmodels/DashboardViewModel`, `CapabilityModel` (QML-exposed), `NavigationModel` (only: lock
  state per section)
- `ui/views/DashboardView.qml`, `ui/components/DashboardCard.qml`, `LockedState.qml`;
  `ui/components/Sidebar.qml`, `SidebarButton.qml`, `TopBar.qml` (lock icon, days to next match)
- `ui/views/SettingsView.qml` (only: "Show advanced modules" toggle) and `AppSettings`
- tests, CMake lists, `i18n/*.ts`, gallery, `qml_smoke`

## Out of scope
Pro modules themselves (W3); changing capability thresholds in the UI.

## Requirements
1. **Capabilities** per SPEC §2 table: Squad, Matches, Load, Ratings, Opponents, Event data, with the
   default conditions as constants. States: available, locked with hint, hidden (Pro: Event data until
   "Show advanced modules"). Hints are concrete and counted ("Enter RPE on 9 more days to see load
   trends"). Recomputed when data changes (after saves) and on login.
2. **Navigation**: a locked section stays visible with a lock icon (not color only); opening it shows
   `LockedState` with the hint and a button to the place where the data is entered. Nothing that is
   available today becomes locked for a club with W1-level data except as the table says (check with
   demo data: everything available).
3. **Dashboard cards**: next match (date, opponent, days to go, lineup ready / issues count → button to
   Pre-match); unavailable players (type, expected return, opens the card; injury details only for
   roles with `ViewAvailabilityDetails`); load signals (red and yellow from P11, "signal to talk to the
   player"); last-5 form (W/D/L as letter + color, goals, average rating); today's session (opens
   Training). Each card has its own empty/locked state.
4. **Top bar**: "days to next match" chip (opens Pre-match).
5. Read-only, every role.

## Tests
Core: each capability boundary (10 vs 11 players, 13 vs 14 RPE days…) and hint numbers. Service: demo
data → all available; empty club → Squad locked with "add 11 players". Viewmodel: card contents for
demo data with a `FakeClock`. `qml_smoke`: dashboard in both themes, empty club and demo club.

## Done when
- [ ] Empty club: Dashboard and locked sections explain what to enter; after entering it they unlock.
- [ ] Demo club: dashboard shows the next match, 2 injured + 1 suspended, the overloaded player, form.
- [ ] Section 3 passes.

## Notes for the thesis
Screenshots: empty club vs demo club side by side — the best picture of "one system from B-klasa to
pro".

## Decisions (accepted plan)
1. **Scope extensions** (accepted): `ui/Main.qml` (a locked section shows `LockedState` instead of its
   view; capabilities reload on every section change, login and toggle; one `DashboardViewModel` shared
   by `TopBar` and `DashboardView`); `src/data/repositories/CapabilityRepository.hpp/.cpp` (one
   `SELECT COUNT` per counter); `ui/components/Icon.qml` (icon `lock`); `ui/components/DashboardRow.qml`
   (the clickable line of a card); `ui/theme/Theme.qml` (`dashboardCardMinWidth`). `CapabilityModel` is a
   plain `QML_ELEMENT` held by `Main.qml` next to `NavigationModel`, not a singleton: no global state, and
   the component tests, which have no registry, never create it.
2. **Locked sections**: `prematch` ← Squad, `matchreport` ← Matches. The Load section is not locked
   (human decision during implementation): its P11 "not enough data" view with per-player hints stays the
   place for a new club; the Load capability locks the load-signals card of the dashboard and shows in its
   "Features" card. Match Day keeps its own empty state (it needs a saved lineup). Ratings, Opponents and
   Event data have no section yet; they show only in the "Features" card (hidden ones left out).
3. **Counters**: Squad = active (not archived) players ≥ 11. Matches = finished matches ≥ 1. Load = days
   with ≥ 1 RPE (present training attendance or finished-match appearance) ≥ 14; between 14 and 28 the
   capability is available with an informative "full acute:chronic in N days", no fourth state.
   Ratings = ≥ 1 match rating, player attribute or metric value. Opponents = ≥ 3 finished matches with a
   result (any opponent; league results import does not exist yet). Event data = ≥ 1 event data file.
   Thresholds are constants in `core::CapabilityThresholds` (SPEC §2 defaults), not editable in the UI.
4. **"Show advanced modules"**: an `AppSettings` preference of this computer (QSettings), like the theme;
   every role sees it in Settings, no permission check. Off: Event data without any file is hidden; on: locked
   with hint. With a file it is available either way (SPEC §2: hidden until data exists).
5. **Refresh**: services have no change signals; `CapabilityModel.reload()` runs on every section change,
   on login and on the toggle, the dashboard also reloads when it becomes visible. A lock in the sidebar
   may be stale until the next section change, like the data of other views.
6. **Top bar chip**: next planned match on or after today → "Today" / "In N days" (opens Pre-match); a
   live match → "Match in progress" (opens Match Day); no match → no chip.
7. **Actions by role** (AGENTS §6): `LockedState` and the empty states of the cards show the action button
   only to a role that can enter the missing data (Squad → `EditSquad`, Load → `EditTraining`, Matches /
   next match → `EditCalendar`, today's session → `EditCalendar`); other roles get the text
   "Ask the coach or the assistant to …". Covered by a viewmodel test with a role without permission.
8. **Card lists as `QVariantList`**: unavailable players, load signals, last-5 form, today's sessions and
   the features list are small, read-only lists rebuilt on reload — the AGENTS §5 case "small fixed or
   nested read-only lists may be a `QVariantList`", so no list models.
