# P01 — Application skeleton

**Branch:** `feature/P01-skeleton`
**Depends on:** P04 (repo files in place)
**Owner agent:** strongest reasoning model (this sets patterns every later packet copies)

## Goal
A running Qt 6.12 Quick app with the final folder layout, the 5 CMake targets, a themed
Football-Manager-style shell (sidebar + top bar + content area), placeholder views,
and live PL/EN switching. No database yet.

## Scope — files you may create or change
- `CMakeLists.txt`, `CMakePresets.json`
- `src/main.cpp`
- `src/core/**` (only a placeholder so the target builds, e.g. `core/util/Version.hpp/.cpp`)
- `src/data/CMakeLists.txt`, `src/services/CMakeLists.txt` (empty static libs with one placeholder file each)
- `src/viewmodels/AppSettings.hpp/.cpp`, `src/viewmodels/LanguageManager.hpp/.cpp`, `src/viewmodels/NavigationModel.hpp/.cpp`
- `ui/Main.qml`, `ui/theme/Theme.qml`, `ui/components/*.qml`, `ui/views/*.qml`
- `i18n/FootballDSS_pl.ts`, `i18n/FootballDSS_en.ts`
- `tests/CMakeLists.txt`, `tests/core/VersionTest.cpp`, `tests/qml_smoke/main.cpp`

## Out of scope
Database, any real feature, CI (P02), QXlsx and packaging (P03).

## Requirements
1. **CMake**
   - `cmake_minimum_required(VERSION 3.28)`, `CMAKE_CXX_STANDARD 23`, required, no extensions.
   - `find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Sql Concurrent Graphs LinguistTools)`
   - `qt_standard_project_setup(REQUIRES 6.12)`
   - Targets exactly as AGENTS.md section 4. `fdss_core` must NOT link any Qt target.
   - `fdss_core` with warnings as errors (`/W4 /WX` on MSVC, `-Wall -Wextra -Wpedantic -Werror` elsewhere).
   - QML module: `qt_add_qml_module(FootballDSS URI FootballDSS VERSION 1.0 QML_FILES ... )`.
   - Translations: `qt_add_translations(FootballDSS TS_FILES i18n/FootballDSS_pl.ts i18n/FootballDSS_en.ts)`.
   - GoogleTest via `FetchContent` (pinned tag), `enable_testing()`, `gtest_discover_tests`.
2. **CMakePresets.json**: preset `dev` (Ninja, Debug, `build/dev`), `release` (Ninja, Release, `build/release`), matching build and test presets. Qt path comes from `CMAKE_PREFIX_PATH` env, never hardcoded.
3. **Theme.qml** (`pragma Singleton`): dark and light palettes switched by `Theme.dark`.
   Tokens: `bg`, `surface`, `surfaceAlt`, `border`, `text`, `textMuted`, `accent`, `accentText`,
   `ok`, `warn`, `danger`, `info`; spacing `xs 4, s 8, m 12, l 16, xl 24, xxl 32`;
   radius `s 4, m 8, l 12`; font sizes `caption 12, body 14, title 18, h1 24, display 32`;
   `hitMin 32`, `hitTouch 44`. Dark default inspired by Football Manager (deep navy/graphite, one accent).
4. **Shell (Main.qml)**
   - `ApplicationWindow`, min size 1366×768, default 1600×900.
   - Left sidebar (collapsible, 220 px / 64 px): Dashboard, Squad, Calendar, Training, Pre-match,
     Match Day, Load, Import, Settings — icon + label, keyboard navigable, active item highlighted.
   - Top bar: team name placeholder, search field (Ctrl+K focuses it), language toggle PL/EN,
     theme toggle, user chip placeholder.
   - Content: `StackLayout` or `StackView` driven by `NavigationModel` (C++ `QAbstractListModel`,
     `QML_ELEMENT`, roles: `key`, `title`, `iconName`).
5. **Placeholder views** in `ui/views/` (one file each): title + `EmptyState` component
   ("Nothing here yet" + description + button). Reusable components: `EmptyState.qml`,
   `SidebarButton.qml`, `TopBar.qml`, `StatusBadge.qml` (color + icon + text).
6. **LanguageManager** (`QML_SINGLETON`): `Q_PROPERTY(QString language ...)`, `Q_INVOKABLE setLanguage("pl"|"en")`,
   loads translator, calls `QQmlEngine::retranslate()`, persists in `QSettings`. Default: system locale if pl, else en.
7. **AppSettings** (`QML_SINGLETON`): `darkTheme` bool persisted in `QSettings`.
8. All user-visible strings via `qsTr()`; Polish translations filled in `FootballDSS_pl.ts`.
9. **qml_smoke test**: executable that creates the engine, loads `Main.qml`, instantiates every view
   in `ui/views/`, and exits non-zero if any QML warning/error was emitted (install a message handler). Registered in CTest.

## Done when
- [ ] `cmake --preset dev && cmake --build --preset dev` works on Linux.
- [ ] App starts; every sidebar item switches view; sidebar collapses.
- [ ] PL/EN switches instantly without restart; theme switches instantly; both survive restart.
- [ ] No hex colors outside `Theme.qml` (`grep -rn "#[0-9a-fA-F]\{6\}" ui/ --include=*.qml | grep -v theme/` is empty).
- [ ] `ctest --preset dev` passes (Version test + qml_smoke).
- [ ] `all_qmllint` passes without warnings.

## Notes for the thesis
Screenshot the empty shell in dark/light × PL/EN (4 images).
