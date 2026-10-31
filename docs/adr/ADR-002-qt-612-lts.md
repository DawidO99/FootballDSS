# ADR-002: Qt 6.12 LTS and C++23

**Status:** accepted · 2026-10-31

## Context
Original plan named Qt 6.10/6.11 and C++23/26. The 6.11 series ended with 6.11.3 (Sep 2026).
Qt 6.12 LTS was released in autumn 2026 with 5 years of maintenance, QML hot reload and
improvements to QRangeModel.

## Decision
Qt 6.12 LTS. C++23 baseline, restricted to features that compile on MSVC, GCC and Apple Clang.
C++26 features are not used.

## Alternatives
- Qt 6.11: no further releases, no hot reload.
- C++26: incomplete support across the three compilers we must ship with.

## Consequences
- `std::expected`, `std::ranges`, `std::format` used freely.
- `std::mdspan` / `std::print` only when CI is green on all three platforms; otherwise our own `Grid2D` / `std::format`.
