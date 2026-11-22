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
1. Files beyond the scope list: `ui/Main.qml` (section key → view), `src/viewmodels/ErrorPresenter.cpp`
   (messages of the new fields), `ui/components/Icon.qml` (icons `metrics`, `arrow-up`, `arrow-down`),
   `src/core/domain/Ids.hpp` (`MetricDefinitionId`, `MetricValueId`) and `src/core/domain/Metric.cpp`.
2. Aggregation, trend and value parsing live in `core/domain/Metric.*` (not a DSS recommendation, so not
   `core/algorithms/`); unit tests with hand-computed values.
3. Key: an ASCII slug of the English name, or of the Polish one when English is empty (diacritics folded,
   other characters → `_`). A slug that is taken is refused with `Conflict` (field `key`, "a metric with a
   similar name already exists"); no `_2` suffix. The key never changes when the name is edited (P18 maps
   import columns by key).
   Names are unique too (manual test): the Polish and the English name, each on its own, may not equal
   either name of another metric, compared by `core::metricNameKey()` (Latin case and diacritics folded,
   every space and punctuation mark left out: "Sprint 30m" = "sprint 30 m" = "Sprint-30m"). A clash is
   `Conflict` with field `name_pl` or `name_en`; checked on add, on edit (the edited metric aside) and
   on undo of a delete. The key stays unique as before.
4. Limits: name 1–50 characters after trimming (at least one of PL/EN), unit up to 20, `min ≤ max`;
   integer metrics have whole `min`/`max`; boolean metrics have no `min`/`max`.
5. A definition with values keeps its value type and scope (`Conflict`, field `metric_locked`); unit,
   names, min/max, aggregation and "higher is better" may change; stored values stay as they are.
6. Boolean metrics allow only `sum` (count of "yes"), `avg` (share) and `last`.
7. The entry grid lists every non-archived player of the team (no attendance/appearance filter). An empty
   cell means no value; saving it deletes the stored one. Players not in the grid keep their values.
8. Player card window: today − 27 … today inclusive. The date of a value is read as the current date of
   its session or match (`metric_value.date` only for scope player): moving a session in the calendar
   corrects the date of its measurements, and a session moved into the future drops out of the
   summary until its day. "Previous value" is the value before the latest one (by date, then id),
   also from before the window. No sparkline. (Updated after the review.)
9. The missing language of a name is stored as an empty string; display falls back to the other name.
10. Deleting a definition (only without values) shows the 10 s Undo toast; undo recreates it with the
    same key.
11. Every role reads: the definitions card in Settings is visible read-only without `ManageMetrics`.
12. Scope extension (review): `src/data/repositories/RepositorySupport.*` gets `RowReader::real()` /
    `optionalReal()` and `optionalRealValue()`: the first REAL columns read by a repository
    (`metric_value.value`, `metric_definition.min/max`).
13. Values and limits are shown in the shortest text that reads back as the same number
    (`QLocale::FloatingPointShortest`), with the decimal separator of the app language: reading and
    saving again never changes a value.
14. **import_id of a corrected value (review of P18)**: `saveValues()` keeps the `import_id` of the
    value it replaces, so a value an import wrote and the user corrected later still goes with the
    undo of that import (counted as changed after the import, P18 decision 10). Replaces the
    earlier behaviour (the corrected value lost its `import_id`).
