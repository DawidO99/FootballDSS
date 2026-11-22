# Backlog

## W2 housekeeping (planned)
- qml_smoke on a real desktop (Wayland, window clamped to 1280×720 by the screen): the Settings toggle of the
  dashboard and the Team dialog checks fail, also on `main`; offscreen and Xvfb runs are clean. The checks
  click items that lie outside the smaller window (P19b manual run)
- Recovery code for the only Coach's PIN, shown once in the wizard (P07a)
- Column chooser in the squad table, SPEC section 4 (P08)
- Undo of a calendar delete gives the item a new id and loses `import_id`, and `formation` of a match
  without a saved lineup: the repository `create()` cannot set them. Matters once imports write them
  (P09a; since P12 a match with a lineup gets its lineup and formation back)
- Import: per-player goals/assists/cards counters in appearance (migration) so imported matches keep scorers and
  cards count toward suspensions (P18 decision 2)
- A player has no date of joining: an old session counts someone who came later as not saved, and saving it writes
  a `present` row for them. Needs a column; the sheet cannot tell them apart (P09b)
- RPE can be saved in a session that is still in the future: nothing forbids it yet. Decide after coach
  testing, a planned absence may be a reason to keep the sheet open before the session (P09b)
- Status segments of the training grid shift by a few pixels between rows, because the selected word is bold in
  `SegmentedControl` (P09b)
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
  must be clean before it goes (fix(P12): CI-only dialog failures). The manual CI job `gc-stress` runs exactly
  that (`FDSS_INCREMENTAL_GC=1`, P19)
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
