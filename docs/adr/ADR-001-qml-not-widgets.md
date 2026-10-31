# ADR-001: Qt Quick / QML instead of Qt Widgets

**Status:** accepted · 2026-10-31

## Context
Desktop app for coaches, Football-Manager-like layout, mouse + keyboard first, touch on the
tactics board and Match Day screen. UI is generated mostly by AI agents.

## Decision
Qt Quick / QML with Qt Quick Controls and a custom `Theme` singleton. No Qt Widgets, no `.ui` files.

## Alternatives
- Qt Widgets: mature tables and forms, but weak touch support, dated look without heavy QSS work,
  and `QGraphicsView` for the pitch adds a second rendering model.
- Mixed (Widgets + QQuickWidget): two UI technologies to maintain in one month.

## Consequences
- Touch handlers (DragHandler, TapHandler) available for free; GPU-rendered animations.
- Charts must use Qt Graphs (QML-native); Qt Charts is deprecated since 6.10.
- Strict separation enforced: logic in C++ ViewModels, QML only presents.
