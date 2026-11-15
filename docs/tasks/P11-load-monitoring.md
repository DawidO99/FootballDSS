# P11 — Load monitoring: sRPE, acute:chronic ratio, alerts, charts

**Branch:** `feature/P11-load`
**Depends on:** W1 (`v0.2-W1`)
**Owner agent:** Claude (effort High); review: Codex

## Goal
The coach sees for every player whether the recent training and match load is in line with what the
player is used to, as a status (color + icon + word) and a chart, and the alert is a "signal to talk to
the player", never a diagnosis.

Read first: SPEC §7.1 (formulas and thresholds), §2 (capability "Load"), §4 (Load screen, chart
components), AGENTS §5 (QtConcurrent rule).

## Scope — files you may create or change
- `src/core/algorithms/Load.hpp/.cpp` (pure, no Qt): daily load series, EWMA, ratio, monotony, strain,
  status
- `src/services/LoadService.hpp/.cpp` (reads attendance, sessions, appearances, matches; one member and
  one construction line in `ServiceRegistry`)
- `src/viewmodels/LoadViewModel` (team table), `PlayerLoadViewModel` (one player's series), list models
- `ui/views/LoadView.qml`; `ui/components/LoadChart.qml`, `LoadStatusBadge.qml`, `ChartLegend.qml`,
  `ChartTooltip.qml`; the "Load" tab of `ui/views/PlayerCardView.qml`
- `tests/core/*`, `tests/services/*`, `tests/viewmodels/*`, `tests/qml_smoke/main.cpp`, CMake lists,
  `i18n/*.ts`, `ui/views/ComponentGalleryView.qml`

## Out of scope
Dashboard (P15), capability locking in navigation (P15), editing thresholds in Settings (P19),
wellness.

## Requirements
1. **Session load** (sRPE) = RPE × effective minutes, only for `present` rows with RPE. **Match load** =
   appearance RPE × minutes played (`minute_off − minute_on`, starters from 0). A match or session
   without RPE adds nothing and is counted as "missing RPE" (shown in the player view, not hidden).
2. **Daily load** per SPEC §7.1, EWMA with λ = 2/(N+1), N = 7 and 28; seeded from the first day with an
   entry; days before the first entry are not zeros.
3. **Status** per player for "today": red (ratio > 1.5), yellow (1.3–1.5 or monotony > 2), green
   (0.8–1.3), info "underloaded" (< 0.8), grey "not enough data". Grey while the player has RPE on fewer
   than 14 days, or fewer than 28 days passed since the first entry (show acute load only, labelled
   "building the baseline"). Never a fake green. Thresholds are a `LoadThresholds` struct with the SPEC
   defaults, passed in (P19 makes them editable).
4. **Computation** runs via `QtConcurrent` on data read on the main thread (AGENTS §5); the screen shows
   a busy state, never blocks.
5. **Load screen**: team table (player, acute, chronic, ratio, monotony, last-7-days load, status),
   sortable, filter by status; empty/grey states explain what is missing ("Enter RPE on 9 more days").
   Selecting a player shows the chart.
6. **Chart** (`LoadChart`, Qt Graphs): last 8 weeks, daily load bars (sessions and matches
   distinguishable not by color only), acute and chronic EWMA lines, the ratio band; legend and tooltip
   per SPEC §4. Same component on the player card "Load" tab.
7. **Wording**: status texts and tooltips say "signal to talk to the player"; no "injury risk" anywhere.
8. Permission: `ViewAnalysis` (every role). Read-only.

## Tests
Core: EWMA and ratio against values computed by hand in the test (document the arithmetic); first-entry
seeding; monotony with SD = 0 → undefined; each status boundary (1.3, 1.5, 0.8, exactly 14 days / 28
days); match load from minutes. Demo data (P10, fixed seed and start date): the overloaded player is red,
the returning player is grey or underloaded, nobody else is red. Service: archived players excluded;
sessions in the future ignored. Viewmodel: sort and filter.

## Done when
- [ ] Demo data → Load screen shows one red player with a chart that explains why.
- [ ] A new club with 3 days of RPE shows grey statuses with a hint, no green.
- [ ] Section 3 passes.

## Notes for the thesis
Screenshot of the team table and of the overloaded player's chart. Write the formulas (Foster sRPE,
EWMA after Williams et al. 2017) and why the UI avoids "injury risk" (Impellizzeri et al. 2020).

## Decisions (accepted plan)
