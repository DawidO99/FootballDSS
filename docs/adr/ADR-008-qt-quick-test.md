# ADR-008: Qt Quick Test for QML component tests

**Status:** accepted · 2026-11-01

## Context
The UI kit (`ui/components/`) has behaviour that can break without a QML warning: a binding on an
input property that does not survive a user edit, a limit change that does not validate again, a
language switch that sends a view back to its first tab. `qml_smoke` loads the views and fails on
warnings, so it sees none of this. GoogleTest (AGENTS.md section 2) drives C++, not key presses in a
QML scene. The review of P06b found this kind of bug in four components.

## Decision
- Component tests use Qt Quick Test: `TestCase` in `tests/qml_components/tst_*.qml`, run by the
  `qml_components` executable and registered in CTest.
- The target imports the app's real `FootballDSS` module through `fdss_uiplugin` and uses its own
  application name, so tests never read or write the real user settings.
- `Qt6::QuickTest` is linked into this test target only.
- GoogleTest stays the tool for core, data, services and viewmodels. `qml_smoke` stays the check
  that every view loads without warnings.

## Alternatives
- GoogleTest with `QQmlComponent`: no new test framework, but every key press, property read and
  wait is C++ plumbing, and the event loop and window have to be managed by hand.
- Growing `qml_smoke`: it would stop being a fast "does everything load" check, and one executable
  reports one pass or fail with no per-case result.
- Moving the logic to C++ and testing it with GoogleTest: the bugs are in the QML wiring itself, and
  input components hold no business logic (AGENTS.md section 6).
- Commercial GUI test tools (Squish): cost and a heavy setup for a thesis project.
- Manual tests only: the same regressions would have to be found again by hand on every change.

## Consequences
- Qt Quick Test ships with `qtdeclarative`, so CI needs no new aqt module. The Windows package is not
  affected: `scripts/` builds only the `FootballDSS` target and `windeployqt` deploys that executable.
- Display on Linux: CI runs `qml_components` on Xvfb with Mesa llvmpipe and the `xcb` platform, like
  `qml_smoke`, so key handling and focus are exercised on a real window system. CTest forces the
  offscreen platform, so the Linux CI job excludes the target from CTest and runs it in its own step.
- Unlike `qml_smoke`, the target does not strictly need a display today: it renders with the software
  backend and instantiates no Qt Graphs, and it passes offscreen on a machine without one. That stops
  being true as soon as a test instantiates a Qt Graphs view (ADR-006); such a test then needs
  `xvfb-run` everywhere that has no display.
- The sanitizer CI job builds only `fdss_tests`, so it excludes this target like `qml_smoke`.
- `TestCase` is invisible by default and the Tab chain skips invisible items, so tests of Tab order
  set `visible: true`. Tab order tests are skipped on platforms where the system setting keeps
  buttons out of the Tab chain (macOS can do that).
