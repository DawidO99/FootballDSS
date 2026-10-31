# ADR-006: Technology risk spike — QXlsx, Qt Graphs, Windows package

**Status:** accepted for Q1 (Linux) and Q2 · Q1 on Windows/macOS and Q3 pending verification · 2026-10-31

## Context
Three risks could force a change of course late in the project: Excel import (QXlsx with Qt 6.12),
charts (Qt Graphs instead of the deprecated Qt Charts) and shipping to Windows machines without Qt.
Packet P03 answers each with YES / NO / YES-WITH-WORKAROUND, backed by minimal code.

| # | Question | Answer |
| --- | --- | --- |
| 1 | QXlsx builds with Qt 6.12 and reads a cell | **YES-WITH-WORKAROUND** on Linux (GCC 14.2). Windows (MSVC): pending the first CI run (`ci.yml` Windows job and `package-windows.yml`). macOS: pending a manual `ci.yml` run (macOS runs only on `workflow_dispatch`). |
| 2 | Qt Graphs covers our charts | **YES-WITH-WORKAROUND**: legend and tooltips are our own components; date axis needs two rules. |
| 3 | Portable Windows ZIP runs on a clean machine | **PENDING**: `scripts/package-windows.ps1` is ready; human test on Windows without Qt. |

## Q1 — QXlsx

**Setup.** `FetchContent` of QXlsx `v1.5.1.1` (latest tag, commit `8a13e1c`), static, linked PRIVATE into
`fdss_data` only (`src/data/CMakeLists.txt`). The spike test links `QXlsx::QXlsx` directly.

**Failure without workaround.** Configure stops in QXlsx's own CMakeLists:
```
Failed to find required Qt component "GuiPrivate".
Qt6GuiPrivate could not be found because dependency XKB could not be found.
```
Since Qt 6.10 private modules are separate CMake packages. QXlsx asks for `GuiPrivate`, and on Linux
`Qt6GuiPrivate` requires the XKB development headers (`libxkbcommon-dev`), which a Qt online-installer
setup does not have. QXlsx master (`4e82d6c`) still asks for `GuiPrivate`, so a newer commit does not help.

**Root cause.** QXlsx needs private API only for `QZipReader` / `QZipWriter`
(`private/qzipreader_p.h`, `private/qzipwriter_p.h`). In Qt 6.12 both live in **QtCore** (the symbols
are exported by `libQt6Core`, not `libQt6Gui`), so `GuiPrivate` is not needed at all.

**Workaround (exact).** `PATCH_COMMAND` in `src/data/CMakeLists.txt` runs a generated CMake script that
edits QXlsx's `QXlsx/CMakeLists.txt` (idempotent):
- `find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)` → `COMPONENTS CorePrivate`
- `Qt${QT_VERSION_MAJOR}::GuiPrivate` → `Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::CorePrivate`

Plus, around `FetchContent_MakeAvailable(QXlsx)`:
- `CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON`: `Qt6::CorePrivate` is found inside the QXlsx directory, and
  without it `qt_finalize_target` warns "CorePrivate target is mentioned as a dependency for
  QXlsx::QXlsx, but not declared" for every executable.
- `QT_NO_PRIVATE_MODULE_WARNING ON`: the warning says the build is tied to this exact Qt version. Accepted,
  because the Windows package ships the Qt it was built with. After a Qt upgrade, rebuild and run
  `XlsxSpikeTest`.
- `CXX_STANDARD 17` on the QXlsx target: QXlsx sets `CMAKE_CXX_STANDARD 17` as a cache variable with
  policy CMP0126 OLD, so it compiled as C++17 on a fresh configure and as C++23 on a re-configure.
  Pinned to 17, the standard upstream tests with. **Our targets stay on C++23**; this was checked in
  `compile_commands.json` on both a fresh and an existing build directory.
- `QT_EXCLUDE_FROM_TRANSLATION` on QXlsx: `qt_add_translations` scans every target, including QXlsx.

**Evidence.** `tests/data/XlsxSpikeTest.cpp`, 6 tests, all pass. `tests/fixtures/sample.xlsx` was saved by
LibreOffice Calc 24.2 from a UTF-8 CSV (header `Imię | Nazwisko | Numer | Rok urodzenia` + 2 players), so
QXlsx reads a file from a real spreadsheet application, not its own output. The tests check:
- sheet names and dimension (3 × 4);
- header and names with Polish letters (`Łukasz Żółkiewski`, `Józef Ćwiękała`);
- numeric cells typed as numbers (shirt number, birth year);
- a missing file and a non-ZIP file both make `load()` return `false` without a crash.

**Notes for the import packet.**
- `Document::load()` returns only `bool`. The reader must turn a failure into our own error
  ("file is not a valid .xlsx").
- A non-ZIP file makes Qt print `qWarning("QZip: not a zip file!")`. Harmless, but test harnesses that
  fail on warnings (like `qml_smoke`) must not open corrupted files.

**Open risk — Windows (MSVC).** `v1.5.1.1` still contains `#define stdext ::std` in `xlsxglobal.h`.
Upstream removed it in master (`4e82d6c`, 2 commits after the tag, not released yet) because it breaks MSVC
STL versions that still declare `namespace stdext` in `<iterator>` (errors `C2061`/`C2143`/`C2447` in
`<iterator>`). Following the "fallback only if the build fails" rule, the tag stays. If the Windows build
fails like that, set `GIT_TAG 4e82d6c0726dcc39020cc5491d0de77f787c18ef` and keep the patch, because master
still asks for `GuiPrivate`.

**Plan B (only if Windows or macOS end up NO).** CSV import only, plus in-app help
"File → Save As → CSV UTF-8" with screenshots for Excel and LibreOffice. The import pipeline already
handles CSV encodings and delimiters (SPEC §8).

## Q2 — Qt Graphs

**Setup.** Hidden view `ui/views/SpikeChartsView.qml`, not in the sidebar. `Ctrl+Shift+G` in `Main.qml`
shows it in place of the shell. It has two `GraphsView`s:
- a `LineSeries` on a `DateTimeAxis` (14 days of team load);
- a `BarSeries` of 3 `BarSet`s on a `BarCategoryAxis` (5 days × 3 position groups).

Both are styled through `GraphsTheme { theme: UserDefined }`, bound to `Theme` tokens. `qml_smoke` opens
the view in both languages and both themes and renders it, and `--screenshots <dir>` saves
`spike-charts-<lang>-<theme>.png`.

**Easy.**
- Both charts are declarative: about 260 lines of QML including the theme, legend and tooltips.
- `GraphsTheme` takes series colors (`seriesColors: Theme.chart.series`), grid, axis and label colors and
  font sizes from `Theme`. Dark/light switching is live through bindings.
- `qsTr()` works for categories, set labels and even the date format of the axis
  (`qsTr("MM/dd")` → Polish `dd.MM`), and the language switch is live.
- Each series exposes `legendData` (color + label per set), and `GraphsView` emits
  `hover`/`hoverEnter`/`hoverExit`.
- No QML warnings in any of the 4 language × theme runs.

**Missing or surprising, with the workaround used in the spike.**

| Area | Finding | Workaround |
| --- | --- | --- |
| Legend | No legend type in Qt Graphs 6.12 (2D). | `Repeater` over `series.legendData`, ~20 lines. Becomes a reusable `ChartLegend` component. |
| Tooltips | None built in. `hover` on a `LineSeries` reports the **interpolated point under the cursor** (e.g. 663.03), not a data point. On a `BarSeries` it reports the series name (not the `BarSet` label) and `(category index, value)`. | Line: snap `x` to the nearest day and read the value from the data. Bars: find the set whose value in that category matches; ambiguous if two sets have the same value, so the real component should use `barDelegate` with its own `HoverHandler`. Becomes a reusable `ChartTooltip`. |
| Date axis | Labels are in **UTC** by default (`timeZone` exists since 6.11). Ticks split `min..max` evenly with no anchoring to midnight, so with padding the labels fall between the points. `tickCount` counts intervals. `tickInterval` is scheduled for deprecation in 6.15. | Plot calendar days as UTC midnight. Set `min`/`max` exactly to the first/last data day and `tickCount = days − 1`. For 28/42-day ranges the ViewModel picks a coarser `tickCount`. |
| Software renderer | With `QSGRendererInterface::Software` (what `qml_smoke` uses) **grid lines, axis lines and ticks are not drawn**, silently. Series, bars, markers and labels are drawn. The same screenshot through RHI/OpenGL shows everything (checked with a throwaway run forcing `QT_QUICK_BACKEND=rhi`). | None needed for users: the app uses RHI (Direct3D 11 on Windows). `qml_smoke` screenshots lack the grid; take thesis screenshots from the running app. |
| Edge markers | Markers on the first/last day and on zero are cut by the plot area. | `clipPlotArea: false`. |
| Styling | Bar corner radius and gaps are not theme properties. | `barDelegate` if the default look is not enough. |
| Data | No model mapper like Qt Charts' `VXYModelMapper`. | Fill series from the ViewModel (`append`/`replace`); the `XYSeries.values` property was not tested. |
| Footprint | `Qt6Graphs` links `Qt6Quick3D`, `Quick3DRuntimeRender` and `ShaderTools` even for 2D charts (~14 MB of Quick 3D libraries on Linux). | Accepted; it shows up in the ZIP size. |

`GraphsView.useCanvasPainter` exists in the 6.12 type info but is not documented. Not used.

**Charts that become custom QML `Shape` components.** None beyond those AGENTS.md already plans: the
radar, the pitch / shot map and the heatmaps. Line and bar charts (load per player, goals by 15-minute
bins, form) stay on Qt Graphs with our `ChartLegend` and `ChartTooltip`. Not tested here: colored
threshold bands behind the acute:chronic ratio line (`AreaSeries` or a `Shape` overlay); decide in the
load-monitoring packet.

**Chart palette (`Theme.chart`).** Five series colors per mode (blue, magenta, olive, purple, teal), in a
fixed order. The hues avoid the status colors (green/amber/red/sky) and the accent. They were picked by
an OKLCH search and checked with the data-viz validator (OKLab ΔE × 100, Machado 2009 protan/deutan
simulation, severity 1.0), against `Theme.surface`:

| Check | Light (`#FFFFFF`) | Dark (`#151D2E`) |
| --- | --- | --- |
| Worst adjacent pair, CVD (target ≥ 8) | 22.5 | 15.3 |
| Worst adjacent pair, normal vision (floor ≥ 15) | 30.9 | 29.3 |
| First 3 slots, all pairs (grouped bars) | pass | pass |
| Contrast vs surface ≥ 3:1 | all 5 | all 5 |
| Closest series vs any status/accent color, normal vision | 15.1 | 15.2 |

## Q3 — Portable Windows ZIP

**Script.** Run it locally with
`powershell -ExecutionPolicy Bypass -File scripts\package-windows.ps1 -QtDir C:\Qt\6.12.0\msvc2022_64`
(Windows blocks `.\script.ps1` under its default execution policy). It does the following:
1. Enters the MSVC x64 environment via `vswhere` + `Enter-VsDevShell`.
2. Runs `cmake --preset release` and builds `FootballDSS`.
3. Copies the exe to `dist/FootballDSS-<version>-win64/`.
4. Runs `windeployqt --release --qmldir ui --no-compiler-runtime`.
5. Copies the MSVC runtime DLLs (`Microsoft.VC*.CRT`) next to the exe. windeployqt would otherwise add the
   `vc_redist` installer, which a portable ZIP cannot rely on.
6. Fails if key files are missing (Qt core DLLs, `platforms\qwindows.dll`, QtQuick Controls/Layouts,
   `qml\QtGraphs`, `vcruntime140*.dll`, `msvcp140.dll`), then zips.

The script runs in Windows PowerShell 5.1 and PowerShell 7. It was **not executed locally**: the dev
machine is Linux without PowerShell. Its first run is the CI workflow below, which also answers Q1 for MSVC.

**CI workflow.** `.github/workflows/package-windows.yml` runs the script on `windows-latest`:
- MSVC set up by `ilammy/msvc-dev-cmd`;
- Qt 6.12 installed exactly as in `ci.yml` (same modules, aqt commit, pinned action SHAs and Qt cache);
- Release build and packaging in Windows PowerShell 5.1 with `-ExecutionPolicy Bypass`;
- the ZIP uploaded as-is (`archive: false`) and kept for 7 days.

It runs on manual `workflow_dispatch` and on pull requests to `main` that change the script, the
workflow, `CMakeLists.txt` or `src/data/CMakeLists.txt`. A newer run on the same ref cancels the older
one. `ci.yml` is not changed.

**How to run the workflow and download the ZIP.**
1. On GitHub, open the repository → **Actions** tab → **Package (Windows)** in the list of workflows
   on the left.
2. Click **Run workflow** (on the right, above the list of runs), pick the branch under
   **Use workflow from** and confirm with the green **Run workflow** button. The new run shows up in
   the list after a few seconds.
   GitHub offers **Run workflow** only for workflows that already exist on the default branch. Until
   P03 is merged into `main`, use the run that the P03 pull request starts automatically:
   pull request → **Checks** tab → **Package (Windows)**.
3. Wait for the green check. The first run without the Qt cache takes noticeably longer than later runs.
4. Open the run → **Summary** → **Artifacts** section at the bottom → click
   **`FootballDSS-<version>-win64.zip`**. The browser downloads the ZIP itself, not a ZIP wrapped
   in another ZIP. Downloading requires being signed in. The artifact expires after 7 days; after that,
   start a new run.
5. If the run fails, the log of the **Build and package** step shows the script output step by step.
   The `logs-package-windows` artifact holds `CMakeCache.txt`, the CMake configure log and
   `aqtinstall.log`.

**Known gaps.** `qsqlite.dll` is not deployed yet because nothing links `Qt6::Sql`; check again after the
database packet. The Universal CRT is part of Windows 10/11.

**Checklist for the human test (fill in):**
- [ ] Machine: Windows 10/11 x64 without Qt, Visual Studio or "Microsoft Visual C++ 2015-2022 Redistributable" (VM is fine).
- [ ] Script output: ZIP size ___ MB; Windows build of QXlsx OK / failed with `C2061` in `<iterator>` (→ pin `4e82d6c`, see Q1).
- [ ] Unzip to a path with spaces and Polish letters (e.g. `C:\Users\Test\Pulpit\Próba FootballDSS`).
- [ ] App starts; switching PL/EN and the dark/light theme works.
- [ ] `Ctrl+Shift+G`: both charts drawn with grid lines; tooltip on hover over a point and a bar.
- [ ] If it does not start: name of the missing DLL from the error dialog; run once with `set QT_DEBUG_PLUGINS=1` from `cmd` and note missing plugins.
- [ ] Result: YES / NO / YES-WITH-WORKAROUND, and what was added to the script.

## Alternatives
- Raise the system requirement instead of patching (`libxkbcommon-dev` on every Linux machine and CI
  runner): it fixes only Linux and keeps an unneeded private dependency on QtGui.
- Own CMake target compiled from the QXlsx sources: more control, but a fork of the upstream build logic.
- Qt Charts: deprecated since Qt 6.10 (ADR-001).
- All charts as custom `Shape` components: full control, but axes, ticks and labels written from scratch.

## Consequences
- QXlsx stays (data layer only); the patch lives in `src/data/CMakeLists.txt`. Drop it once upstream
  links `CorePrivate`, and drop the C++17 pin once upstream stops writing `CMAKE_CXX_STANDARD` to the cache.
- Charts use Qt Graphs, with our own `ChartLegend` and `ChartTooltip` components and the two date-axis
  rules above. Radar, pitch and heatmaps stay custom `Shape` (as planned).
- `Theme` gains a validated `chart` group (5 series colors, line width, marker size).
- `qml_smoke` renders the spike view in every language × theme. The spike view and `Ctrl+Shift+G` are
  removed when the real load chart exists.
