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
1. **Scope extensions**: `src/core/demo/DemoGenerator.cpp` and `tests/core/DemoGeneratorTest.cpp` (story
   parameters for the load patterns, new FNV golden constant explained in the test);
   `ui/components/StatusBadge.qml` (a `Neutral` kind for grey); `ui/components/PlayerLoadPanel.qml` (the
   same panel on the Load screen and the player card); `ui/theme/Theme.qml` (new tokens only);
   `src/viewmodels/LoadTexts.*`, `LoadListModel`, `LoadFilterModel`; `AGENTS.md` §4 (service name
   `LoadService`); `docs/BACKLOG.md` (item for P19).
2. **Demo expectations** (replace the Tests paragraph on demo data), checked for 7 consecutive start dates
   (today = Mon … Sun): `loadSpike` red on at least 5 of the 7 days and at least yellow on the others (if
   that cannot be reached, the report gives the best distribution reached); `returned` may be red or
   yellow: a fast return after a break with a low chronic load is a correct signal, and the explanation
   names it ("chronic load low after a break"); nobody else is red on any of the 7 days; yellow and
   underloaded are allowed.
3. **Known limitation of the method** (thesis material): the EWMA ratio with λ = 2/(N+1) and daily
   resolution follows the weekly microcycle. The same week of training gives a different ratio on Monday
   (after the match) than on Thursday (after a rest day), about ±20 % for a typical player, so a one-week
   load spike is red on some weekdays and yellow on others.
4. **"Today"**: the series ends today when the player has an RPE entry today (session or match),
   otherwise yesterday, because the day is not over (attendance saved before the session, RPE still to
   come, does not pull the status down). The "as of" date is shown.
5. **Boundaries**: green 0.8 ≤ ratio ≤ 1.3; yellow 1.3 < ratio ≤ 1.5 or monotony > 2; red ratio > 1.5;
   underloaded ratio < 0.8. Precedence red > yellow > underloaded > green; a yellow status lists every
   reason that applies. Grey while RPE was entered on fewer than 14 distinct days (sessions and matches
   together, RPE 0 counts) or `asOf − first entry < 28` days; chronic = 0 is grey too.
6. **Service name**: `LoadService` (AGENTS.md §4 updated). Reads on the main thread (the last 365 days
   through today; the weight of an older seed is below 10⁻¹¹), computes through `QtConcurrent::run`, returns
   `Result<QFuture<…>>`. Thresholds: `LoadThresholds{}` from the service until P19.
7. **Exception to AGENTS §5 (list data for QML)**: the chart series of `PlayerLoadViewModel` (56 values
   per series) are `QList<qreal>` / `QList<QPointF>` properties that go straight into `BarSet.values` and
   `LineSeries.replace()`; the tooltip reads one day by index through `dayAt(index)`. They are chart data,
   not rows of a list view.
8. **Matches**: only `finished` matches up to the evaluation day. Minutes = `minute_off − minute_on`
   (a starter from 0). No `minute_off`, or a substitute without `minute_on`: did not play, adds nothing and
   is not missing RPE. Played without RPE: a missing match RPE.
9. **Missing RPE**: sessions (present) and matches (played) without RPE in the last 28 days, shown in the
   player panel.
10. **SpikeChartsView and Ctrl+Shift+G** stay; their removal (ADR-006) goes to `docs/BACKLOG.md` for P19.
11. **Chart** (spike done, no plan B): one `GraphsView` for the load (stacked `BarSeries` on a hidden
    `BarCategoryAxis`; `LineSeries` on their own `ValueAxis` per series (Qt 6.10+) from −0.5 to days − 0.5,
    so day i of a line sits on bar i; ticks every 7 days anchored on today with `tickAnchor`, labels through
    `labelDelegate`), and a second one below for the ratio over three `AreaSeries` bands. Matches are
    outlined bars (`barDelegate`, which gets `barLabel`/`barIndex`, not the `BarSet`), the chronic line is
    dashed (`strokeStyle`, Qt 6.11+). The tooltip finds the day from the pointer and `plotArea`, by index.
    The ratio axis has whole-number ticks so it stays readable in a low panel.
12. **Monotony** uses the population SD of the 7 daily loads (the week is the whole window, not a sample).
13. **Counted texts** put the number after a colon ("Days with RPE still needed: 11."), so they are correct
    for every number in both languages without plural forms (`FootballDSS_en.ts` stays unfinished,
    AGENTS §6).
14. **Load screen**: the selected player's panel hides the row of numbers (the table row shows them); the
    player card shows them. `PlayerCardView` gets a `navigateRequested` signal for the empty state of its
    Load tab, forwarded by one line in `SquadView.qml`.
