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
| View squad, calendar and training sheet (every role) | yes | yes | yes | yes |
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
(`club-<yyyyMMdd-HHmmss>-<reason>.sqlite`, `-2`, `-3`… for further backups of the same second, 10
newest kept).
Schema = numbered migrations; the first migration (`src/data/migrations/0001_initial.sql`) creates
every table below at once so later packets do not reshape it. The tables below list the exact column
names and types of that migration, plus what later migrations added: `0002_match_live.sql` (P13) adds
`match_live`, `match_clock_step`, `match_note`, `match_event.added_time` and `match_event.seq`;
`0003_match_tactics.sql` (P13b) adds `match_tactic_change`; `0004_match_report.sql` (P14) adds
`match.report_note`.

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
- Match event type: `goal`, `assist`, `yellow`, `red`, `sub`, `opponent_goal`.
- Metric value type: `integer`, `decimal`, `boolean`. Aggregation: `sum`, `avg`, `min`, `max`, `last`.
  Scope: `match`, `training`, `player`.

`import_log.type`, `event_data_file.provider` and `player_attribute.attribute` are open text (only
"not empty" is checked): those features are not designed yet, and SQLite changes a `CHECK` only by
rebuilding the table.

Rules: players are archived (`archived_at`), never deleted, except players created by an import when
that import is undone and nothing references them (P16).
Shirt number 1–99 (optional), unique among
non-archived players of a team; birth year (optional) between 1950 and current year − 4 (`CHECK` holds
only the lower bound because it cannot use the current date; the core validation holds both); player
first and last names and staff names 1–50 characters after trimming, club, team and opponent names
1–100 (`CHECK` holds the upper bound only for player names; the core validation holds all of them);
dates (including `archived_at`) stored as ISO text (`YYYY-MM-DD`,
local, no time zone), times of day as `HH:MM`, lockout and `import_log` timestamps as UTC ISO datetime;
wellness scores 1–5; every foreign key indexed; `ON DELETE RESTRICT` except for pure child rows
(attendance → session, appearance/match_event/match_live/match_note/match_tactic_change → match,
match_clock_step → match_live: `CASCADE`).

Availability status: `actual_to` is the first available day; with `actual_to` empty, `suspension` and
`absence` stay active through `expected_to` inclusive, while for `injury` and `illness` `expected_to`
does not affect the status. Overlapping entries resolve as `injury` > `illness` > `suspension` >
`absence`.

Training sheet default status: a player with no saved attendance row for a session gets a status from
the availability active on the session day: `injury` or `illness` → `injured`, `absence` → `excused`,
`suspension` → `present` (a suspension is about matches, not training). Only rows with nothing saved
get a default; a saved row shows what was saved.

All tables are `STRICT` (SQLite 3.37 or newer), so column types are enforced. Every table has
`id INTEGER PRIMARY KEY` except where noted. Columns are `NOT NULL` unless marked *null*. `import_id` is always *null* and references `import_log`: it marks rows an import created, so
the import can be undone.

| Table | Columns | Notes |
| --- | --- | --- |
| `club` | `name` TEXT | One file per club |
| `team` | `club_id` INTEGER, `name` TEXT, `category` TEXT, `season` TEXT | Many teams per club. Season like `2026/27` |
| `staff_user` | `name` TEXT, `role` TEXT, `pin_hash` BLOB, `salt` BLOB, `iterations` INTEGER, `failed_attempts` INTEGER (default 0), `locked_until` TEXT *null*, `active` INTEGER 0/1 (default 1), `must_change_pin` INTEGER 0/1 (default 0, since 0005) | `must_change_pin`: a Coach chose the PIN (new user or reset); the user sets their own at the next login (P19) |
| `player` | `team_id` INTEGER, `first_name` TEXT, `last_name` TEXT, `shirt_number` INTEGER *null*, `birth_year` INTEGER *null*, `primary_position` TEXT *null* (since 0005; empty = not given, shown as "—"), `secondary_positions` TEXT (comma list, default empty), `foot` TEXT (default `unknown`), `notes` TEXT *null*, `archived_at` TEXT *null*, `import_id` INTEGER | No PESEL, no contact data; "status" is derived from `availability`. Partial unique index on (`team_id`, `shirt_number`) among non-archived players |
| `availability` | `player_id` INTEGER, `type` TEXT, `from_date` TEXT, `expected_to` TEXT *null*, `actual_to` TEXT *null*, `description` TEXT *null* | Description: Coach + Physio only. End dates not before `from_date` |
| `opponent` | `name` TEXT, `name_key` TEXT UNIQUE, `notes` TEXT *null* | `name_key` = `QString::toCaseFolded()`, because SQLite `NOCASE` folds ASCII only |
| `match` | `team_id` INTEGER, `opponent_id` INTEGER, `date` TEXT, `kickoff_time` TEXT *null*, `venue` TEXT, `competition` TEXT *null*, `goals_for` INTEGER *null*, `goals_against` INTEGER *null*, `status` TEXT (default `planned`), `formation` TEXT *null*, `import_id` INTEGER, `report_note` TEXT *null* (1–2000 after trimming, since 0004) | `report_note`: the note of the match report, plain text |
| `appearance` | `match_id` INTEGER, `player_id` INTEGER, `started` INTEGER 0/1 (default 0), `minute_on` INTEGER *null*, `minute_off` INTEGER *null*, `position` TEXT *null*, `rating` INTEGER *null* (1–10), `rpe` INTEGER *null* (0–10), `import_id` INTEGER | Minutes from Match Day, 0–130. UNIQUE (`match_id`, `player_id`) |
| `match_event` | `match_id` INTEGER, `minute` INTEGER (0–130), `type` TEXT, `player_id` INTEGER *null*, `related_player_id` INTEGER *null*, `added_time` INTEGER *null* (≥ 1, since 0002), `seq` INTEGER *null* (≥ 1, since 0002) | `player_id` empty only for `opponent_goal`. Semantics below |
| `match_live` | `match_id` INTEGER UNIQUE, `periods` INTEGER (1–4), `period_length` INTEGER (10–60), `clockless` INTEGER 0/1 (default 0), `next_seq` INTEGER (default 1), `finished_seq` INTEGER *null*, `ended_early_minute` INTEGER *null* (0–130) | Since 0002 (P13): a match that Match Day has started, in its format; `periods` × `period_length` ≤ 130. `next_seq`: the seq of the next action, a counter that only grows |
| `match_clock_step` | `match_id` INTEGER (→ `match_live.match_id`), `period` INTEGER (1–4), `kind` TEXT, `at` TEXT (UTC ISO datetime), `seq` INTEGER | Since 0002 (P13). `kind`: `start`, `end`; UNIQUE (`match_id`, `period`, `kind`) |
| `match_note` | `match_id` INTEGER, `minute` INTEGER (0–130), `added_time` INTEGER *null* (≥ 1), `player_id` INTEGER *null*, `text` TEXT (1–500 after trimming), `seq` INTEGER *null* | Since 0002 (P13): a note of Match Day with its minute; never changes the score or the minutes. Plain text |
| `match_tactic_change` | `match_id` INTEGER, `minute` INTEGER (0–130), `added_time` INTEGER *null* (≥ 1), `formation` TEXT (1–20), `tactic_json` TEXT, `seq` INTEGER (≥ 1) | Since 0003 (P13b): a change of the shape during the match, one row per Match Day action. UNIQUE (`match_id`, `seq`). Semantics below |
| `training_session` | `team_id` INTEGER, `date` TEXT, `start_time` TEXT *null*, `duration_min` INTEGER (15–240), `type` TEXT, `note` TEXT *null*, `import_id` INTEGER | |
| `attendance` | `session_id` INTEGER, `player_id` INTEGER, `status` TEXT, `rpe` INTEGER *null* (0–10), `minutes_override` INTEGER *null* (0–240), `import_id` INTEGER | session load = rpe × minutes; rpe and minutes empty unless `present`. UNIQUE (`session_id`, `player_id`) |
| `wellness` | `player_id` INTEGER, `date` TEXT, `sleep`, `fatigue`, `soreness`, `stress`, `mood` INTEGER (1–5) | SHOULD. UNIQUE (`player_id`, `date`) |
| `metric_definition` | `key` TEXT UNIQUE, `name_pl` TEXT, `name_en` TEXT, `value_type` TEXT, `unit` TEXT *null*, `min` REAL *null*, `max` REAL *null*, `aggregation` TEXT, `scope` TEXT, `higher_is_better` INTEGER 0/1 (default 1) | |
| `metric_value` | `metric_id` INTEGER, `player_id` INTEGER, `match_id` INTEGER *null*, `session_id` INTEGER *null*, `date` TEXT, `value` REAL, `import_id` INTEGER | At most one of `match_id` and `session_id` (scope `player` has neither) |
| `player_attribute` | `player_id` INTEGER, `attribute` TEXT, `rating` INTEGER (1–10), `date` TEXT | TOPSIS input for amateurs |
| `lineup` | `match_id` INTEGER *null*, `template_name` TEXT *null*, `formation` TEXT, `tactic_json` TEXT | Exactly one of `match_id` and `template_name` |
| `event_data_file` | `match_id` INTEGER, `provider` TEXT, `path` TEXT, `hash` TEXT | Loaded on demand |
| `import_log` | `timestamp` TEXT, `file` TEXT, `hash` TEXT, `type` TEXT, `rows_ok` INTEGER (default 0), `rows_failed` INTEGER (default 0), `report_json` TEXT *null* | Undo by `import_id` |
| `schema_version` | `id` INTEGER PRIMARY KEY (always 1), `version` INTEGER | One row |

`match_event` semantics (P10 decision 4, P13 decisions):
- `goal`: `player_id` = scorer, `related_player_id` = the assisting player or empty. Match Day writes no
  separate `assist` rows.
- `sub`: `player_id` = the player coming on, `related_player_id` = the player going off.
- `yellow`, `red`: `player_id` = the player. A second yellow card is a `yellow` row followed by a `red` row
  at the same time with `related_player_id` empty; it is recognised by counting the yellow cards of the
  player in the order of the match. A yellow and a straight red at the same minute is a red card.
- `opponent_goal`: no player.
- Format: `periods` parts of `period_length` minutes (2 parts are the halves; "1st/2nd half", otherwise
  "1st/2nd/3rd period"). Part p runs from (p − 1) × `period_length` to p × `period_length`.
- `minute`: the full minutes elapsed in the match, 0-based, cut at the end of the part. The minutes over
  the end of the part go to `added_time` (display only, ≥ 1); a substitution in a break is at the end of
  the part played, without `added_time`. Display follows the football convention: `minute` + 1 without
  `added_time` (0:30 → 1', 44:20 → 45'); with it, the end of the part plus the added minutes (45:30 →
  45+1', 47:10 → 45+3'; in 3 × 30 "30+2'", "60+1'").
- Order of the match: by `minute`; at the end of a part its added time (by `added_time`) comes before the
  same minute without it (the break or the start of the next part); then as entered.
- Every action of Match Day (an event with its rows, a note, a clock step, the finish) takes the next
  `seq` of the match from `match_live.next_seq`, which only grows: no seq is used twice, not even after
  undo or a deleted note. A note restored from its undo toast keeps its seq. Undo takes back the rows
  with the highest `seq`, whatever their minute. Rows not written by Match Day have it empty.
- Late entry: every action has a minute, the current one by default. Any minute already played can be
  entered (not the future); the players to choose are those on the pitch (or the bench) at that minute
  (`core::situationAt`), marked when that differs from now. The event then goes through a full
  replay, and one that a later event no longer fits (e.g. a red card for a player who scored later) is
  refused, naming that later event.
- Minutes of `appearance` are recomputed from the saved lineup and the events (`core::computeMatchMinutes`,
  the single source of truth): starters from 0, substitutes from their minute, a player sent off until
  the card, everyone else on the pitch until the final minute, which is `periods` × `period_length`
  whatever the stoppage time. While the match is live, players on the pitch have `minute_off` empty. A
  substituted player cannot come back; a sent-off player cannot be substituted. Rating and RPE are kept.
- "Set time": while a part runs with the clock, the time as the clock (the referee's watch) shows it
  ("67:30", "45+2:10") moves the start of that part (`match_clock_step.at`), and the clock runs on from
  there; also right after the kick-off was pressed late. At most the length of the part + 30 min, never
  before an event of the part. Not an action of the undo stack: its toast has Undo, which brings the
  start back.
- Without the clock (`clockless` = 1, chosen at the start; the default for a match before today):
  no stopwatch and no clock steps; every action takes a minute typed, any minute of the format (before
  `periods` × `period_length`, or 1–30 added minutes at the end of a part), not only minutes played.
  Parts come from the minutes; a substitution "in the break" is at the minute that starts the next
  part, as with the clock. "End match" is always there: with 7 players or more a normal end
  (`periods` × `period_length`), with fewer an early one at the last event or the minute typed, if
  later. Undo, notes, the checks of the events and the minutes of play work as with the clock.
- At least 7 players on the pitch (laws of the game, `core::kMinPlayersOnPitch`): with 7 left Match Day
  warns; with fewer only the end of the match, notes and undo are possible. Every end of the match
  (the clock button, the banner, the dialog) goes one way: with fewer than 7 players it is early,
  `ended_early_minute` = the minute of the end (while a part runs the clock, at most the end of the
  part; in a break the end of the part played; never before a stored action: an event, a note or a
  tactic change, `core::latestRecordedTime`); with 7 or more a normal end at `periods` ×
  `period_length` (then only at the end of the last part). "Set time" and its undo never put the
  clock before a stored action of the running part either (P13b review).

Live tactics (`match_tactic_change`, P13b):
- The 11 slots of the saved lineup keep their index for the whole match. A substitute takes the slot of
  the player going off, with its current position and place; a player sent off leaves his slot empty
  (not drawn on the board). A tactic change gives the slots new places (position and coordinates) and
  never changes who is in them: `tactic_json` is `.tactic.json` v1 with the 11 slots in slot order,
  every `playerId` null and no bench. Swapping two players exchanges the places of their slots.
- The shape at a minute (`core::shapeAt`): the slots of the lineup with the players after the events
  up to that time and the places of the last tactic change up to it (in the order of the match).
  Events entered late never conflict with tactic changes; a substitution and a tactic change at the
  same time commute. The name of the shape is counted from the occupied slots: the catalogue name with
  eleven, otherwise the rows ("4-4-1" with ten).
- `formation` holds that name when the change was recorded (no translated text); what Match Day and
  the match report show is always counted again by `core::shapeAt`, since a red card entered later may
  change it. `match.formation` keeps the starting formation of the lineup.
- A formation chosen during the match (`core::changeShape`) keeps everyone on the pitch: with fewer
  than eleven the empty slots take the goalkeeper position when nobody is in goal, then the positions
  furthest forward; the others take the positions of the same name first, then the free ones from the
  own goal up.
- Every change is one action of the undo stack (`seq` from `match_live.next_seq`), at a time played
  (in a break: the minute that starts the next part, as a substitution), never before a stored change,
  only with 7 players or more and needs `RunMatchDay`. Recording or undoing it recomputes the
  appearances: a substitute's `appearance.position` is the position of his slot at the minute he came
  on (right after the substitution, before later events of the same minute), tactic changes
  included; starters keep the position of the lineup.
- The board of Match Day spreads the rows of the shape evenly over its height and never draws a name
  wider than the gap to the next token of its row, so no two tokens overlap at the minimum window;
  drops still use the position zones.

Match report (P14):
- Ratings (1–10) and RPE (0–10) of `appearance` are entered for every player who played; the RPE counts
  in the match load (§7.1). Recomputing the minutes keeps them.
- Corrections of a finished match add or delete an event, a note, or delete a change of shape; every
  correction is one transaction that replays the whole match and recomputes the appearances and the
  score (`core::computeMatchMinutes`). The final minute of the match never changes; an event after it
  is refused. Events and notes added in the report have `seq` empty (they are not actions of Match Day,
  whose undo still takes back the end of the match); a row restored from the undo toast keeps its id
  and `seq`, and a substitute whose substitution comes back gets his rating and RPE back with it.
- Deleting an event that a later one needs (the substitution of a later scorer, the first yellow of a
  later second yellow) is refused, naming the later event; a second yellow and its red go together.
  Editing an event is delete + add.
- A match that Match Day did not run (demo data, import) is corrected on a sheet rebuilt from
  `appearance`: the starters (on from minute 0; the `started` flag is not needed) with their stored
  position, the players who came on, then the rest of the squad (who may come on in a correction); the
  final minute is the largest `minute_off` (90 without one). No format is guessed: an event takes a
  minute 0 to the final minute, without added time. Corrections are possible only when the stored events replay on
  that sheet and give the stored minutes and score; otherwise only ratings, RPE and notes.

`.tactic.json`: `{ "version": 1, "formation": "4-3-3", "slots": [{ "position": "GK", "x": 0.5, "y": 0.95, "playerId": 12 }] }`
— coordinates normalised 0–1 relative to the pitch (own goal at y = 1). Optional `"bench": [playerId, …]`
(at most 12, missing = empty); `playerId` null or missing = an empty slot.

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
- Note (info, P19): a player of the XI without a primary position, for whom the position is not checked.

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
