# P04b — Housekeeping after W0

**Branch:** `chore/P04b-housekeeping`
**Depends on:** main at `v0.1-W0` + the W1 docs commit (AGENTS.md, SPEC.md, packets)
**Owner agent:** Codex or Claude (small packet, effort Medium)

## Goal
Close the small findings from the P02/P03 reviews so W1 starts on a clean base.

## Scope — files you may create or change
- `scripts/package-windows.ps1`
- `ui/theme/Theme.qml` (only `ChartStyle.series3`)
- `README.md`
- `docs/adr/ADR-006-spike-results.md` (status line and Q1 table row only)

## Out of scope
Everything else, including `ci.yml` (already updated by the human) and `SpikeChartsView.qml`.

## Requirements
1. **Packaging with a different Qt kit.** `package-windows.ps1` must never mix Qt versions:
   pass `-DCMAKE_PREFIX_PATH=<QtDir>` explicitly to `cmake --preset release`, and if
   `build/release/CMakeCache.txt` exists with a `Qt6_DIR` outside `<QtDir>`, configure with
   `--fresh`. Print which Qt kit the build uses. Also add `sqldrivers\qsqlite.dll` to the list of
   required files, guarded so it is only required once the app links Qt6::Sql (P05 makes that true;
   detect it via the presence of `Qt6Sql.dll` in the staged folder). Same for `Qt6Network.dll`
   (P07a) — no extra plugin needed, just report it in the file list.
2. **Chart palette.** `series3` in the dark theme (`#836D03`) reads as muddy olive on the navy
   background. Pick a lighter ochre/gold that keeps the constraints from ADR-006 (adjacent-series
   ΔE ≥ 8 in protanopia/deuteranopia simulation, ≥ 15 ΔE from status colors and accent); report the
   new values with the numbers. The P03 validator is not in the repo: recompute with any throwaway
   tool outside the repo (Machado 2009 CVD simulation, ΔE in OKLab ×100), do not commit it. Light
   theme unchanged unless the check requires it.
3. **README.md** (English, short): what the app is (2 sentences), requirements (Qt 6.12 with
   Qt Graphs, CMake ≥ 3.28, Ninja, GCC 14 / MSVC 2022 / Apple Clang), build and test commands
   (presets), how to run, where the docs are (`AGENTS.md`, `docs/SPEC.md`, `docs/adr/`), license
   placeholder line "License: to be decided".
4. **ADR-006.** macOS manual CI run on `main` was green (build, unit tests incl. XlsxSpikeTest,
   qml_smoke): set Q1 to YES-WITH-WORKAROUND on all three platforms, status line "accepted".

## Done when
- [ ] Section 3 passes (no display: see §3 note).
- [ ] Script change reviewed by reading; it runs in the Package (Windows) workflow on the PR
      (the workflow triggers on changes to the script).
- [ ] New `series3` values and validator numbers in the report.

## Decisions (accepted plan)
