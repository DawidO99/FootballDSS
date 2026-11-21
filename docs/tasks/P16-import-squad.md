# P16 — Import framework and squad import

**Branch:** `feature/P16-import`
**Depends on:** W1 (`v0.2-W1`); independent of P11–P15
**Owner agent:** Claude (effort High); review: Codex

## Goal
A coach imports the squad from the file he already has (Excel or CSV, Polish headers, Polish
characters, any separator) through a wizard that shows what will happen row by row, writes nothing
wrong, and can be undone. The framework is reused by P18 for training and match data.

Read first: SPEC §8 (all of it, including the must-pass cases), AGENTS §8, §7 (backup before import),
`docs/adr/ADR-006` (QXlsx).

## Scope — files you may create or change
- `src/core/import/` (pure): CSV parser (quotes, delimiter detection), CP1250 decoding table (own table:
  do not rely on ICU codecs, they are missing on some Qt builds), header synonym matching, row
  validation helpers, position synonyms (PL/EN, e.g. BR/GK, ŚO/CB, N/ST)
- `src/services/import/` : file detection (`QMimeDatabase`, 20 MB), XLSX reading (QXlsx), `ImportService`
  (preview, mapping, validation, commit with backup, `import_log`, undo) (+ `ServiceRegistry` lines)
- `src/data/repositories/ImportLogRepository.hpp/.cpp`; small additions to `PlayerRepository` for
  `import_id`
- `src/viewmodels/ImportWizardViewModel` and models
- `ui/views/ImportView.qml`, wizard components in `ui/components/`
- `resources/import-templates/` (squad template CSV + XLSX) and saving a copy via a file dialog
- `tests/data/import/` fixtures, tests, CMake lists, `i18n/*.ts`, gallery, `qml_smoke`

## Out of scope
Training, match and metric import (P18), StatsBomb JSON (W3).

## Requirements
1. **Wizard**: choose type (Squad now; Training, Matches appear with P18) → file → preview (first 20
   rows, sheet choice for multi-sheet XLSX) → mapping (auto by PL/EN synonyms, manual override per column,
   "ignore column") → validation report (per row: OK / warning / error with the reason, e.g. "row 14:
   shirt number 'abc', expected a number") → commit (skip invalid rows or abort) → summary with
   "Undo import".
2. **Reading**: SPEC §8 steps 1–2. XLS → "save as .xlsx"; JSON → "not supported for this type".
3. **Squad rows**: first + last name (or one "name" column split by the last space, shown in preview),
   number, birth year, primary and secondary positions, foot. Existing player matched by shirt number or
   by name: the user chooses per import "update existing" or "skip existing"; ambiguous matches are
   listed and skipped.
4. **Commit**: automatic backup (AGENTS §7) → one transaction → `import_log` with file hash; the same
   file again shows a warning before commit. Undo import: players created by the import are deleted if
   nothing references them, otherwise archived; updated players are not reverted (say so in the
   summary and in Decisions).
5. **Mapping templates**: the confirmed mapping is remembered by header set and offered next time.
6. Parsing and validation run via `QtConcurrent` with progress; the UI never blocks.
7. Permission `Import`.

## Tests
Every must-pass case of SPEC §8 that applies to squad files, each as a fixture: video renamed to `.csv`,
empty file, header only, Polish Excel CSV (CP1250, `;`), UTF-8 with BOM, quotes and commas inside fields,
duplicate players, unknown positions, same file twice, corrupted XLSX, multi-sheet XLSX. Undo import.
Locked database during commit → error, nothing written.

## Done when
- [ ] A CP1250 CSV saved by Polish Excel with headers "Nazwisko;Imię;Nr;Pozycja" imports 20 players.
- [ ] The downloaded template, filled and imported, round-trips.
- [ ] Section 3 passes.

## Notes for the thesis
Table of the must-pass cases and their results; screenshot of the validation report.

## Decisions (accepted plan)
1. **Locations** (AGENTS §4, ADR-006): pure parsing in `src/core/parsing/` (encoding/CP1250, CSV,
   header matching, position synonyms, squad row validation); the XLSX reader in
   `src/data/readers/XlsxReader` (QXlsx stays in the data layer); file detection, CSV reading and
   `ImportService` in `src/services/import/`. P18 uses the same places.
2. **Templates** live in `ui/import-templates/` (resource of `fdss_ui`), not in a new top-level
   `resources/` folder. Headers in Polish (`Imię;Nazwisko;Nr;Rok urodzenia;Pozycja;Pozycje dodatkowe;Noga`);
   the synonyms also match the English names.
3. **Undo deletes** created players that nothing references: an exception to "players are archived,
   never deleted" (SPEC §6) only for undoing an import, because such a player never existed before it.
   A player with availability, attendance, appearances, match events or a place in any lineup
   (`lineup.tactic_json`, no foreign key) counts as referenced and is archived. The `import_log` row stays
   (archived players keep `import_id`); the undo is marked in `report_json` (`undoneAt`), a second undo is
   a `Conflict`. Updated players are not reverted; the summary says so.
4. **Mapping templates**: the confirmed mapping is stored in `report_json` of the import, keyed by the
   normalised header set; the latest successful import with the same key supplies it next time. No
   schema change.
5. **Scope extensions**: `ImportId` in `src/core/domain/Ids.hpp`, `ErrorPresenter` (new fields).
6. **Update existing** overwrites only mapped, non-empty cells; `import_id` is not set on updated players.
   Changing a number to one held by another player is a row error.
7. **Duplicates in the file** (same shirt number, or same first + last name): every involved row is an
   error naming the other rows. Unknown position in a cell: row error. Unknown secondary position:
   warning, dropped. Missing foot: `unknown`.
8. **Required columns**: only the name — first + last name, or one full-name column. Without a position
   column (or with an empty position cell) the mapping step asks for a "Default position" (`SelectField`,
   required only then); every such row gets the warning "position set by default — correct it in Squad".
   An unknown position in a cell stays a row error.
9. **Full-name column**: the mapping step has an order switch "First Last" / "Last First", detected from
   the header ("Nazwisko i imię", "Surname, name"… → Last First, otherwise First Last), with a preview of
    the split for the first rows. Split at the last space for First Last, at the first space for
    Last First, so "Nowak-Kowalski Jan" and "Jan Nowak-Kowalski" both give last name "Nowak-Kowalski".
10. **Target team**: the current team (`AppState.teamId`), as in Squad.
11. **Type step**: only "Squad" is shown; Training and Matches arrive with P18.
12. **SPEC §8 cases left to P18/W3**: database deleted while running, decimal comma, unknown shirt
    numbers, truncated / non-StatsBomb JSON. P16 says "not supported for this type" for any JSON.
13. **Locked database**: a second connection holding `BEGIN EXCLUSIVE` makes the backup or the commit
    fail with `DatabaseLocked`; nothing is written.
14. **Technical**: QXlsx runs on a worker thread (checked by a test); `qml_smoke` never opens a corrupted
    XLSX (QZip warning, ADR-006). XLSX is recognised by the ZIP signature and then by QXlsx, because
    `QMimeDatabase` reports a plain `application/zip` for content only.
