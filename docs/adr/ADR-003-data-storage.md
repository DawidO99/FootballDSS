# ADR-003: SQLite core + user-defined metrics table + JSON files

**Status:** accepted · 2026-10-31

## Context
Data ranges from "goals and minutes" (B-klasa) to thousands of events per match (pro event data).
Coaches must be able to track their own metrics without schema changes.

## Decision
Three layers:
1. Fixed relational core in SQLite (players, matches, appearances, sessions, attendance, availability).
2. User-defined metrics: `metric_definition` + narrow `metric_value` table (typed, validated, aggregatable in SQL).
3. JSON files for tree-shaped data: event data (StatsBomb) loaded on demand, `.tactic.json` for formations.

## Alternatives
- JSON column with "advanced stats" (original plan): flexible, but aggregations across matches and
  validation become painful; SQLite JSON functions don't validate types or ranges.
- Everything in JSON files: no relational integrity, no cheap queries.
- Event data in tables: thousands of rows per match for data that is read whole and rarely queried.

## Consequences
- Aggregations like "average of last 5 matches" stay one SQL query.
- Raw imported JSON is kept, so derived data can be recomputed.
- One `.sqlite` file per club: trivial backup and portability.
