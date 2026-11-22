# Manual testing — commands to copy

For the human. Run everything from the repository root (`~/Desktop/Praca_Dyplomowa/FootballDSS`
or the packet's worktree), never from `build/`.

## 1. Check the branch

```bash
git status
git log --oneline -3
```

## 2. Build and test

```bash
cmake --preset dev-local
cmake --build build/dev --parallel
ctest --test-dir build/dev --output-on-failure
ctest --preset dev-local-ci          # offscreen + C locale + GC stress, like CI
```

One test only: `ctest --test-dir build/dev -R <name> --output-on-failure`.

## 3. Run the app on fresh data

```bash
rm -rf /tmp/fdss-test
XDG_DATA_HOME=/tmp/fdss-test/data XDG_CONFIG_HOME=/tmp/fdss-test/config XDG_CACHE_HOME=/tmp/fdss-test/cache ./build/dev/FootballDSS
```

Same data again (continue a test, resume a match): run only the second line, without `rm -rf`.
The variables apply only to the app; your real data is never touched.

## 4. Look into the database

```bash
sqlite3 "$(find /tmp/fdss-test -name club.sqlite)"
```

Useful queries:

```sql
SELECT player_id, minute_on, minute_off, position, rating, rpe FROM appearance WHERE match_id = <id>;
SELECT id, date, status, goals_for, goals_against FROM match ORDER BY date DESC LIMIT 5;
SELECT id, file, type, rows_ok, rows_failed FROM import_log ORDER BY id DESC LIMIT 5;
```

## 5. Standard test data

Wizard: language Polish, any club, Coach PIN `1234`; "Load demo data" unless the test needs an
empty club. Staff (Settings → Staff): Assistant `3333`, Physio `2222`, Analyst `4444`.
Log out from the user menu to switch roles. Check both languages, both themes and the minimum
window (1280×720) at least once per packet.

## 6. After the test

```bash
git push -u origin <branch>          # first push
git push                             # later pushes
git push --force-with-lease          # after a rebase
```

PR in the browser → CI green → Squash and merge → then:

```bash
git switch main && git pull
git worktree remove ../FootballDSS-<packet>    # if the packet had a worktree
git branch -d <branch>
```

New packet: `git worktree add ../FootballDSS-<packet> -b <branch> main`.

If `main` moved after the branch started: ask the agent to rebase onto `origin/main`
(`.ts` files: take main, run `update_translations`, translate new entries), then push with
`--force-with-lease`. Never resolve conflicts in the GitHub web editor.

Sanitizer + macOS run: Actions → CI → Run workflow on `main` (after packets with migrations or match
logic).
