# AGENTS.md — FootballDSS

Single source of truth for every AI agent working in this repo (Claude, Codex, Gemini).
`CLAUDE.md` and `GEMINI.md` only say: "Read and follow AGENTS.md."
If a task packet contradicts this file, stop and ask the human.

## 1. Project

FootballDSS is a desktop decision-support system for football coaches (engineering thesis).
- Targets: Windows 10/11 (primary), macOS (secondary). Linux is the dev machine.
- Desktop-first UI in the spirit of Football Manager: mouse + keyboard, large screen.
  Touch must work on the tactics board and Match Day screen, but is optional elsewhere.
- One app for every level: B-klasa (amateur) to professional. Features unlock based on
  data available in the database (`CapabilityService`). No paid tiers.
- Offline, local SQLite database, no cloud, no sync, no player app.

## 2. Fixed stack — do not change without an ADR

| Area | Choice |
| --- | --- |
| Language | C++23 (only features that compile on MSVC, GCC and Apple Clang) |
| Framework | Qt 6.12 LTS |
| UI | Qt Quick / QML + Qt Quick Controls, custom theme. NO Qt Widgets, NO `.ui` files |
| Charts | Qt Graphs. Radar, pitch and heatmaps are custom QML (`Shape`). NO Qt Charts (deprecated) |
| Database | SQLite via QtSql |
| JSON | QJsonDocument / QJsonObject (data layer only) |
| Excel | QXlsx (data layer only) |
| Tests | GoogleTest via FetchContent; QML smoke test offscreen |
| Build | CMake + CMakePresets.json. NO qmake |
| Python | none |

## 3. Build and verify (run ALL before saying "done")

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
cmake --build --preset dev --target all_qmllint
QT_QPA_PLATFORM=offscreen ./build/dev/tests/qml_smoke   # loads every view, fails on QML warnings
```

If any step fails, fix it or report exactly what fails. Never claim success without running these.

## 4. Architecture — dependencies point DOWN only

```
ui/ (QML)            -> viewmodels only
src/viewmodels/      -> services, core types
src/services/        -> data, core
src/data/            -> core
src/core/            -> C++ standard library ONLY (no Qt headers, ever)
```

CMake targets: `fdss_core` (static, no Qt), `fdss_data` (static), `fdss_services` (static),
`FootballDSS` (app + QML module), `fdss_tests`.

- `core/`: domain types (`domain/`), DSS algorithms (`algorithms/`), pure parsing
  helpers (`parsing/`: CSV tokenizer, CP1250 decoder, header matching), `util/`.
- `data/`: `DatabaseManager`, `migrations/`, `repositories/`, `readers/` (CSV, XLSX,
  StatsBomb, `.tactic.json`).
- `services/`: one class per use case (`MatchDayService`, `ImportService`,
  `LoadMonitoringService`, `LineupService`, `CapabilityService`, `AuthService`).
  Services own transactions.
- `viewmodels/`: QObject classes exposed to QML. Permissions are checked here AND in services.
- `ui/`: `Main.qml`, `theme/`, `components/`, `views/`. Presentation only.

## 5. C++ rules

- Errors: core/data/services return `std::expected<T, Error>`. No exceptions across layer
  boundaries. `Error` = { code enum, translatable message key, technical detail }.
- Prefer `std::ranges` / views over raw loops when it stays readable. Use `std::format`.
  `std::mdspan` / `std::print` only if CI is green on all three platforms; otherwise `Grid2D`.
- Ownership: `std::unique_ptr` / values. Raw `new` only for QObjects with a parent.
- Every QObject subclass has `Q_OBJECT`. QML types use `QML_ELEMENT` / `QML_SINGLETON` /
  `QML_UNCREATABLE` with `qt_add_qml_module`. `qmlRegisterType` is FORBIDDEN.
- List data for QML: `QAbstractListModel` with named roles, or `QRangeModel` for simple cases.
- Long work (import, recalculation over many players) runs via `QtConcurrent`;
  the UI thread never blocks on it.
- Naming: `PascalCase` types, `camelCase` functions/variables, `m_` members, `kConstant`.
  Code identifiers and comments in English.
- `fdss_core` compiles with warnings as errors. Code formatted with the repo `.clang-format`.
- Files live in `QStandardPaths::AppDataLocation` (db, logs, backups). Never next to the exe.

## 6. QML rules

- No business logic in QML. Bindings, simple UI state and calls to ViewModel methods only.
- Every user-visible string goes through `qsTr()`. Source strings in English; Polish lives in
  `i18n/FootballDSS_pl.ts`. Update `.ts` files when you add strings.
- Colors, fonts, spacing, radii come ONLY from the `Theme` singleton. No hex literals in views.
- Hit targets: min 32 px; min 44 px on Match Day and the tactics board.
- Keyboard: every form and grid is fully usable with Tab/Enter; shortcuts per the spec.
- Empty states always tell the user what to do next, with a button.
- Destructive actions execute immediately with a 10 s "Undo" toast (confirm dialog only for bulk).
- Status is never color-only: color + icon + word.
- Reusable pieces go to `ui/components/`. One view per file in `ui/views/`.
- Use `required property` for delegates; no unqualified access to outer ids from delegates.

## 7. Database rules

- Connection setup: `PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;`
- Schema changes ONLY via a new numbered migration file. Never edit an existing migration.
- Queries ONLY with `prepare()` + `bindValue()`. String-concatenated SQL is forbidden.
- Multi-row writes run in a transaction. Automatic DB backup before every import and migration
  (keep the 10 newest).
- No personal data beyond the schema: birth YEAR only, no PESEL, no addresses, no phone numbers.
- Injury details are readable only by roles Coach and Physio.

## 8. Import rules (users WILL upload garbage)

- Detect type by content (`QMimeDatabase`), not extension. Size limit 20 MB.
- Allowed: CSV/TXT, XLSX, JSON. XLS -> message "save as .xlsx".
- CSV: detect encoding (UTF-8 with/without BOM, fallback CP1250) and delimiter (`;` `,` tab).
  Decimal comma must work.
- Flow: detect -> read -> preview + column mapping -> per-row validation report -> commit.
  Nothing is written before commit. Commit = backup + one transaction + `import_log` row.
- Every import is undoable via `import_id`. Warn when a file hash was already imported.
- Error messages name the row, the column, the bad value and what was expected.

## 9. DSS algorithm rules

- Algorithms live in `core/algorithms/`, are pure functions/classes, and have unit tests with
  hand-computed expected values.
- Every recommendation returns its explanation (which inputs/criteria drove it).
- Insufficient data -> explicit "not enough data" state, never a fake green/OK.
- Wording in UI: load alerts are "signals to talk to the player", never "injury prediction".
- Thresholds are configurable settings with documented defaults.

## 10. Definition of Done for a task packet

- [ ] Only files listed in the packet (plus tests, `.ts`, CMakeLists) were changed.
- [ ] All commands from section 3 pass.
- [ ] New logic in core/data/services has tests.
- [ ] New strings are in `qsTr()` and added to both `.ts` files.
- [ ] No new dependency, no new top-level folder, no stack change without an ADR in `docs/adr/`.
- [ ] Short summary: what changed, how to test it manually, known limitations.

## 11. How to work

1. Read this file and the task packet (`docs/tasks/Pxx.md`).
2. Propose a short plan (files, classes, interfaces) and WAIT for approval.
3. Implement in small steps; build often.
4. Run section 3. Fix what fails.
5. Report per section 10. Do not start the next packet on your own.

When parallel agents work, the ViewModel header (properties, signals, invokables) is agreed
first; UI and backend are then built against it independently. Stay inside your directories.

## 12. Forbidden

Qt Widgets, `.ui` files, Qt Charts, `qmlRegisterType`, qmake, Python, network access from the
app, string-built SQL, hex colors in views, hardcoded user-visible strings, editing existing
migrations, silently swallowing errors, deleting or rewriting tests to make them pass.

## 13. Glossary (PL <-> EN)

| PL | EN / code | Meaning |
| --- | --- | --- |
| Kadra | Squad | Players of a team |
| Dostępność | Availability | Injury / illness / suspension / absence |
| RPE | RPE | Rate of perceived exertion, 0–10, reported by the player |
| sRPE | sessionLoad | RPE × minutes |
| Obciążenie ostre / przewlekłe | acuteLoad / chronicLoad | EWMA 7 days / 28 days |
| Monotonia, strain | monotony, strain | Foster: weekly mean / SD; weekly load × monotony |
| Tryb meczowy | MatchDay | Live match screen: clock, subs, events, minutes |
| Tablica taktyczna | TacticsBoard | Pitch with draggable player tokens |
| Asystent składu | LineupAssistant | TOPSIS + Hungarian assignment |
| Metryka własna | CustomMetric | User-defined metric (`metric_definition`) |
| Moduł Pro | ProAnalysis | Features unlocked by event data (StatsBomb) |
| Sztab | Staff | Users: Coach, Assistant, Physio, Analyst |
