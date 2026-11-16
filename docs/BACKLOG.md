# Backlog

## W2 housekeeping (planned)
- Season validation: years 1900–2100, second part = first + 1 (P07b L2)
- Startup read error shows the wizard instead of an error screen (P07b L1)
- After a PIN reset by a Coach, the user must set their own PIN at next login
- Recovery code for the only Coach's PIN, shown once in the wizard (P07a)
- Log file in AppData/logs; single-instance lock with QLockFile (P05)
- "Expected return date has passed" hint on the player card (P06)
- Keyboard shortcut overview on F1
- Column chooser in the squad table, SPEC section 4 (P08)
- Deleting an availability entry entered by mistake; the repository already has `remove` (P08)
- Undo of a calendar delete gives the item a new id and loses `import_id`, and `formation` of a match
  without a saved lineup: the repository `create()` cannot set them. Matters once imports write them
  (P09a; since P12 a match with a lineup gets its lineup and formation back)
- Correcting the name of an opponent added by mistake, on the Opponents screen; the repository already
  has `update` (P09a)
- CI: compiler cache (sccache/ccache) for Windows and Linux builds; Windows build is ~14 min (P09a)
- qml_smoke: CTest TIMEOUT (600 s) and per-scenario progress output, so a hang fails fast with a name
- Squad position column: show at most 3 secondary codes + "+N"; codes matching the active filter first (P08 manual test)
- Saving attendance rewrites the rows and loses their `import_id`: the repository `saveAttendance()` cannot set
  it, so a corrected imported row no longer goes with the undo of its import. Matters once imports write it (P09b)
- A player has no date of joining: an old session counts someone who came later as not saved, and saving it writes
  a `present` row for them. Needs a column; the sheet cannot tell them apart (P09b)
- RPE can be saved in a session that is still in the future: nothing forbids it yet. Decide after coach
  testing, a planned absence may be a reason to keep the sheet open before the session (P09b)
- Status segments of the training grid shift by a few pixels between rows, because the selected word is bold in
  `SegmentedControl` (P09b)
- Remove the P03 spike view `SpikeChartsView.qml` and its Ctrl+Shift+G (Main.qml, qml_smoke, the mention in
  AGENTS §6): `LoadChart` is the real chart now (ADR-006 consequence, P11 decision 10)
- Yellow-card threshold per competition (SPEC §7.2); P12 has one global threshold in the app settings, a
  threshold per competition needs a settings table in the database (P12 decision 3)
- Icon clip workaround — sprawdzić w kolejnej wersji Qt i ewentualnie zgłosić błąd do Qt: `Icon.qml`
  adds far segments (`boundsGuide`) because the scene graph does not clip a Shape lying wholly outside the
  clip of a list (Qt 6.12, both renderers). With `boundsGuide: ""` `tst_iconclip.qml` must pass before the
  workaround goes; otherwise report it with that test as the reproduction (fix: icons respect clipping)

## Open questions for coach testing
- Coach hierarchy: can a second Coach deactivate the founder? Options: (a) single Coach role,
  (b) protected founder account, (c) "Owner" role above Coach. Decide after coach feedback.
