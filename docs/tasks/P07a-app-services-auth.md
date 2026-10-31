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

## Done when
- [ ] Section 3 passes; `fdss_vm_tests` runs in CTest.
- [ ] App still starts; with an empty database it shows the setup placeholder.

## Decisions (accepted plan)
