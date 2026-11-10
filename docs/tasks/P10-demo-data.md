# P10 — Demo data: a fictional B-klasa team

**Branch:** `feature/P10-demo-data`
**Depends on:** P07a merged for the service; the wizard option needs P07b (merge after it)
**Owner agent:** Claude (effort Medium)

## Goal
One click in the first-run wizard creates a complete, believable half-season of a fictional
B-klasa team, so every later screen (and the defense) has data to show.

## Scope — files you may create or change
- `src/core/demo/DemoGenerator.hpp/.cpp` (pure, deterministic)
- `src/services/DemoDataService.hpp/.cpp` (persists the generated data in one transaction;
  + one member and one construction line in `ServiceRegistry`)
- `src/viewmodels/SetupWizardViewModel.*` and `ui/views/SetupWizardView.qml`
  (only: the "Load demo data" choice on the last step)
- `tests/core/DemoGeneratorTest.cpp`, `tests/services/*`, CMake lists, `i18n/*.ts`

## Out of scope
StatsBomb data (Pro demo, W3), dashboard, any other screen.

## Requirements
1. **Deterministic**: `generate(seed, startDate)` with a fixed seed gives identical output on all
   three platforms. Use our own small PRNG (e.g. SplitMix64/PCG) and own distributions, **not**
   `std::uniform_int_distribution` / `std::normal_distribution` (their output differs between
   standard libraries). Integer arithmetic only (no `std::log`, `sin`, `pow` in distributions).
   Determinism test: FNV-1a 64-bit over a canonical serialisation of the output, compared with a
   golden constant committed in the test (never `std::hash`).
2. **Content** (start = 12 weeks before today):
   - club and team names typed in the wizard (not overwritten), category senior, season of today;
   - 22 active + 2 archived players with fictional Polish names (name lists in the code), 2 GK
     among the active, realistic position spread, numbers 1–99 unique, birth years 1988–2007;
   - staff: the coach from the wizard + one Assistant and one Physio with PIN `1234`
     (shown on the summary step);
   - 3 training sessions per week (Tue/Thu/Fri style), RPE mostly 4–8, higher before matches lower
     after, attendance ≈ 85 % with excused/absent mixed in;
   - 1 match per weekend vs 8 fictional opponents, results, appearances with minutes (11 starters,
     up to 5 substitutes), goals and cards as match events;
   - 3 injuries (one still active today, one returning next week), 1 suspension, 1 illness.
3. **Wizard**: the summary step offers "Start empty" (default) or "Load demo data" (demo keeps the
   typed club and team names). Takes < 2 s; on error nothing is written (one transaction).

## Tests
Same seed → identical output (hash of the generated structs); invariants: unique shirt numbers,
exactly 2 GK among the 22 active, every appearance within match duration, minutes consistent with
substitutions, no RPE for absent players, injuries do not overlap attendance as `present`.

## Done when
- [ ] Fresh install → wizard → "Load demo data" → squad and calendar are full and plausible.
- [ ] Section 3 passes.

## Decisions (accepted plan)
1. **Atomicity**: `SetupService::completeSetup` runs first (its own transaction, the Coach logs in),
   then `DemoDataService::load` in a separate transaction. A demo failure writes nothing of the demo,
   the club stays empty and the wizard shows a toast. `SetupService` is not changed.
2. **Category and season**: while "Load demo data" is chosen, the category and season fields are
   locked and show what `create()` saves: `senior` and the season of today. Turning the demo off
   brings back the values chosen before.
3. **Upcoming match**: besides the finished matches, 1 `planned` match on the nearest match day on or
   after today, without a result.
4. **Substitution event**: `sub` has `player_id` = player coming on, `related_player_id` = player
   going off. A goal has `related_player_id` = the assisting player (no separate `assist` events).
5. **Staff PIN** `1234` for the demo Assistant and Physio; short Polish injury descriptions as data.
6. **Today and next week**: a session on today without attendance (whatever the weekday, also on a
   match day) and planned Tue/Thu/Fri sessions in the 7 days after today, without attendance.
7. **Load patterns for W2 alerts**, checked by the invariant tests from the dataset itself
   (load = RPE × minutes for sessions and match appearances):
   - one player with a load spike in the last 7 days: acute load ≈ 1.6 × the mean weekly load of
     the last 28 days (high RPE on every session + a full match);
   - one player after a closed injury with low load in the weeks before the return.
8. **Calendar shape**: matches on Sundays (`B-klasa`, 8 opponents, the second round reverses the
   venue), sessions Tue (75 min, after the match, lower RPE), Thu (90 min, highest RPE),
   Fri (60 min, `match_prep`).
9. **Exception to AGENTS §5 (list data for QML)**: `SetupWizardViewModel::demoStaff` is a
   `QVariantList`, not a list model: two fixed, read-only entries shown once on the summary step.
10. **Exception to AGENTS §5 (ErrorPresenter)**: a failed demo load shows its own `tr()` text
    ("Demo data could not be loaded. The club starts empty."). It reports the state the club is in,
    not the mapping of an error code; the technical detail goes to the log.
