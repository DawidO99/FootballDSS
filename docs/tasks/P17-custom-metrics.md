# P17 — Custom metrics

**Branch:** `feature/P17-metrics`
**Depends on:** W1 (`v0.2-W1`); independent of P11–P16
**Owner agent:** Claude (effort Medium); review: Codex

## Goal
A club measures what it cares about (sprint 30 m, jumps, test results, a match statistic) without code
changes: define a metric once, enter values per session, match or date, see them on the player card.

Read first: SPEC §6 (`metric_definition`, `metric_value`, enumerations), §5 (`ManageMetrics`).

## Scope — files you may create or change
- `src/core/domain/Metric.hpp`, validation in `src/core/domain/Validation.*`
- `src/data/repositories/MetricRepository.hpp/.cpp`
- `src/services/MetricService.hpp/.cpp` (+ `ServiceRegistry` lines)
- `src/viewmodels/MetricDefinitionsViewModel`, `MetricEntryViewModel`, models
- `ui/views/MetricsView.qml` (new section "Metrics" in `NavigationModel` and the sidebar),
  `ui/views/SettingsView.qml` (metric definitions card), dialogs in `ui/components/`
- `ui/views/PlayerCardView.qml` (only: a metrics card on "Overview")
- tests, CMake lists, `i18n/*.ts`, gallery, `qml_smoke`

## Out of scope
Using metrics in the lineup assistant (W3), importing metric columns (P18), charts beyond a sparkline.

## Requirements
1. **Definitions** (Settings): name PL and EN (one is enough; the other falls back), key generated from
   the name and unique, type (integer, decimal, boolean), unit, optional min/max, aggregation (sum, avg,
   min, max, last), scope (match, training, player), "higher is better". Edit; delete only when no
   values exist, otherwise explain why.
2. **Entry** (Metrics section): pick a metric, then the session (training scope), match (match scope)
   or date (player scope) → grid of players with one value cell; decimal comma accepted; boolean as a
   checkbox; range errors in the row; Ctrl+S with a visible Save button; leave guard.
3. **Player card**: latest value and the aggregation over the last 28 days per metric, with the unit;
   arrow up/down vs the previous value, colored by "higher is better" plus an icon (not color only).
4. Permission `ManageMetrics` for definitions and entry; everyone reads.

## Tests
Validation of every type and range; key uniqueness; delete with values refused; aggregation functions;
decimal comma parsing; permission per role.

## Done when
- [ ] Define "Sprint 30 m [s], decimal, lower is better, training" → enter values for a session →
      the player card shows the latest value and the 28-day average with the right arrow.
- [ ] Section 3 passes.

## Notes for the thesis
This is the "universal from B-klasa to pro" mechanism for data the app does not know in advance.

## Decisions (accepted plan)
