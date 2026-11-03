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

## Open questions for coach testing
- Coach hierarchy: can a second Coach deactivate the founder? Options: (a) single Coach role,
  (b) protected founder account, (c) "Owner" role above Coach. Decide after coach feedback.
