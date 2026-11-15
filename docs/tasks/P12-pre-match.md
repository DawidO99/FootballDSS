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
