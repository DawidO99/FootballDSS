# P05 — Database foundation: error model, DatabaseManager, migrations, backups

**Branch:** `feature/P05-database`
**Depends on:** P04b not required (no overlap)
**Owner agent:** Claude (effort High) — this sets the data-layer patterns for every later packet

## Goal
The app can open (or create) the club database, apply migrations with a backup first, and every
table from SPEC §6 exists with its constraints. Errors travel as `Result<T>`. Nothing visible in
the UI yet except a clear error screen when the database cannot be opened.

## Scope — files you may create or change
- `src/core/util/Error.hpp` (+ `.cpp` if needed)
- `src/data/db/DatabaseManager.hpp/.cpp`, `src/data/db/Transaction.hpp/.cpp`,
  `src/data/db/Migrations.hpp/.cpp`, `src/data/db/Backup.hpp/.cpp`, `src/data/db/AppPaths.hpp/.cpp`
- `src/data/migrations/0001_initial.sql`, `src/data/migrations.qrc` or `qt_add_resources` entry
- `src/data/CMakeLists.txt` (link `Qt6::Core Qt6::Sql` PUBLIC, remove `DataPlaceholder.cpp`)
- `docs/SPEC.md` §6 (exact column names and types, same commit as 0001)
- `src/main.cpp` (open the database before loading QML; on failure load the error screen)
- `ui/views/DatabaseErrorView.qml` (+ registration in root `CMakeLists.txt`)
- `src/viewmodels/StartupViewModel.hpp/.cpp` (exposes the startup error to QML; minimal)
- `tests/data/*` (new), `tests/core/ErrorTest.cpp`, `tests/test_main.cpp` (own gtest main),
  `tests/CMakeLists.txt`, `tests/qml_smoke/main.cpp` (in-memory database, error view)
- `i18n/*.ts`

## Out of scope
Repositories (P06), domain types beyond `Error` (P06), auth and wizard (P07), any other view.

## Interfaces (agreed before coding)
```cpp
// core/util/Error.hpp — no Qt
namespace fdss::core {
enum class ErrorCode {
    NotFound, InvalidInput, Conflict, PermissionDenied,
    Database, DatabaseLocked, NotADatabase, IncompatibleVersion,
    Io, UnsupportedFormat, Internal
};
struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string detail; // technical, English, for logs only
    std::string field;  // optional: input field / column the error refers to
};
template <class T> using Result = std::expected<T, Error>;
using Status = Result<void>;
[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;  // for logs and tests
}

// data/db/DatabaseManager.hpp
namespace fdss::data {
class DatabaseManager {
public:
    // Opens or creates the file, applies pragmas, backs up and migrates if needed.
    static core::Result<std::unique_ptr<DatabaseManager>> open(const QString& filePath);
    static core::Result<std::unique_ptr<DatabaseManager>> openInMemory(); // tests
    ~DatabaseManager();                       // closes and removes the named connection
    [[nodiscard]] QSqlDatabase database() const;   // the connection of the creating thread
    [[nodiscard]] int schemaVersion() const;
    [[nodiscard]] QString filePath() const;        // empty for in-memory
    // New named connection to the same file for a worker thread (QtConcurrent work later).
    [[nodiscard]] core::Result<QSqlDatabase> openWorkerConnection(const QString& name) const;
    core::Result<QString> backup(std::string_view reason);  // returns the backup path
};

// RAII, nestable: the outermost Transaction runs BEGIN IMMEDIATE / COMMIT, inner ones run
// SAVEPOINT / RELEASE (depth counter kept in DatabaseManager). Destructor rolls back
// (ROLLBACK or ROLLBACK TO + RELEASE) unless commit() succeeded. Services and repositories may
// both open one, so a service can wrap several repository calls atomically.
class Transaction {
public:
    explicit Transaction(DatabaseManager& db);
    core::Status commit();
};
}
```
`fdss_data` links `Qt6::Core` and `Qt6::Sql` PUBLIC (headers expose `QSqlDatabase`), but only
repositories and `db/` write SQL; services never do. `openWorkerConnection` on an in-memory
database returns `Internal`.

## Requirements
1. **Pragmas** on every connection: `foreign_keys = ON`, `busy_timeout = 5000`; on file databases
   `journal_mode = WAL`. Verify `foreign_keys` actually reads back as 1. The busy timeout is a
   parameter of `open()` (default 5000 ms) so tests can use a short one.
2. **Opening a bad file:** random bytes / a text file renamed to `club.sqlite` → `NotADatabase`
   (detect with `PRAGMA quick_check` or reading `sqlite_master`), never a crash. A database locked
   by another process beyond the busy timeout → `DatabaseLocked` (in WAL mode reads are not blocked,
   so detect it with `BEGIN IMMEDIATE` during open). A `schema_version` higher than the
   app knows → `IncompatibleVersion` (the app refuses to touch it).
3. **Migrations** live as `NNNN_name.sql` in Qt resources, applied in order, each in its own
   transaction together with the `schema_version` update. Statement splitting: one statement per
   `;` at the end of a line; migration files must not contain `;` inside literals or triggers
   (document this at the top of the runner and in a comment in 0001). Unknown or failing statement →
   `Database` error with the migration number in `detail`, database left at the previous version.
4. **Backup** with `VACUUM INTO ?` (path bound as a parameter; the target must not exist, so add
   `-2`, `-3`… when two backups fall in the same second) to
   `AppDataLocation/backups/club-<yyyyMMdd-HHmmss>-<reason>.sqlite`
   before migrating an existing database (not for a brand-new file) and on demand; keep the 10 newest
   backups (by file name), never delete anything else in that folder.
5. **0001_initial.sql** creates every table of SPEC §6 with the enumerations, `CHECK` constraints,
   foreign keys, `ON DELETE` rules, unique shirt number among non-archived players
   (partial unique index), `opponent.name_key` UNIQUE, `archived_at` on `player`, `active`/lockout columns on `staff_user`, and an
   index for every foreign key and every date column used for range queries. Columns not yet used by
   W1 (wellness, metrics, lineup, event data, import log) are created now as well.
6. **AppPaths:** `defaultDatabasePath()` and `backupDirectory()` from `QStandardPaths::AppDataLocation`;
   create the folders if missing.
7. **Startup:** `main.cpp` opens the database. On failure the app loads `DatabaseErrorView` (an
   `ApplicationWindow`, so it opens its own window) instead
   of `Main` (title, a translated explanation per error code, the file path, buttons "Open data
   folder" via `QDesktopServices::openUrl` and "Quit"). On success behaviour is unchanged.
   `qml_smoke` uses an in-memory database and also instantiates `DatabaseErrorView`.
8. **Test harness:** replace `GTest::gtest_main` with `tests/test_main.cpp` (creates
   `QCoreApplication`, calls `QStandardPaths::setTestModeEnabled(true)`); enable AUTOMOC for test
   targets that need it.
9. **Tests** (in-memory and temp-file databases, `QTemporaryDir`): fresh create → version 1 and all
   tables present; reopen → no migration, no backup; foreign keys enforced (insert child with missing
   parent fails); CHECK constraints reject an invalid role/position/RPE 11; partial unique index allows
   reusing an archived player's number; garbage file → `NotADatabase`; future version → `IncompatibleVersion`;
   backup creates a readable file and pruning keeps 10; two backups in the same second both succeed;
   a failing migration rolls back; nested `Transaction` commits/rolls back correctly (inner rollback
   keeps outer work, outer rollback drops everything).

## Done when
- [ ] App starts as before with a new `club.sqlite` in the AppData folder.
- [ ] Replacing `club.sqlite` with a text file shows the error screen with the path.
- [ ] All tests above exist and pass; section 3 passes.

## Notes for the thesis
ERD can be generated from 0001 later; keep table and column names exactly as in SPEC §6.

## Decisions (accepted plan)

Accepted by the human on 2026-11-01. These override the packet text above (AGENTS §11).

1. **Transaction API.** `Transaction::begin(DatabaseManager&)` returns `Result<Transaction>` instead of
   the public constructor, because a constructor cannot report a failed `BEGIN IMMEDIATE` and later
   statements would then commit one by one.
2. **OpenOptions.** `open()` and `openInMemory()` take `OpenOptions` (`busyTimeoutMs`,
   `backupDirectory`, `migrations`), so tests do not depend on earlier runs. `backup()` on an in-memory
   database returns `Internal`.
3. **`import_id`.** Nullable, `RESTRICT`, indexed, added now to `player`, `match`, `appearance`,
   `training_session` and `attendance` (plus `metric_value` from SPEC §6).
4. **Values SPEC §6 did not define.** `value_type`: `integer`, `decimal`, `boolean`. `aggregation`:
   `sum`, `avg`, `min`, `max`, `last`. `wellness` scores: 1–5. `import_log.type`,
   `event_data_file.provider` and `player_attribute.attribute`: non-empty only, no list of values.
5. **Startup error texts.** Temporary `tr()` in `StartupViewModel`; P07a moves them to `ErrorPresenter`.
6. **Translations.** Only `pl` is translated; `FootballDSS_en.ts` entries stay `unfinished` because
   English is the source language.
7. **This section** is written as the first step of the implementation.

Decided by the plan and accepted:
- `availability.from` becomes `from_date` (`from` is an SQL keyword).
- `shirt_number` and `birth_year` are nullable (P08). The `CHECK` on `birth_year` has only the lower
  bound 1950, because `CHECK` cannot use the current date; P06 validation checks the upper bound.
- `archived_at` is `YYYY-MM-DD`; `import_log.timestamp` is UTC ISO. `rpe` and `rating` are INTEGER.
  Minutes in `appearance` and `match_event` are 0–130 (SPEC §8).
- A valid SQLite file with tables but no `schema_version` gives `NotADatabase`.
- A bad file is detected by reading `sqlite_master`, not `quick_check`.
- `busy_timeout` goes through `setConnectOptions`; the savepoint has the constant name `fdss_sp`.
- Backups sort numerically by (timestamp, suffix). `reason` is limited to `[a-z0-9_]+`.
- `DatabaseErrorView` uses two `PrimaryButton`s until P06b provides `SecondaryButton`.

**Worker connections (human decision).** Heavy work (reading files, validation, computation) runs via
`QtConcurrent`, but database writes always stay on the main-thread connection in one `Transaction`.
`openWorkerConnection` stays, unused for now.

**Scope extension (same PR):** one-sentence documentation changes in `AGENTS.md` §5 and §6 and in
`docs/tasks/P07a-app-services-auth.md`.
