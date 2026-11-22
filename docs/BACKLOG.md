# Backlog

## W2 housekeeping (planned)
- Startup read error shows the wizard instead of an error screen (P07b L1)
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
- Import: per-player goals/assists/cards counters in appearance (migration) so imported matches keep scorers and
  cards count toward suspensions (P18 decision 2)
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
- JS garbage collector workaround — sprawdzić w kolejnej wersji Qt i ewentualnie zgłosić błąd do Qt:
  `useNonIncrementalGarbageCollector()` (`src/viewmodels/JsGarbageCollector.hpp`) sets `QV4_GC_TIMELIMIT=0`
  because the incremental collector of Qt 6.12 frees the property storage of live objects ("Cannot find
  member data" in AppDialog and DateField on CI). Without the call, `qml_smoke` under `QV4_GC_TIMELIMIT=1`
  must be clean before it goes (fix(P12): CI-only dialog failures)
- CI: ręczne zadanie ze stresem GC (QV4_GC_TIMELIMIT=1, bez obejścia) — sprawdza, czy kolejna wersja Qt
  naprawiła błąd. A `workflow_dispatch` job running `qml_smoke` and `qml_components` with
  `QV4_GC_TIMELIMIT=1` and `useNonIncrementalGarbageCollector()` switched off (it needs a way to skip the
  call, e.g. an environment variable read only by the test programs) (P19)
- Match Day: cards at the time of a break itself (a break takes events at minutes already played and
  substitutions at the start of the next part), own goals of the opponent (a `goal` row needs our
  scorer), cards for players on the bench, and a limit of substitutions per competition (P13 decision 7)
- Match Day: editing an event in place (the match report of P14 corrects by delete + add); undo of a
  note edit or delete other than its 10 s toast
- Match report: adding a change of shape after the match (needs the tactics board in the report; today
  only deleting one, P14 decision 4)
- Match Day: leaving asks only while Match Day is the section shown; logging out or closing the window from
  another section during a live match does not ask (one guarded page in `Main.qml`, P13)
- Match Day tactics: inserting a change of shape before a later stored one (refused today with
  `tactic_change_later`; it would need the draft based on the shape at its minute and the later changes
  rebased on it) (P13b decision 5)

## Open questions for coach testing
- Coach hierarchy: can a second Coach deactivate the founder? Options: (a) single Coach role,
  (b) protected founder account, (c) "Owner" role above Coach. Decide after coach feedback.
