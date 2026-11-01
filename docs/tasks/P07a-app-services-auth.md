# P07a — Composition root, auth and setup services, AppState

**Branch:** `feature/P07a-services`
**Depends on:** P06 merged
**Owner agent:** Claude (effort High)

## Goal
Everything below the UI that login, roles and every later screen need: the composition root,
`AuthService`, `SetupService`, the current session, error presentation, `AppState`, and
navigation with arguments. No new screens (P07b).

## Scope — files you may create or change
- `src/services/AuthService.hpp/.cpp`, `SetupService.hpp/.cpp`, `Session.hpp/.cpp`, `Clock.hpp`
  (injectable "now"/"today"), `src/services/CMakeLists.txt` (link `Qt6::Network` PRIVATE for
  `QPasswordDigestor`, ADR-007; remove `ServicesPlaceholder.cpp`)
- root `CMakeLists.txt` (`find_package` adds `Network`; new viewmodel sources)
- `src/viewmodels/ServiceRegistry.hpp/.cpp`, `ErrorPresenter.hpp/.cpp`, `AppState.hpp/.cpp`,
  `NavigationModel.hpp/.cpp` (arguments, see below)
- `src/main.cpp` (composition root), `tests/qml_smoke/main.cpp` (installs the registry)
- `ui/Main.qml` and all `ui/views/*View.qml` placeholders (only the navigation signal change)
- `tests/services/*`, `tests/viewmodels/*` (new target `fdss_vm_tests` linking `fdss_ui`),
  `tests/CMakeLists.txt`, `i18n/*.ts`

## Out of scope
Wizard, profile picker, staff settings UI, top bar menu (P07b).

## Interfaces (agreed before coding)
```cpp
// ServiceRegistry: plain C++ (not a QObject). main.cpp / qml_smoke install it before QML loads.
// Holds DatabaseManager, repositories and services. Later packets add their service with one
// member + one construction line (expected, trivial merge conflicts).
static void install(std::unique_ptr<ServiceRegistry>);
static ServiceRegistry& instance();
static void reset();   // tests and qml_smoke only (switch between database states)

// AppState (QML_SINGLETON)
Q_PROPERTY(State state READ state NOTIFY stateChanged)   // NeedsSetup, NeedsLogin, Ready
Q_PROPERTY(QString userName ...) Q_PROPERTY(QString roleName ...)   // roleName translated
Q_PROPERTY(QString clubName ...) Q_PROPERTY(QString teamName ...) Q_PROPERTY(qint64 teamId ...)
Q_INVOKABLE bool can(const QString& permission) const;   // names = core Permission enum names
Q_INVOKABLE void logout();
Q_INVOKABLE void refresh();   // re-reads state after setup/login

// NavigationModel: navigation with arguments
Q_INVOKABLE void activate(const QString& key, const QVariantMap& args = {});
Q_PROPERTY(QVariantMap currentArgs READ currentArgs NOTIFY currentIndexChanged)
// every view: signal navigateRequested(string key, var args); Main.qml passes args through,
// and gives each view `property var navigationArgs` (current args when it is shown)

// ErrorPresenter: QString message(const core::Error&) — qsTr text per ErrorCode (+ field name)
```

## Requirements
1. **AuthService**: PIN policy 4–8 digits; hashing per ADR-007 (salt, constant-time compare);
   iterations are a constructor parameter (120 000 in the app, small in tests); lockout after 5
   failures for 30 s stored as UTC ISO `locked_until`; `login`, `changeOwnPin` need no permission;
   staff-management methods (`addStaff`, `updateStaff`, `resetPin`, `deactivate`, `reactivate`)
   require `ManageStaff` of the session user and return `PermissionDenied` otherwise; last active
   Coach can be neither deactivated nor demoted.
2. **SetupService**: `isSetupComplete()`; `completeSetup(club, team, coachName, pin)` in one
   `Transaction`, then logs the coach in.
3. **Session**: current user + role; `isAllowed(Permission)` via core matrix; cleared on logout.
4. **main.cpp**: open DB (P05) → build registry → install → load QML. `AppState` reads its state from
   the registry.
5. **qml_smoke**: runs three database states with `reset()`: empty in-memory DB (`NeedsSetup`),
   seeded club + coach (`NeedsLogin`), logged in (`Ready`) — both languages and themes each.
   Until P07b exists, `NeedsSetup`/`NeedsLogin` show a temporary placeholder in `Main.qml` with a
   debug-build-only button "Create test club and log in as Coach" (PIN `1234`), so P08/P09 can be
   clicked through before P07b is merged. P07b removes the placeholder and the button.
6. **Tests**: hash round trip and wrong PIN; same PIN → different hash; lockout and expiry with an
   injected clock; permission denial per role for each staff method; last-Coach protection; setup
   atomicity (forced failure leaves no club); `AppState.can()` for each role (vm test).
7. Move the startup error texts from `StartupViewModel` to `ErrorPresenter`.

## Done when
- [ ] Section 3 passes; `fdss_vm_tests` runs in CTest.
- [ ] App still starts; with an empty database it shows the setup placeholder.

## Decisions (accepted plan)

Accepted by the human on 2026-11-01. These override the packet text above (AGENTS §11).

1. **PBKDF2 on the main thread.** `login()` is synchronous. This is an accepted exception to AGENTS §5
   ("the UI thread never blocks on computation"), limited to PIN hashing. Measured on the dev machine
   (Ryzen 7 260): 132–136 ms per hash at 120 000 iterations, inside the 0.1–0.3 s of ADR-007. Above
   0.3 s locally the agent stops and reports. `changeOwnPin` hashes twice.
2. **One place builds the services.** `ServiceRegistry::create(database, options)`; `main.cpp`,
   `qml_smoke` and the viewmodel tests only call it. A new service is added only in `ServiceRegistry`.
   Data types are only forward-declared in its header, so viewmodels reach nothing below the services.
   AGENTS §5, the scopes of P07b, P08, P09a, P09b, P10 and `W1-plan.md` are updated.
3. **`reset()` in `main.cpp` too.** The registry owns the database, so it has to go after the QML engine
   and before `QGuiApplication`.
4. **`PinCheck` instead of new error codes.** `login` and `changeOwnPin` return `Result<PinCheck>`: a
   wrong PIN and a lockout are answers carrying the remaining attempts and the time left; `Error` stays
   for failures. No change to `core`.
5. **Beyond the agreed interfaces.** P07b has no services in its scope, and viewmodels may not use
   repositories, so this packet also adds: `listActiveStaff`, `listAllStaff`, `lockRemaining` (a lockout
   visible after a restart), `hashPin` (P10), `SetupService::clubContext`; `AppState`: `errorOccurred`,
   `debugBuild`, `createTestClubAndLogin`, `roleDisplayName`; `ErrorPresenter::startupMessage` (its texts
   speak of opening the file and would mislead after a failed save); `Session::require`.
6. **The shell is a `Loader` on state `Ready`.** `can()` is a method without a notify signal, so a
   binding on it does not update when the user changes. Rebuilding the section views at every login
   makes it correct, and a logout leaves no data of the last user in memory, injury details included.
   After a logout the navigation returns to `dashboard`. AGENTS §6 states the rule.
7. **Changing your own account.** Changing the role of the session user or deactivating that user ends
   the session (`NeedsLogin` after `refresh()`); renaming only updates it.
8. **Rules of login and setup.**
   - During a lockout an attempt costs no hash and does not extend it. After it, the user has 5 fresh
     attempts. `now == locked_until` means unlocked.
   - A `locked_until` more than 30 s ahead means the clock was moved back, and counts as expired;
     otherwise the only Coach could stay locked out for as long as the clock was off.
   - A malformed PIN is `InvalidInput` with field `pin` and is not counted. A deactivated user gets
     `PermissionDenied`.
   - A wrong current PIN in `changeOwnPin` counts towards the lockout, so it is no way around the limit.
     `resetPin` and a successful change of PIN clear the counter and the lockout.
   - A stored PIN is checked with its own `iterations`; a new hash uses the constructor parameter.
   - `validate()` reports `name` for club, team and coach alike, so `completeSetup` reports `club_name`,
     `team_name` and `coach_name`. The last active Coach gives `Conflict` with field `last_coach`.
   - The current team is the first by id. `isSetupComplete` means a club, a team and an active Coach.
9. **Sanitizers do not run tests that link `fdss_ui`.** `fdss_vm_tests`, `qml_smoke` and `qml_components`
   have the CTest label `ui`; the job builds `fdss_tests` and runs `ctest -LE ui`. `fdss_vm_tests` uses a
   plain `add_test`, not `gtest_discover_tests`: for a target that was not built, the latter registers a
   `<target>_NOT_BUILT` test without any properties, which `-LE ui` would run and fail. The cost is one
   CTest entry instead of one per test case; `--gtest_filter` on the binary still selects a single one.

**Standing rules.**
- Every service calls `validate()` before `create()`/`update()` in a repository: repositories do not
  validate. `firstError()` in `services/ServiceSupport.hpp` turns the result into a `Status`.
- A database write always runs on the main-thread connection in one `Transaction`; `QtConcurrent` is for
  computation only. `AuthService` and `SetupService` hash a PIN before the transaction begins, so the
  write lock is not held for the length of a hash.

**Scope extension:** `src/viewmodels/StartupViewModel.hpp/.cpp` (requirement 7),
`src/services/ServiceSupport.hpp`, `.github/workflows/ci.yml` (decision 9), `AGENTS.md`, the packets of
decision 2, and in P07b the scope for removing the temporary `AppState` members. `ui/Main.qml` changes
beyond the navigation signal (requirement 5, decision 6).

**Found during implementation.**
- An empty `std::vector` has a null `data()`, so through `bytesValue()` it binds `NULL` and the schema
  refuses it. An empty `pin_hash` can therefore come only from a damaged or edited file; `AuthService`
  treats it as matching nothing.
- `QPasswordDigestor::deriveKeyPbkdf2` returns an empty key for `iterations = 0`; `hashPin` checks the
  length of the key and returns `Internal`.
- What a `Repeater` creates is not found by `findChildren()` from the window, so `qml_smoke` walks
  `childItems()` to find a section view.

**Known limitations.**
- There is no way to recover the PIN of the only Coach (planned in W2).
- Until P07b is merged, `NeedsSetup` and `NeedsLogin` are a dead end in a Release build, which has no test
  club button: do not hand out a package between P07a and P07b.
- `TopBar` still shows the fixed "My team" and "Coach": the user chip belongs to P07b.
- A `can()` binding does not update during the life of a view. A change of own role ends the session, so
  this cannot be observed today.
