# P02 — Continuous integration on 3 platforms

**Branch:** `feature/P02-ci`
**Depends on:** P01 merged
**Owner agent:** any (can run in parallel with P03)

## Goal
Every push and PR builds and tests the app on Windows, macOS and Linux, so platform problems
show up the same day they are introduced, not at the defense.

## Scope — files you may create or change
- `.github/workflows/ci.yml`
- `.clang-format` (only if missing)
- `CMakePresets.json` (add a `ci` preset if needed)

## Out of scope
Application code. If CI exposes a code bug, report it; fix it only if it is a one-line build fix.

## Requirements
1. Triggers: `push` to any branch, `pull_request` to `main`.
2. Matrix: `windows-latest` (MSVC), `macos-latest` (Apple Clang), `ubuntu-latest` (GCC 14+).
3. Install Qt 6.12 with modules needed by P01 (incl. `qtgraphs`) using `jurplel/install-qt-action`
   (verify the action version supports 6.12; pin it). Enable caching.
4. Windows: set up the MSVC environment (e.g. `ilammy/msvc-dev-cmd`) and use Ninja.
5. Steps: configure → build → `ctest --output-on-failure` → `all_qmllint` →
   qml_smoke with `QT_QPA_PLATFORM=offscreen`.
6. Extra Linux job: `clang-format --dry-run --Werror` on `src/` and `tests/`.
7. Extra Linux job: Debug build with `-fsanitize=address,undefined` running `ctest`.
8. `fail-fast: false` so one platform failing doesn't hide the others.
9. Upload build logs as artifacts on failure.

## Done when
- [ ] All jobs green on the P01 code (or a clear report of what is red and why).
- [ ] README badge line prepared (text only, the human adds it).
- [ ] Total CI time per push under ~15 minutes with cache warm.

## Notes for the thesis
Screenshot of a green matrix; one sentence on why CI on 3 OS matters for a vibe-coded project.
