# ADR-005: No Python; decision algorithms in C++

**Status:** accepted · 2026-10-31

## Context
The thesis focuses on a usable desktop app. ML-heavy opponent analysis is out of scope.

## Decision
All decision-support algorithms (sRPE/EWMA load monitoring, lineup validation, TOPSIS,
Hungarian assignment, opponent aggregations) are implemented in C++23 in `fdss_core`.

## Alternatives
- Embedded Python with pandas/scikit-learn: richer libraries, but a second runtime to ship on
  Windows and macOS, harder packaging, and no algorithm here needs it.

## Consequences
- Single-language codebase, simple packaging with `windeployqt` / `macdeployqt`.
- Algorithms are explainable and unit-tested against hand-computed values.
- Future ML work stays possible as a separate offline tool producing JSON the app imports.
