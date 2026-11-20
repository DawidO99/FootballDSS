# W2 — decision support: load, lineup, Match Day, dashboard, import, metrics

Goal: every MUST item of SPEC §3 works on top of W1. After W2 a coach can plan a match on the
tactics board, run it live, rate the players, see load alerts and the dashboard, import the squad and
training data from Excel, and track own metrics. Tag `v0.3-W2` when all of it works on Windows.

## Packets and dependencies

```
P11 load monitoring ──► P12 pre-match (lineup, validator, tactics board) ──► P13 Match Day ──► P13b live tactics ──► P14 match report
        └──────────────────────────────────────────────────────────────────► P15 dashboard + capabilities
P16 import framework + squad ──┬──────────────────────────────────────────► P18 import training, matches, metrics
P17 custom metrics ────────────┘
                                                          all of the above ──► P19 W2 housekeeping
```

| Packet | What | Depends on | Effort |
| --- | --- | --- | --- |
| P11 | Load monitoring: sRPE, EWMA acute/chronic, ratio, monotony, alerts, Load screen, player chart | W1 | High |
| P12 | Pre-match: lineup storage, lineup validator, tactics board, `.tactic.json`, templates | P11 | High |
| P13 | Match Day: live clock, substitutions, goals, cards, automatic minutes, undo, resume | P12 | High |
| P13b | Live tactics: formation changes and moves during the match, live tactical board | P13 | High |
| P14 | Match report: statistics, ratings, match RPE, note, corrections, post-match entry without Match Day, player card "Matches" tab | P13b | High |
| P15 | Dashboard + `CapabilityService` (features unlock with data, locked hints) | P11 (P14 for form) | Medium |
| P16 | Import framework (CSV/XLSX, mapping, validation, undo) + squad import + templates | W1 | High |
| P17 | Custom metrics: definitions, entry, player card | W1 | Medium |
| P18 | Import of training, matches and custom metric columns | P16, P17 | High |
| P19 | W2 housekeeping from `docs/BACKLOG.md` | all | Medium |

Every packet: author Claude, review Codex (AGENTS §11). Claude in Antigravity is the overflow.

## Order

Pipeline, as in W1: one packet with the agent, the previous one with the human (manual test, push,
CI, review, merge). Independent branches may run in parallel when the limit allows:

1. P11, then P12 → P13 → P13b → P14 (the decision-support core, in this order).
2. P16 and P17 can run in parallel with step 1 at any time (separate code, no shared screens).
3. P15 after P11 and P14; P18 after P16 and P17.
4. P19 last.

## Shared files (trivial conflicts, merge one at a time and rebase the others)

`ServiceRegistry`, CMake file lists, `i18n/*.ts` (AGENTS §6 rule), `ErrorPresenter.cpp`,
`ui/theme/Theme.qml` (new tokens only), `tests/qml_smoke/main.cpp`, `docs/BACKLOG.md`.

**Migration numbers are fixed here** so two branches never create the same file:
`0002` → P13 (live match state, periods, notes), `0003` → P13b (tactic changes), `0004` → P14 (match report), `0005` → P19 (housekeeping).
No other W2 packet adds a migration; if one turns out to need it, stop and ask.

## Rules for W2

- Every packet reads its SPEC sections (named in the packet) before planning; SPEC wins over memory.
- Charts use Qt Graphs (ADR-006); every chart has `ChartLegend`/`ChartTooltip` (SPEC §4) and is
  readable in both themes.
- Load wording: "signal to talk to the player", never "injury risk" (SPEC §7.1).
- Match Day and the tactics board use hit targets of at least `Theme.hitTouch` (44 px, AGENTS §6).
- After P13 and after P18: run CI manually on `main` for the sanitizer job.

## W3 (SHOULD, after `v0.3-W2`)

Lineup assistant (TOPSIS + Hungarian, SPEC §7.3), opponent profile (§7.4), Pro analysis with
StatsBomb open data and radar (§7.5), PDF export (player card, match report), wellness
questionnaire. Packets are written after W2 is merged.
