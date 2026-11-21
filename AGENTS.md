# AGENTS.md — FootballDSS

Single source of truth for every AI agent working in this repo. Claude authors the packets, Codex
reviews them, and Claude in Antigravity is the overflow when the Claude Code limit runs low.
`CLAUDE.md` and `GEMINI.md` (read by Antigravity) only say: "Read and follow AGENTS.md."
If a task packet contradicts this file, stop and ask the human.

Document map:
- `AGENTS.md` — HOW we build (rules, stack, conventions). Always read.
- `docs/SPEC.md` — WHAT we build (scope, screens, roles, data model, algorithms, import). Read the
  sections relevant to your packet; never invent tables, columns, thresholds or screens it already defines.
- `docs/tasks/Pxx-*.md` — WHAT NOW (the current packet).
- `docs/adr/` — WHY (architecture decisions).

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
| PIN hashing | `QPasswordDigestor` PBKDF2-SHA256 from Qt Network, linked into `fdss_services` only; no sockets (ADR-007) |
| Tests | GoogleTest via FetchContent; QML smoke test offscreen; Qt Quick Test for QML components (ADR-008) |
| Build | CMake + CMakePresets.json. NO qmake |
| Python | none in app code, CMake build or shipped package; CI-only tooling (e.g. aqtinstall) is allowed |

## 3. Build and verify (run ALL before saying "done")

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure            # unit + viewmodel tests + qml_smoke + qml_components
cmake --build --preset dev --target all_qmllint
clang-format-18 --dry-run --Werror $(git ls-files 'src/*.cpp' 'src/*.hpp' 'tests/*.cpp' 'tests/*.hpp')
```

On the human's machine use the gitignored preset `dev-local` instead of `dev` (it sets the Qt
path and GCC 14; build directory is still `build/dev`).
From P13b on, also run `ctest --preset dev-local-ci` (QML tests offscreen, GC stress, C locale as on CI).
Tests never compare user-visible text with an English literal: use keys/roles from the viewmodel or
`QCoreApplication::translate()`; the `dev-local-ci` preset runs with the C locale like CI.

**No display (agent sandboxes, CI).** Qt Graphs needs OpenGL even offscreen (ADR-006), so on Linux
without `$DISPLAY` run the unit tests with `ctest --preset dev -E qml_smoke` and the smoke test with:

```bash
xvfb-run -a -s '-screen 0 1920x1080x24' env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
    ./build/dev/tests/qml_smoke
```

If `xvfb-run` is not available, say so in the report and let the human or CI run `qml_smoke`.
Never "fix" `QRhiGles2: Failed to create context` in code; it means "no display", not a bug.

If any step fails, fix it or report exactly what fails. Never claim success without running these.

## 4. Architecture — dependencies point DOWN only

```
ui/ (QML)            -> viewmodels only
src/viewmodels/      -> services, core types (built into fdss_ui)
src/services/        -> data, core
src/data/            -> core
src/core/            -> C++ standard library ONLY (no Qt headers, ever)
```

CMake targets:
- `fdss_core` (static, no Qt) — domain, algorithms, pure parsing.
- `fdss_data` (static) — links `fdss_core`.
- `fdss_services` (static) — links `fdss_data`.
- `fdss_ui` (static, backing target of the QML module `FootballDSS`) — viewmodels, QML, theme,
  translations; links `fdss_services`.
- `FootballDSS` (thin executable) — `main.cpp` only; links `fdss_uiplugin`.
- `fdss_tests` (GoogleTest) — unit tests for core, data, services.
- `fdss_vm_tests` (GoogleTest) — viewmodel tests; links `fdss_ui`; registered in CTest.
- `qml_smoke` (test executable) — loads `Main.qml` and every view offscreen, fails on any QML
  warning; links `fdss_uiplugin`; registered in CTest.
- `qml_components` (Qt Quick Test executable, `tests/qml_components/tst_*.qml`) — behaviour of the
  `ui/components/` kit that `qml_smoke` cannot see; links `fdss_uiplugin`; registered in CTest (ADR-008).
- A test that links `fdss_ui` has the CTest label `ui` and uses a plain `add_test`: the sanitizer job
  builds only `fdss_tests` and runs `ctest -LE ui`.

- `core/`: domain types (`domain/`), DSS algorithms (`algorithms/`), pure parsing
  helpers (`parsing/`: CSV tokenizer, CP1250 decoder, header matching), `util/`.
- `data/`: `DatabaseManager`, `migrations/`, `repositories/`, `readers/` (CSV, XLSX,
  StatsBomb, `.tactic.json`).
- `services/`: one class per use case (`MatchDayService`, `ImportService`,
  `LoadService`, `LineupService`, `CapabilityService`, `AuthService`).
  Services own transactions.
- `viewmodels/`: QObject classes exposed to QML. Permissions are checked here AND in services.
- `ui/`: `Main.qml`, `theme/`, `components/`, `views/`. Presentation only.

## 5. C++ rules

- Errors: core/data/services return `fdss::core::Result<T>` = `std::expected<T, Error>`
  (`core/util/Error.hpp`). No exceptions across layer boundaries. `Error` =
  { `ErrorCode code`, `std::string detail` (technical, English, for logs),
  `std::string field` (optional: which input/column is wrong) }. Core, data and services never
  produce user-visible text; viewmodels turn `code` + `field` into a `qsTr()` message through
  `ErrorPresenter`. Never show `detail` to the user.
- Composition root: `fdss::viewmodels::ServiceRegistry::create()` builds the repositories and
  services once. `main.cpp` and `qml_smoke` only open the `DatabaseManager`, call `create()` and
  `install()` the result. A new service is added only in `ServiceRegistry` (one member, one accessor,
  one construction line), never in `main.cpp` or `qml_smoke`. Viewmodels obtain services from the
  registry in their constructor or `create()` factory. No other global state.
- Prefer `std::ranges` / views over raw loops when it stays readable. Use `std::format`.
  `std::mdspan` / `std::print` only if CI is green on all three platforms; otherwise `Grid2D`.
- Ownership: `std::unique_ptr` / values. Raw `new` only for QObjects with a parent, or in a
  QML singleton `create()` factory (the QML engine takes ownership).
- Every QObject subclass has `Q_OBJECT`. QML types use `QML_ELEMENT` / `QML_SINGLETON` /
  `QML_UNCREATABLE` with `qt_add_qml_module`. `qmlRegisterType` is FORBIDDEN.
- List data for QML: `QAbstractListModel` with named roles, or `QRangeModel` for simple cases.
  Small fixed or nested read-only lists (a few items) may be a `QVariantList`.
- Long work (parsing, import validation, recalculation) runs via `QtConcurrent`; database writes stay on
  the main-thread connection in one `Transaction`. The UI thread never blocks on parsing or computation.
  Checks of a form or a lineup (microseconds, not long work) run synchronously.
- Naming: `PascalCase` types, `camelCase` functions/variables, `m_` members, `kConstant`.
  Code identifiers and comments in English.
- `fdss_core` compiles with warnings as errors. Code formatted with the repo `.clang-format`.
- Files live in `QStandardPaths::AppDataLocation` (db, logs, backups). Never next to the exe.

- Tests: `fdss_tests` uses our own gtest `main` that creates a `QCoreApplication` and calls
  `QStandardPaths::setTestModeEnabled(true)`; tests never touch the real AppData folder.
  Viewmodel tests live in `fdss_vm_tests` (links `fdss_ui`).
- Repositories never store `QSqlDatabase` copies; they ask `DatabaseManager` on each call.

## 6. QML rules

- No business logic in QML. Bindings, simple UI state and calls to ViewModel methods only.
- Every user-visible string goes through `qsTr()`. Source strings in English; Polish lives in
  `i18n/FootballDSS_pl.ts`. Update `.ts` files when you add strings
  (`cmake --build --preset dev --target update_translations`, then fill every `unfinished` entry).
  Only the Polish file is translated; entries in `FootballDSS_en.ts` stay `unfinished` because
  English is the source language.
- `.ts` merge conflicts (parallel branches always touch them): never hand-merge XML. Take the
  `main` version, re-run `update_translations`, then translate the entries that came back
  `unfinished`.
- `AppState.can()` is evaluated when a view is created. The shell in `Main.qml` is loaded by a
  `Loader` in state `Ready`, and section views are recreated on every login and every user change
  (`AppState.sessionSerial`), so `visible: AppState.can("...")` is correct.
- Keyboard shortcuts inside a view use `enabled: root.visible` (all views stay loaded; only the
  current one is visible), otherwise identical shortcuts in two views block each other.
- Every user-facing shortcut is only an accelerator for a visible control (button, menu item,
  field). Nothing is reachable by keyboard shortcut alone. The shortcut is shown in that
  control's tooltip, e.g. "Add player (Ctrl+N)". Hidden developer views (Ctrl+Shift+G,
  Ctrl+Shift+K) are the only exception and are never mentioned in the UI.
- Dates between viewmodels and QML are ISO strings `yyyy-MM-dd` (times `HH:mm`), never JS `Date`
  (time-zone shifts). Formatting for display uses the app language, not the system locale.
- Build UI from `ui/components/` (DataTable, form fields, dialogs, toast…). Do not restyle raw
  Controls inside views; if a component is missing, add it to `ui/components/` and to the
  component gallery.
- Colors, fonts, spacing, radii and sizes come ONLY from the `Theme` singleton. No hex literals
  or magic numbers in views/components. Allowed keywords: "transparent".
- Hit targets: min 32 px; min 44 px on Match Day and the tactics board.
- Keyboard: every form and grid is fully usable with Tab/Enter; shortcuts per the spec.
- Empty states always tell the user what to do next, with a button when the current role can act;
  otherwise the text says who can (e.g. ask the coach).
- Destructive actions execute immediately with a 10 s "Undo" toast (confirm dialog only for bulk).
  Deleting an item that carries user-entered data (e.g. a session with attendance) also asks first; the
  undo toast stays.
- Status is never color-only: color + icon + word.
- Text that comes from user data, including tooltips, is always shown with `textFormat: Text.PlainText`
  (no rich text, no network requests from markup). A tooltip with such text is a `PlainToolTip`: the
  attached `ToolTip.text` guesses rich text.
- Reusable pieces go to `ui/components/`. One view per file in `ui/views/`.
- Use `required property` for delegates and `pragma ComponentBehavior: Bound` in files whose
  delegates reference outer ids.

## 7. Database rules

- Connection setup: `PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL;`
- `PRAGMA journal_mode = WAL` is requested on file databases; if SQLite refuses (for example on a
  network drive) the app continues in the default journal mode and logs a warning; a database that
  cannot be written shows the error screen.
- Schema changes ONLY via a new numbered migration file. Never edit an existing migration. An
  "existing migration" is one merged to `main`; migrations on an unmerged branch may still be edited.
- Values in SQL are ONLY bound with `prepare()` + `bindValue()`. Identifiers (table/column names) may
  be composed only from compile-time constants. Every statement goes through `execSql()`/`sqlError()`
  so transaction failures are tracked.
- Never use `OR ROLLBACK` / `ON CONFLICT ROLLBACK`: they end the whole transaction behind
  `Transaction`'s back.
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
- [ ] No new dependency, no new top-level source folder, no stack change without an ADR in
      `docs/adr/`. Tooling folders required by their tools (`.github/`, `.vscode/`, `scripts/`)
      are allowed.
- [ ] Portability fixes outside the packet scope (missing include, compiler-specific flag or
      warning) are allowed and listed in the report; behaviour changes outside scope are not.
- [ ] Short summary: what changed, how to test it manually, known limitations.

## 11. How to work

1. Read this file, the task packet (`docs/tasks/Pxx.md`, including its `Decisions` section) and
   the relevant sections of `docs/SPEC.md`. Decisions in the packet override the packet text.
2. Propose a short plan (files, classes, interfaces) and WAIT for approval.
3. Implement in small steps; build often.
4. Run section 3. Fix what fails.
5. Commit locally with a descriptive message. Report per section 10. Do not start the next
   packet on your own.

The human pushes, opens PRs and reads CI. Agents do not `git push`, create PRs, poll CI, run
`sudo`, write credential or authentication helpers, or work around a blocked command. If you need
any of that, ask.

When parallel agents work, the ViewModel header (properties, signals, invokables) is agreed
first; UI and backend are then built against it independently. Stay inside your directories.

## 12. Forbidden

Qt Widgets, `.ui` files, Qt Charts, `qmlRegisterType`, qmake, Python in app code, CMake build or
the shipped package, network access from the app (sockets, HTTP, telemetry), SQL with concatenated values (identifiers only from compile-time constants, see §7), hex colors in views, hardcoded user-visible strings, editing existing
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
