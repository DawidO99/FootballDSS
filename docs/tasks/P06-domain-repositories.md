# P06 — Domain types, permissions and repositories

**Branch:** `feature/P06-repositories`
**Depends on:** P05 merged
**Owner agent:** Claude (effort High)

## Goal
Plain C++ domain types with validation, the permission matrix, and repositories that read/write
them through SQL, fully unit-tested. Services and viewmodels of later packets never write SQL.

## Scope — files you may create or change
- `src/core/domain/*.hpp/.cpp`: `Enums` (all enumerations of SPEC §6 + to/from text),
  `Club`, `Team`, `StaffUser`, `Player`, `Availability`, `Opponent`, `Match`, `Appearance`,
  `MatchEvent`, `TrainingSession`, `Attendance`, `Permissions`, `Validation`
- `src/core/CMakeLists.txt`
- `src/data/repositories/*.hpp/.cpp`, `src/data/CMakeLists.txt`
- `tests/core/*`, `tests/data/*`, `tests/CMakeLists.txt`

## Out of scope
Services, viewmodels, QML, demo data, PIN hashing (P07; the repository only stores the bytes).

## Interfaces (agreed before coding)
- IDs: strong types (`struct PlayerId { std::int64_t value; auto operator<=>(...) const = default; }`
  and the same for every entity). No raw `int64_t` ids in public APIs.
- Dates: `std::chrono::year_month_day`; time of day: `std::chrono::minutes` since midnight.
  Use only `year_month_day` construction, comparison and `sys_days` arithmetic: no
  `current_zone`/`zoned_time`/`std::format` of chrono types (Apple Clang). Core never asks for
  "today"; callers pass it in (services take it from Qt). Parsing/formatting ISO text lives in
  `fdss_data`.
- Enums: `enum class` + `toDbText()` / `fromDbText()` returning `Result` (unknown text = error).
  Positions also expose `positionGroup(Position)`.
- `bool isAllowed(Role, Permission)` with the `Permission` values listed in SPEC §5.
- Validation: `ValidationErrors validate(const PlayerDraft&, int currentYear)` etc. returning a list
  of `{field, ErrorCode}`; pure, no database. Repositories turn constraint violations into
  `Conflict` (with `field`) or `InvalidInput`.
- Repositories take `DatabaseManager&` and return `Result<T>`:
  - `ClubRepository`: club get/create; teams list/create/update.
  - `StaffRepository`: list (active and all), get, create, update (name, role, active), credential
    fields read/write (`pin_hash`, `salt`, `iterations`, `failed_attempts`, `locked_until`),
    `countActiveCoaches()`.
  - `PlayerRepository`: list by team (with/without archived), get, create, update, archive, unarchive.
  - `AvailabilityRepository`: list by player, list active on a date for a team, create, update, close
    (set `actual_to`), delete. `update` takes `std::optional<std::string> description`:
    `nullopt` leaves the stored description unchanged (roles without `ViewAvailabilityDetails` must
    not wipe it).
  - `OpponentRepository`: list, get, create, update; uniqueness through `name_key`
    (`QString::toCaseFolded()`, so "Śląsk" and "śląsk" collide).
  - `MatchRepository`: list by team and date range, get, create/update planned match, set result and
    status, `listCompetitions(TeamId)` (distinct, most recent first); appearances (replace all for a match in one transaction, list); events (append, list,
    delete last).
  - `TrainingRepository`: sessions list by team and range, get, create, update, delete (cascades
    attendance); `saveAttendance(sessionId, vector<Attendance>)` replaces all rows of a session in one
    transaction; list attendance by session and by player and date range.
- Multi-row writes use `Transaction` from P05.

## Requirements
1. `fdss_core` stays Qt-free (the CMake guard checks it).
2. All SQL in repositories is prepared + bound; text ↔ enum mapping only through `core/domain/Enums`.
3. Derived "status" of a player for a date (available / injured / ill / suspended / absent) is a pure
   core function over availability entries, not a column.
4. Tests:
   - core: every enum round-trips; unknown text fails; permission matrix row by row against SPEC §5;
     validation boundaries (names 0/1/50/51 chars after trim, shirt 0/1/99/100, birth year 1949/1950,
     current−4/current−3); derived status with overlapping entries.
   - data (in-memory DB with migrations): CRUD for every repository; duplicate shirt number →
     `Conflict` with `field = "shirt_number"`; archived player frees the number; deleting a session
     removes its attendance; `saveAttendance` is atomic (one invalid row → nothing saved);
     opponent name uniqueness ignores case; appearances replace is atomic.

## Done when
- [ ] Section 3 passes; test count and coverage of every repository method listed in the report.
- [x] No QML or viewmodel changes.

## Decisions (accepted plan)

Accepted by the human on 2026-11-01. These override the packet text above (AGENTS §11).

1. **When an availability entry ends.** `actual_to` is the first available day (exclusive end), so
   "Mark as returned" today makes the player available today. An entry with
   `actual_to = from_date` is never active. The meaning of `expected_to` depends on the type:
   - `injury`, `illness`: an estimate that does not affect the status; the entry stays active until
     `actual_to`.
   - `suspension`, `absence`: a known end. With `actual_to` empty the entry is active through
     `expected_to` inclusive. `actual_to`, when set, takes precedence.
2. **Overlapping entries.** The derived status follows `injury > illness > suspension > absence`.
3. **Deleting a match.** `MatchRepository::removePlanned(MatchId)` is added, because P09a needs it and
   its scope has no repositories. A status other than `planned` gives `Conflict`.
4. **Name limits in `validate()`.** Club, team and opponent names 1–100 characters; `first_name`,
   `last_name` and the staff name 1–50, all after trimming. `CHECK` holds the upper bound only for
   player names. SPEC §6 is corrected in the same PR.
5. **Editing a match.** `updatePlanned` on a match that is not `planned` gives `Conflict`.

**Repository rule.** Every statement in the repositories goes through `execSql()` or ends with a
`sqlError()` check; a repository stops working at the first error.

Decided by the plan and accepted:
- `saveAttendance` and `replaceAppearances` take `std::span<const AttendanceDraft>` and
  `std::span<const AppearanceDraft>` instead of `vector<Attendance>`: `id` and `sessionId` have no
  meaning on input.
- Drafts hold no parent id: `create(TeamId, draft)`. `update` does not move a player between teams.
- `AvailabilityDraft` includes `actualTo`, so P10 can create a closed entry in one call and `update`
  can undo a mistaken close. `close()` stays. `description` lives in the draft: on `update`, `nullopt`
  keeps the stored text and an empty text clears it.
- `locked_until` is `std::chrono::sys_seconds` in core and `yyyy-MM-ddTHH:mm:ssZ` in the database.
- `StaffUser` holds no PIN data: `pin_hash` and `salt` are read only through `credentials()`, as
  `std::vector<std::uint8_t>`.
- Repositories store trimmed names, and an optional text that is empty after trimming becomes `NULL`.
  Length counts Unicode code points, the same as SQLite `length()`.
- `createClub` refuses a second club with `Conflict`; the schema does not enforce one club per file.
- `validate()` rejects duplicates in `secondary_positions` and the primary position among them. An
  unknown token read from the database is a `Database` error.
- `listAttendanceByPlayer` also returns the date and duration of the session (W2 load needs them).
- `permissionFromName()` is added for `AppState.can()`: the scope of P07a has no core files.
- `listActiveOn` narrows by `from_date` in SQL and leaves the rule to `isActiveOn()`. It skips players
  archived on or before that date.
- Repositories neither set nor return `import_id` (the import packet does). `Match::formation` is
  read-only in W1.
- Repositories do not know the logged-in user: hiding `description` from a role, and checking that a
  player belongs to the team of a session or match, are service work (P08, P09b).
- Added files beyond the list above: `core/domain/Ids.hpp`, `core/domain/DateTypes.hpp`,
  `data/repositories/RepositorySupport.hpp/.cpp`, `tests/data/RepositoryFixture.hpp`.

**Scope extension (same PR):** the name limits in `docs/SPEC.md` §6.

**Found during implementation.**
- `ON DELETE RESTRICT` reports SQLite code 1811 (`CONSTRAINT_TRIGGER`), not 787 (`CONSTRAINT_FOREIGNKEY`),
  with the same message. `statementError()` handles both; the migrations cannot hold triggers of their
  own, so 1811 always means `RESTRICT` here.
- The driver binds a null `QString` as `NULL`, but an empty `QByteArray` as an empty blob. So
  `textValue()` never passes a null string, and an empty `pin_hash` is stored without an error. The
  repository only stores the bytes; `AuthService` (P07a) always writes a derived key.

**Review decisions (one round).**
- `runSql()`, the one path of every repository statement, checks the new
  `DatabaseManager::isTransactionBroken()` first and returns `Database` without running anything, as
  `execSql()` does. A real `SQLITE_FULL` rolled back the whole transaction, and the next write committed
  on its own and survived the destructor of the outer `Transaction`.
- `RowReader::integer()` and `optionalInteger()` return `Database` naming the column for a value that
  does not fit `int`, instead of truncating it. `boolean()` reads through `integer()`.
- Supported dates are 1900-01-01 to 2100-12-31 (`core::isSupportedDate()`), used by `validate()` for
  every date field. `archive()` and `close()` take a bare date that no `validate()` sees, so they check
  it themselves: `InvalidInput` with `field` = `archived_at` / `actual_to`. Reading is not limited to
  the range.
- `parseIsoTimestamp()` accepts only `yyyy-MM-ddTHH:mm:ssZ`. A date alone or a time without a zone is
  `Database`: read as local time it would make the end of a lockout depend on the machine.
- SPEC §6 now states the availability rules of decisions 1 and 2.

**Scope extension of the review:** `DatabaseManager::isTransactionBroken()` in `src/data/db/`, the
availability rules in `docs/SPEC.md` §6.

**Known limitations.**
- `metric_value` references `training_session` and `match` with `RESTRICT`, so a session or match with
  metric values cannot be deleted: `remove` and `removePlanned` return `Conflict` (for a match also
  because of `lineup` and `event_data_file`).
- `saveAttendance` and `replaceAppearances` replace rows with `DELETE` + `INSERT`, which loses
  `import_id`. All of them are `NULL` in W1, so nothing breaks yet; the import packet must solve this,
  otherwise "Undo import" misses the edited rows.
