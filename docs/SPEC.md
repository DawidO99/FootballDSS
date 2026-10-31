# FootballDSS — Product Specification

Reference for agents: WHAT we build. AGENTS.md says HOW. Task packets (`docs/tasks/`) say WHAT NOW.
If a packet conflicts with this spec, stop and ask the human. Schedule and thesis notes are kept elsewhere.

## 1. Product in one paragraph

Desktop decision-support system for football coaching staff. It helps with five decisions:
who is available, whom to rest, which lineup, how to approach the opponent, how the match went.
It works for a B-klasa amateur team entering data by hand and scales up to a professional club
importing event data. Features unlock automatically when the database contains the data they need.

## 2. Adaptive capability model

Every feature declares the data it requires. `CapabilityService` evaluates the database and puts each
feature in one of three states:
- **available** — shown in navigation;
- **hidden** — Pro modules until data exists (Settings toggle "Show advanced modules" reveals them);
- **locked with hint** — visible, explains what is missing ("Enter RPE for 9 more sessions to see the load trend").

| Capability | Condition (defaults, configurable) | Unlocks |
| --- | --- | --- |
| Squad | ≥ 11 players | Tactics board, lineup validator |
| Matches | ≥ 1 finished match | Match report, form, minutes |
| Load | RPE on ≥ 14 days; full acute:chronic after 28 days | Load alerts and trends |
| Ratings | Coach match ratings or attributes, or custom metrics | Lineup assistant (TOPSIS) |
| Opponents | ≥ 3 matches with opponent result, or league results import | Opponent profile |
| Event data | ≥ 1 imported event file (StatsBomb JSON) | Pro analysis: shot map, heatmaps |

## 3. Scope

**MUST**
1. First-run wizard: club, team, coach account, option "Load demo data".
2. Staff profiles with roles (Coach, Assistant, Physio, Analyst) and PIN.
3. Squad: table, player card, availability statuses (injury, illness, suspension, absence).
4. Calendar of matches and training sessions.
5. Training session: attendance + RPE for the whole squad in one keyboard-driven grid.
6. Match Day: clock, substitutions, goals, cards, automatic minutes; post-match report + rating 1–10.
7. Pre-match: tactics board (formations, drag & drop from bench), lineup validator, `.tactic.json`.
8. Load monitoring: sRPE, alerts, per-player chart.
9. CSV/XLSX import with column mapping and downloadable templates.
10. Custom metrics: definition and entry.
11. Dashboard: next match, availability, alerts, form.
12. PL/EN, dark and light theme.
13. Tests for algorithms and parsers; Windows portable package.

**SHOULD**: lineup assistant (TOPSIS + Hungarian), opponent profile, Pro analysis (StatsBomb shot map,
heatmap), player radar, PDF export (player card, match report), wellness questionnaire.

**COULD**: Poisson opponent strength model, pass maps, set pieces with arrows, macOS `.dmg`,
whole-club export/import.

**WON'T**: player app, cloud/sync, video, Python/ML, web scraping, payments, network access.

## 4. Screens

Window: left collapsible sidebar, top bar (team switcher, search Ctrl+K, days to next match, user,
language, theme), content area. Minimum window 1280×720 (fits a 1366×768 laptop screen with
taskbar); design target 1366×768, scales to 4K.

App start: no database → first-run wizard; database present → profile picker (staff tiles + PIN);
after login → main shell. Logout from the user chip in the top bar returns to the profile picker.

Shared UI kit (`ui/components/`, visible in the hidden component gallery, Ctrl+Shift+K):
`DataTable`, `FormField`, `TextInputField`, `NumberField`, `DateField`, `SelectField`,
`SearchField`, `AppDialog`, `ToastHost` (undo toast), `Card`, `AppTabs`, `IconButton`,
`SecondaryButton`, `DangerButton`, `SegmentedControl`, plus the existing `PrimaryButton`,
`EmptyState`, `StatusBadge`, `PageHeader`, `Icon`. Charts (W2+): `ChartLegend`, `ChartTooltip`
(tooltips identify a series by index, never by value).

| Screen | Purpose | Priority |
| --- | --- | --- |
| Profile picker | Staff tiles + PIN | MUST |
| First-run wizard | 4 steps: welcome + language, club and team, coach account, summary (start empty or load demo data; demo keeps the typed club/team names) | MUST |
| Dashboard | Next match, unavailable players, load alerts, last-5 form | MUST |
| Squad | Filterable/sortable table, column chooser; player card tabs Overview / Load / Matches / Notes | MUST |
| Calendar | List + week view of matches and sessions | MUST |
| Training session | Grid player × attendance, RPE, minutes | MUST |
| Pre-match | Opponent, tactics board, validator, assistant | MUST (assistant SHOULD) |
| Match Day | Fullscreen, large buttons, clock, subs, events, undo last action | MUST |
| Match report | Score, minutes, events, ratings, note | MUST |
| Load | Team table with green/yellow/red/grey status + player chart | MUST |
| Import | Wizard: file → preview → mapping → validation → commit | MUST |
| Settings | Club, staff and roles, custom metrics, position weights, language, theme, backups | MUST |
| Opponents | List, profile, notes | SHOULD |
| Pro analysis | Shot map, heatmaps, radar (only after event data import) | SHOULD |

## 5. Roles and permissions

Checked in ViewModels (hide/disable action) AND in services (reject).

| Permission | Coach | Assistant | Physio | Analyst |
| --- | --- | --- | --- | --- |
| Manage staff and settings (`ManageStaff`, `ManageSettings`) | yes | no | no | no |
| Edit squad (`EditSquad`) | yes | yes | no | no |
| Add/edit availability entries (`EditAvailability`) | yes | yes | yes | no |
| Read availability description (`ViewAvailabilityDetails`) | yes | no (type + dates only) | yes | no (type + dates only) |
| Calendar: matches and sessions (`EditCalendar`) | yes | yes | no | no |
| Training attendance and RPE (`EditTraining`) | yes | yes | yes | no |
| Lineup and Match Day (`EditLineup`, `RunMatchDay`) | yes | yes | no | no |
| Import and custom metrics (`Import`, `ManageMetrics`) | yes | yes | no | yes |
| View analyses (`ViewAnalysis`) | yes | yes | yes | yes |

The matrix lives in core (`core/domain/Permissions.hpp`, `isAllowed(Role, Permission)`) and is
unit-tested row by row against this table.

**Staff access (ADR-007).** Each staff user has a PIN of 4–8 digits, stored as
PBKDF2-SHA256 (`QPasswordDigestor`), 16-byte random salt, 120 000 iterations, all stored per user.
5 wrong PINs in a row lock that user for 30 seconds. The PIN separates staff members sharing one
laptop; it is not protection against someone who copies the database file (the file is not
encrypted, see RODO decision). Staff users are deactivated, never deleted (history keeps their
name). The last active Coach cannot be deactivated or demoted.

## 6. Data model (SQLite, one file per club)

Location: `QStandardPaths::AppDataLocation`/`club.sqlite`; backups in `…/backups/`
(`club-<yyyyMMdd-HHmmss>-<reason>.sqlite`, 10 newest kept). Schema = numbered migrations; the
first migration creates every table below at once so later packets do not reshape it. The table
below lists key columns; P05 writes the exact column names and types into this section in the same
commit as `0001_initial.sql`.

Enumerations (stored as lowercase text with a `CHECK` constraint; C++ enums in `core/domain`):
- Team category: `senior`, `u19`, `u17`, `u15`, `u13`, `u11`, `other`. Season text `2026/27`.
- Role: `coach`, `assistant`, `physio`, `analyst`.
- Position (14): `gk`, `rb`, `cb`, `lb`, `rwb`, `lwb`, `dm`, `cm`, `am`, `rm`, `lm`, `rw`, `lw`,
  `st`; groups: goalkeeper `gk`; defender `rb cb lb rwb lwb`; midfielder `dm cm am rm lm`;
  forward `rw lw st`.
- Preferred foot: `left`, `right`, `both`, `unknown`.
- Availability type: `injury`, `illness`, `suspension`, `absence`.
- Match status: `planned`, `live`, `finished`; venue: `home`, `away`, `neutral`.
- Session type: `training`, `match_prep`, `recovery`, `gym`, `other`.
- Attendance status: `present`, `absent`, `excused`, `injured`.

Rules: players are archived (`archived_at`), never deleted; shirt number 1–99, unique among
non-archived players of a team; birth year between 1950 and current year − 4; names 1–50
characters after trimming; dates stored as ISO text (`YYYY-MM-DD`, local, no time zone),
times of day as `HH:MM`, lockout timestamps as UTC ISO datetime; every foreign key indexed; `ON DELETE` restricted except for pure
child rows (attendance → session, appearance/match_event → match: cascade).

| Table | Key columns | Notes |
| --- | --- | --- |
| `club`, `team` | name, category (senior / U-19 / …), season | Many teams per club |
| `staff_user` | name, role, pin_hash, salt, iterations, failed_attempts, locked_until, active | |
| `player` | team, first/last name, shirt number, birth_year, primary position, secondary positions (comma list), foot, notes, archived_at | No PESEL, no contact data; "status" is derived from `availability` |
| `availability` | player, type, from, expected_to, actual_to, description | Description: Coach + Physio only |
| `opponent` | name, name_key (case-folded name, UNIQUE), notes | `name_key` = `QString::toCaseFolded()`, because SQLite `NOCASE` folds ASCII only |
| `match` | team, opponent, date, kickoff_time (nullable), venue, competition, goals_for, goals_against, status (planned/live/finished), formation | |
| `appearance` | match, player, started, minute_on, minute_off, position, rating 1–10, rpe | Minutes from Match Day |
| `match_event` | match, minute, type (goal/assist/yellow/red/sub/opponent_goal), player, related_player | |
| `training_session` | team, date, start_time (nullable), duration_min (15–240), type, note (nullable) | |
| `attendance` | session, player, status, rpe 0–10, minutes_override (0–240) | session load = rpe × minutes; rpe and minutes empty unless `present` |
| `wellness` | player, date, sleep, fatigue, soreness, stress, mood | SHOULD |
| `metric_definition` | key, name_pl, name_en, value_type, unit, min, max, aggregation, scope (match/training/player), higher_is_better | |
| `metric_value` | metric, player, match or session, date, value, import_id | |
| `player_attribute` | player, attribute, coach rating 1–10, date | TOPSIS input for amateurs |
| `lineup` | match or template, formation, tactic_json TEXT | |
| `event_data_file` | match, provider, path, hash | Loaded on demand |
| `import_log` | timestamp, file, hash, type, rows_ok, rows_failed, report_json | Undo by import_id |
| `schema_version` | version | |

`.tactic.json`: `{ "version": 1, "formation": "4-3-3", "slots": [{ "position": "GK", "x": 0.5, "y": 0.95, "playerId": 12 }] }`
— coordinates normalised 0–1 relative to the pitch (own goal at y = 1).

## 7. Algorithms (`src/core/algorithms/`)

### 7.1 Load monitoring (MUST)
- Session load (sRPE, Foster) = RPE (0–10) × minutes. Daily load = sum of sessions and matches that day; days without data = 0 only after the player has ≥ 1 entry in the window.
- EWMA: `ewma_t = λ·load_t + (1−λ)·ewma_{t−1}`, λ_acute = 2/(7+1), λ_chronic = 2/(28+1).
- Ratio = acute / chronic. Monotony (7 days) = mean / SD (SD = 0 → undefined). Strain = weekly load × monotony.
- Default thresholds: ratio > 1.5 red; 1.3–1.5 yellow; < 0.8 info "underloaded"; monotony > 2 yellow.
- Not enough data → grey "not enough data". Never a fake green.
- UI wording: "signal to talk to the player", never "injury risk".

### 7.2 Lineup validator (MUST)
Strategy pattern: each rule is a class `ILineupRule` returning issues with severity.
- Errors: starting XI ≠ 11, no goalkeeper, injured or suspended player, duplicate player.
- Warnings: red load status, out of nominal position, absent from last 3 sessions, cards at suspension threshold (threshold per competition, setting).

### 7.3 Lineup assistant (SHOULD)
- Per position: matrix players × criteria from available data (coach attributes, form = mean rating last 5, attendance last 28 days, load status as cost criterion; custom metrics / event data when present).
- Weights per position profile and style preset (e.g. "high press"). Criterion missing for all candidates → dropped, weights renormalised, UI says so.
- TOPSIS: vector normalisation, weighting, ideal/anti-ideal, closeness C ∈ [0,1].
- Assignment: Hungarian algorithm on cost = 1 − C over players × 11 slots; coach may pin players first.
- Output: proposed XI, top 3 alternatives per slot, per-criterion contribution ("why").

### 7.4 Opponent profile (SHOULD)
Record, goals for/against averages, goals by 15-minute bins, last-5 form, scouting notes.
Text hints generated by rules with minimum sample size, always showing n.

### 7.5 Pro analysis (SHOULD)
StatsBomb events (pitch 120 × 80): shot map (marker size = xG), player heatmap on a 12 × 8 grid,
radar = player percentiles within the team. StatsBomb attribution + logo shown wherever their data appears.

## 8. Import

1. Detect: content-based type, ≤ 20 MB; CSV/TXT, XLSX, JSON; XLS → "save as .xlsx".
2. Read: CSV encoding (UTF-8 ± BOM, fallback CP1250) and delimiter (`;` `,` tab), decimal comma; XLSX sheet choice, first non-empty row = header.
3. Map: auto-match headers by PL/EN synonyms; manual override; unknown column → "create custom metric"; mapping saved as template.
4. Validate per row: types, ranges (minutes 0–130, RPE 0–10), several date formats, player match by number or surname with disambiguation; report "row 14: minutes = 'abc', expected a number".
5. Commit: backup → one transaction (skip invalid rows or abort) → `import_log` with file hash (warn on re-import) → "Undo import".

Templates (XLSX + CSV) for squad, match and training are downloadable from the app.

Must-pass cases: video renamed to `.csv`; empty file and header-only file; Polish Excel CSV (CP1250, `;`,
decimal comma); Polish letters, quotes and commas in fields; duplicate players and unknown shirt numbers;
same file twice; corrupted XLSX and multi-sheet XLSX; truncated JSON and non-StatsBomb JSON;
database locked by another process; database deleted while running.

## 9. Demo data

- Team A: fictional B-klasa team from a deterministic generator (fixed seed): ~22 players, half a season of matches, sessions with RPE, a few injuries.
- Team B: StatsBomb Open Data match(es) for the Pro module, with attribution.
- All names in Team A are fictional.
