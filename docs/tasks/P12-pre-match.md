# P12 — Pre-match: tactics board, lineup validator, lineups and templates

**Branch:** `feature/P12-pre-match`
**Depends on:** P11 merged (the validator uses load statuses)
**Owner agent:** Claude (effort High); review: Codex

## Goal
Before a match the coach picks a formation, places the starting XI and the bench on a pitch, sees at
once what is wrong with the lineup (injured, suspended, no goalkeeper, overloaded, out of position) and
saves it for Match Day.

Read first: SPEC §7.2 (validator), §6 (`lineup`, `.tactic.json`), §4 (Pre-match screen), AGENTS §6 (hit
targets 44 px on the tactics board).

## Scope — files you may create or change
- `src/core/domain/Lineup.hpp`, `src/core/algorithms/Formations.hpp/.cpp` (formation catalogue),
  `src/core/algorithms/LineupValidator.hpp/.cpp` + one class per rule (`ILineupRule`)
- `src/core/io/TacticJson.hpp/.cpp` (read/write `.tactic.json` v1; may use Qt JSON only if placed in
  `services`, core stays Qt-free — choose in the plan)
- `src/data/repositories/LineupRepository.hpp/.cpp`
- `src/services/LineupService.hpp/.cpp` (+ `ServiceRegistry` lines)
- `src/viewmodels/PreMatchViewModel`, `TacticsBoardModel`, `LineupIssuesModel`, related list models
- `ui/views/PreMatchView.qml`; `ui/components/TacticsPitch.qml`, `PlayerToken.qml`, `BenchList.qml`,
  `LineupIssuesPanel.qml`
- `src/viewmodels/AppSettings.*` and `ui/views/SettingsView.qml` (only: the yellow-card threshold)
- tests, CMake lists, `i18n/*.ts`, gallery, `qml_smoke`

## Out of scope
Lineup assistant (TOPSIS, W3), Match Day (P13), opponent profile (W3).

## Requirements
1. **Match selection**: `navigationArgs.matchId`, otherwise the next planned match; `SelectField` to
   switch; empty state with a button to the Calendar (role-aware, AGENTS §6). Header: opponent, date,
   venue, competition, last result against this opponent.
2. **Formations**: 4-4-2, 4-3-3, 4-2-3-1, 4-1-4-1, 3-5-2, 3-4-3, 5-3-2, each slot with a `Position` and
   normalised coordinates (SPEC `.tactic.json`, own goal at y = 1). Changing the formation keeps the
   players whose slot position still exists and moves the others to the bench.
3. **Tactics board**: pitch drawn in QML; player tokens (number + surname); available players list with
   status badges and load status (P11). Assign by drag and drop **and** without the mouse: select a slot,
   then pick a player from the list with Enter. Swap two slots, move a slot player to the bench, clear.
   Bench: up to 12 players. Hit targets ≥ `Theme.hitTouch`.
4. **Validator** (SPEC §7.2), live on every change. Errors: starting XI ≠ 11, no goalkeeper, injured or
   suspended player (status on the match date), duplicate. Warnings: red load status, out of nominal
   position (primary and secondary positions count as nominal), absent from the last 3 sessions, yellow
   cards one below the suspension threshold (yellows of this competition and season; threshold in
   Settings, default 4). Each issue names the player and the rule; clicking it selects the slot.
5. **Save** the lineup of the match (`lineup` row with `match_id`, `formation`, `tactic_json`; also
   `match.formation`). Saving with errors is allowed (a draft) and says so; Match Day (P13) refuses to
   start while errors remain. Leaving with unsaved changes asks (P09b leave guard).
6. **Templates**: save the current lineup as a named template, load a template into a match (players no
   longer in the squad are dropped with a message). **`.tactic.json`**: export and import a file (v1).
7. Permission `EditLineup`; read-only board for other roles.

## Tests
Core: every rule with positive and negative cases; formation change keeps/moves the right players;
`.tactic.json` round trip and rejection of a wrong version or broken file. Service: save/load, templates,
permission per role. Viewmodel: assignment by keyboard path, swap, bench limit. `qml_smoke`: board in
both themes at 1280×720.

## Done when
- [ ] Demo data → next match → 4-4-2 → place an injured player: an error names him; replace him: the
      error disappears; the lineup is saved and reloads the same.
- [ ] The keyboard-only path places 11 players.
- [ ] Section 3 passes.

## Notes for the thesis
Screenshot of the board with the issues panel. The validator is a Strategy pattern example (one class
per rule), worth a class diagram.

## Decisions (accepted plan)
1. **`.tactic.json` lives in the data layer**: `src/data/readers/TacticJson.hpp/.cpp` with
   `QJsonDocument` (AGENTS §2 "JSON: data layer only", §4 `data/readers/`), not in `core/io` or
   `services`. Version above 1: `IncompatibleVersion`; anything else that is not a valid v1 file:
   `UnsupportedFormat` with field `tactic_file`; a file above 1 MB: `InvalidInput`, field
   `tactic_file_size`. Positions are written in upper case as in the SPEC example and read in any case.
2. **Bench in `.tactic.json`**: an optional `"bench": [playerId, …]` key of v1 (missing = empty bench,
   at most 12); SPEC §6 says so. `playerId` null or missing = an empty slot.
3. **Yellow-card threshold**: one global threshold in `AppSettings` (QSettings, default 4, range 2–15),
   edited in Settings by `ManageSettings`. A threshold per competition goes to `docs/BACKLOG.md` (it needs
   a settings table, i.e. a migration). Cards accumulate up to a suspension and the count starts again:
   the warning shows when `yellows mod threshold == threshold − 1`, where `yellows` are the yellow
   cards of the player in matches of the same team (the team row is the season) and the same
   competition (no competition counts with no competition), `live` or `finished`, before the match
   date, leaving out matches in which the player got a red card.
4. **Synchronous lineup check**: the database reads of Pre-match run on the main thread (small
   queries), load statuses come from the existing `LoadService` future (P11), and the rules run
   synchronously after every change (8 rules × ≤ 23 players). AGENTS §5 now says that `QtConcurrent` is
   for long computations and import validation, not for checks of a form or a lineup.
5. **Which availability is an error**: every status other than available on the match date (injury,
   illness, suspension, absence); the text names the status.
6. **No goalkeeper**: an error when no slot has the position GK with a player in it. An outfield player
   in the GK slot is an out-of-position warning.
7. **Rules and the bench**: unavailable, duplicate, high load, missed sessions and yellow cards check the
   XI and the bench; out of position checks the XI only.
8. **Missed sessions**: the last 3 sessions of the team with saved attendance, before the match date
   and not after today. Warning when the saved status of the player is not `present` in all three. A
   player without a row in one of them, or fewer than 3 such sessions: no warning.
9. **Load status** is the P11 status "as of today" (the tooltip says so), not on the match date.
10. **Editing**: only `planned` matches; a `live` or `finished` match (for example from
    `navigationArgs.matchId`) shows its saved lineup read-only. The match field lists every planned match
    of the team by date; the default is the first one on or after today, otherwise the last planned one.
    No planned match: empty state with a button to the Calendar (`EditCalendar`), otherwise "ask the coach
    or the assistant".
11. **Formation change with a full bench**: players who do not fit on the bench leave the lineup (back
    to the list) and a toast says how many.
12. **Free positions on the pitch** (replaces the fixed catalogue coordinates of the plan):
    - The pitch is split into position zones, from the own goal: GK | LB CB RB | LWB DM RWB | LM CM RM |
      LW AM RW | ST. The GK and ST rows are one zone across the width (tokens stay in the middle half);
      the other rows have a narrow left, a wide centre and a narrow right zone. There are 14 positions
      (SPEC §6) and no CF, so the forward row is the ST zone. Zones are drawn subtly (Theme tokens), with
      labels while a token is dragged.
    - Catalogue formations are the starting point. Dropping a token in another zone changes the position
      of the slot to that of the zone (e.g. LM → LW); several tokens in one zone are laid out
      automatically (x spread over the zone, kept in their left-to-right order; y of the zone). The GK
      zone holds at most one token. Dropping a token on another token swaps the players (positions
      stay). Keyboard path: the selected slot has a `SelectField` "Position" with the same zones.
    - Name of the shape: the catalogue name when the slot positions are exactly those of a catalogue
      formation; otherwise counted from the rows (defence to attack, empty rows left out, the goalkeeper
      not counted), shown as e.g. "4-2-3-1 (custom)". `match.formation` and `"formation"` in
      `.tactic.json` hold the name without the suffix, so no translated text reaches the database; the
      suffix is shown whenever the positions differ from the catalogue.
    - Import of a file accepts any valid positions (11 slots, at most one GK) and coordinates 0–1 as they
      are (no normalisation); it rejects only invalid data. The name is counted again from the slots.
    - Choosing a formation from the list works as `changeFormation` in the plan, also from a custom
      shape. Swapping two slots swaps players, not positions. The validator does not change.
    - Catalogue (positions; coordinates come from the zone layout): 4-4-2 gk · rb cb cb lb · rm cm cm lm ·
      st st; 4-3-3 gk · rb cb cb lb · cm dm cm · rw st lw; 4-2-3-1 gk · rb cb cb lb · dm dm · rm am lm ·
      st; 4-1-4-1 gk · rb cb cb lb · dm · rm cm cm lm · st; 3-5-2 gk · cb cb cb · rwb cm dm cm lwb · st
      st; 3-4-3 gk · cb cb cb · rwb cm cm lwb · rw st lw; 5-3-2 gk · rwb cb cb cb lwb · cm cm cm · st st.
13. **Templates** are club-wide (`lineup` has no `team_id`): players of another team or archived ones are
    dropped on load with a message, as for a saved lineup or a file. Name 1–50 characters after trimming,
    unique regardless of case (checked by the service: no UNIQUE in the schema and no migration in P12);
    a taken name asks "Replace?". Deleting a template has a 10 s undo toast.
14. **Permissions**: export of a file for every role (read-only); import, templates and saving need
    `EditLineup`.
15. **Calendar delete** of a planned match with a saved lineup deletes the lineup in the same
    transaction; `DeletedMatch` keeps it and `restoreMatch` restores it (scope extension:
    `CalendarService`, its test).
16. **File dialog**: `QtQuick.Dialogs` `FileDialog`, no ADR (part of Qt Quick). `scripts/package-windows.ps1`
    checks the QML modules of the package, so `qml\QtQuick\Dialogs` is added to its required list.
17. **Scope extensions**: `ui/Main.qml` (the leave guard of `trainingPage` becomes one guarded page,
    Training or Pre-match), `ui/components/LineupPlayerList.qml`, `src/core/domain/Ids.hpp` (`LineupId`),
    `src/core/domain/Lineup.cpp`, `src/core/algorithms/LineupRules.hpp/.cpp`,
    `src/viewmodels/LineupTexts.*`, `RowsModel.*` (the base of the list models and `ChoiceListModel`),
    `LineupModels.*` (the four list models), `ErrorPresenter.cpp`, `ui/theme/Theme.qml` (new tokens),
    `ui/components/Icon.qml` (a `swap` icon), `src/services/CalendarService.*`,
    `scripts/package-windows.ps1`, `docs/SPEC.md` §6, `AGENTS.md` §5 (one line), `docs/BACKLOG.md`,
    `tests/qml_components/tst_tacticspitch.qml`.
18. **Tests added by the decisions**: core: yellow cards 3, 4, 5, 7 at threshold 4; changing the position
    of a slot; the name counted from the rows; at most one GK. `qml_components`: dragging a token to
    another zone. Viewmodel: the keyboard path of a position change. `TacticJson`: round trip of an
    asymmetric shape. Service: delete and undo of a match with a lineup.
19. **Review (Codex)**:
    - *Read errors*: `reload()` that cannot read the facts again keeps the lineup on the screen and its
      dirty state (the leave guard still asks) and shows `loadError` in a banner with Try again above
      the board; only with no match open the error replaces the screen.
    - *Load not checked*: a `LoadService` read error is an explicit state, "the check is incomplete:
      the load could not be checked" with a warning icon and Try again in the issues panel; the
      summary never says the lineup is ready while the load is pending or not checked; `reload()`
      tries again.
    - *Undo of whole-lineup changes*: Clear, loading a template and importing a file remember the
      lineup before and show a 10 s toast whose Undo brings it back (with its dirty state); no
      confirmation dialog.
    - *Calendar*: deleting a match with a saved lineup asks first ("This match has a saved lineup.
      Delete it together with the lineup?", AGENTS §6 like a session with attendance), then the undo
      toast (`CalendarService::hasLineup`, `CalendarViewModel::matchHasLineup`).
    - *Threshold*: `AppSettings::setYellowCardThreshold()` checks ManageSettings in C++ and refuses
      with a message otherwise; the property is read-only to QML (`ServiceRegistry::isInstalled()`
      for the singleton, which also lives without a registry in the component tests).
    - *Synchronous import of `.tactic.json`*: an exception to AGENTS §5, like decision 4: the file is
      at most 1 MB and parses in milliseconds.
    - *Position field*: a refused position (a second goalkeeper) shows the current one again.
    - *Bench by keyboard*: "Move to the bench (B)" and "Add to bench (B)": the buttons of the
      selected slot are in the Tab order, B on a focused token or row of the squad list is their
      accelerator.
    - *Touch*: the position field (`SelectField.touch`), the buttons of the selected slot, the rows and
      buttons of the bench and the squad list are at least `Theme.hitTouch`; qml_smoke measures them.
    - *Threshold change* emits `issuesChanged`.
    - *Squad tooltip*: "On the match day: …" and "Load as of dd.MM (today): …" (the day of the P11
      status, decision 9).
    - *Transaction*: `LineupService::save()` owns the transaction; `LineupRepository::saveForMatch()`
      runs inside the caller's one (also `CalendarService::restoreMatch()`).
    - *Tokens of one zone*: a grid, `kMinZoneColumnWidth` 0.15 (one token in a row in a side zone,
      three in a wide one) and further rows `kZoneRowStep` 0.14 apart around the y of the zone, so they
      never overlap one another; catalogue formations lay out as before. Extreme shapes may still
      bring a token of one zone close to one of the next row.
