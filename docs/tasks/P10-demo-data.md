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
