# W1 — data foundation and the first real screens

Goal of the week: install → wizard (empty or demo) → PIN login → squad with availability →
calendar → training RPE grid. Tag `v0.2-W1` when all of it works on Windows.

## Packets and dependencies

```
P04b housekeeping ────────────────────────────── (independent)
P05 database ──► P06 domain + repositories ──► P07a services + AppState ──┬──► P07b wizard + login UI ──► P10 demo (wizard option)
P06b UI kit (parallel with P05/P06) ──────────────────────────────────────┤
                                                                          ├──► P08 squad
                                                                          ├──► P09a calendar
                                                                          └──► P09b training grid
```

Critical path: P05 → P06 → P07a. After P07a (and P06b), P07b, P08, P09a, P09b and the P10 generator
can run in parallel worktrees. They share a few files with **trivial** conflicts: one member and one
construction line in `ServiceRegistry`, file lists in `CMakeLists.txt`, and the `.ts` files
(AGENTS §6 rule). Merge one at a time and rebase the others.

## Who does what

| Packet | Author | Effort | Review |
| --- | --- | --- | --- |
| P04b | Codex | Medium | none (small) |
| P05 | Claude | High | Codex |
| P06 | Claude | High | Codex |
| P06b | Claude (Antigravity) | Medium | Codex |
| P07a | Claude | High | Codex |
| P07b | Claude | Medium (done) | Codex |
| P08 | Claude | Medium | Codex |
| P09a | Claude | Medium | Codex |
| P09b | Claude | High | Codex |
| P10 | Claude | Medium | Codex |

Claude in Antigravity is the overflow when the Claude Code limit runs low. Gemini is not used.

## Suggested order

- Sun 1.11 (bonus day): P04b, P05 and P06b in parallel; P06 as soon as P05 is merged.
- Weekday evenings: reviews and merges; P07a if P06 is done.
- Sat 7.11: P07a (if not done), then P07b, P08, P09a, P09b in parallel.
- Sun 8.11: P10, fixes, manual macOS + sanitizer CI on `main`, Windows ZIP check, tag `v0.2-W1`.

If time runs short, cut in this order (last = most protected): P10 wizard option → P09a week view
→ P08 player card tabs. P05, P06, P07a/b and P09b are the core of W1.

## Rules learned in W0 (also in AGENTS.md)

- Paste your plan acceptance into the packet's `Decisions` section before implementation starts.
- `.ts` conflicts after a rebase: take `main`, run `update_translations`, translate `unfinished`.
- Agents commit locally; you push, open the PR and read CI.
- Docs-only commits no longer trigger CI (`paths-ignore`).
- After P05, P06 and P09b: run CI manually on `main` for the sanitizer job.
