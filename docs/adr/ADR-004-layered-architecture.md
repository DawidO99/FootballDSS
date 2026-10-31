# ADR-004: Layered architecture with a Qt-free core

**Status:** accepted · 2026-10-31

## Context
Code is written mostly by several AI agents in parallel. Without hard boundaries it drifts:
logic leaks into QML, SQL into view code, Qt types into algorithms.

## Decision
Five layers with dependencies pointing down only: UI (QML) → ViewModels → Services → Data → Core.
`fdss_core` is plain C++23 and must not include any Qt header. Each layer is its own CMake target,
so a wrong dependency fails at link time instead of review time.

## Alternatives
- Classic MVC with Qt models directly over SQL (`QSqlTableModel`): fast to start, but couples UI to
  schema and makes algorithms untestable without a database.
- Single target: no enforcement.

## Consequences
- Algorithms are unit-tested in milliseconds without a window or database.
- Some conversion code between `std::string` and `QString` at the data/service boundary.
- Agents can work in parallel on separate layers against an agreed ViewModel interface.
