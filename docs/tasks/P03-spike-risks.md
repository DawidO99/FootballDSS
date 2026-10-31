# P03 — Risk spike: QXlsx, Qt Graphs, Windows package

**Branch:** `spike/P03-risks`
**Depends on:** P01 merged
**Owner agent:** any (can run in parallel with P02)

## Goal
Answer three yes/no questions now, while changing course is cheap. The result is a short
report plus whatever minimal code proves the answer. Spike code that is not reusable may be thrown away.

## Questions
1. **Does QXlsx build with Qt 6.12 on Windows (MSVC), Linux and macOS, and can it read a cell?**
   - Add QXlsx via `FetchContent`, pinned to the latest release tag; link only into `fdss_data`.
   - Known risk: distros needed a patch for Qt 6.10. If it fails, try the patch / newer commit; document what worked.
   - Prove it: test `tests/data/XlsxSpikeTest.cpp` reads `tests/fixtures/sample.xlsx` (create it with 3 rows: header + 2 players, Polish letters in names) and asserts values.
2. **Does Qt Graphs cover our charts?**
   - Hidden `ui/views/SpikeChartsView.qml` (not in the sidebar): a line chart with dates on X (14 points) and a grouped bar chart (3 series × 5 categories), themed with `Theme` tokens.
   - Report: what was easy, what is missing (labels, tooltips, date axis, styling).
3. **Does a portable Windows ZIP run on a clean machine?**
   - `scripts/package-windows.ps1`: Release build → `windeployqt --qmldir ui --release` → ZIP in `dist/`.
   - Human tests it on a Windows machine without Qt installed (or a VM). Document missing DLLs/plugins if any.

## Scope — files you may create or change
- `src/data/CMakeLists.txt`, `CMakeLists.txt` (FetchContent for QXlsx)
- `tests/data/XlsxSpikeTest.cpp`, `tests/fixtures/sample.xlsx`
- `ui/views/SpikeChartsView.qml`
- `scripts/package-windows.ps1`
- `docs/adr/ADR-006-spike-results.md`

## Done when
- [ ] ADR-006 answers each question with YES / NO / YES-WITH-WORKAROUND and the exact workaround.
- [ ] If QXlsx is a NO: ADR proposes plan B (CSV only + in-app "save Excel as CSV" help).
- [ ] If Qt Graphs is limited: ADR lists which charts become custom QML `Shape` components.

## Notes for the thesis
ADR-006 is ready-made material for the "technology risks" subsection.
