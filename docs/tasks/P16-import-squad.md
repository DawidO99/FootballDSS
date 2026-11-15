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
