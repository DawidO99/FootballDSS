# Backlog

## W2 housekeeping (planned)
- Season validation: years 1900–2100, second part = first + 1 (P07b L2)
- Startup read error shows the wizard instead of an error screen (P07b L1)
- After a PIN reset by a Coach, the user must set their own PIN at next login
- Recovery code for the only Coach's PIN, shown once in the wizard (P07a)
- Log file in AppData/logs; single-instance lock with QLockFile (P05)
- "Expected return date has passed" hint on the player card (P06)
- DatabaseErrorView window sizes from Theme (P05)
- Keyboard shortcut overview on F1
- Column chooser in the squad table, SPEC section 4 (P08)
- Deleting an availability entry entered by mistake; the repository already has `remove` (P08)
- Undo of a calendar delete gives the item a new id and loses `import_id` and `formation`: the
  repository `create()` cannot set them. Matters once imports and the pre-match screen write them (P09a)
- Correcting the name of an opponent added by mistake, on the Opponents screen; the repository already
  has `update` (P09a)
- CI: compiler cache (sccache/ccache) for Windows and Linux builds; Windows build is ~14 min (P09a)
- qml_smoke: CTest TIMEOUT (600 s) and per-scenario progress output, so a hang fails fast with a name
- Squad position column: show at most 3 secondary codes + "+N"; codes matching the active filter first (P08 manual test)
- Saving attendance rewrites the rows and loses their `import_id`: the repository `saveAttendance()` cannot set
  it, so a corrected imported row no longer goes with the undo of its import. Matters once imports write it (P09b)
- A player has no date of joining: saving an old session writes a `present` row also for someone who came
  later. Needs a column; the sheet cannot tell them apart (P09b)
- RPE can be saved in a session that is still in the future: nothing forbids it yet. Decide after coach
  testing, a planned absence may be a reason to keep the sheet open before the session (P09b)
- Status segments of the training grid shift by a few pixels between rows, because the selected word is bold in
  `SegmentedControl` (P09b)

## Open questions for coach testing
- Coach hierarchy: can a second Coach deactivate the founder? Options: (a) single Coach role,
  (b) protected founder account, (c) "Owner" role above Coach. Decide after coach feedback.
