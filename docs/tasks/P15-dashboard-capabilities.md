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
