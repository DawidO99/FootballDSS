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
- [ ] No QML or viewmodel changes.

## Decisions (accepted plan)
