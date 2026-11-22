# Start pracy w IDE — kolejność na dziś i jutro

## Podział ról
- **IDE + agenci (Claude autor, Codex review, Claude in Antigravity jako zapas przy wyczerpanym
  limicie):** piszą kod według pakietów z `docs/tasks/`.
- **Rozmowa z promotorem-architektem (czat):** wracasz tam, gdy kończysz weekend, gdy agent proponuje
  zmianę architektury albo stacku, gdy coś blokuje Cię dłużej niż ~30 minut, oraz po kolejne pakiety
  (pakiety rozpisujemy przed każdym weekendem, na podstawie tego, co faktycznie powstało).
  Plan bieżącego tygodnia: `docs/tasks/W1-plan.md` (i kolejne).

## Kolejność W0 (31.10–1.11)
1. **P04** — Ty, ręcznie: repo, Qt 6.12, kompilator, VS Code. Ok. 20–40 minut.
2. **P01** — jeden agent, najmocniejszy model. To fundament i wzorce dla wszystkich kolejnych pakietów,
   więc nie równolegle. Przejrzyj diff uważnie, zwłaszcza CMake i `Theme.qml`.
3. **P02 i P03 równolegle** — dwa agenty w dwóch worktree:
   ```bash
   git worktree add ../FootballDSS-P02 -b feature/P02-ci
   git worktree add ../FootballDSS-P03 -b spike/P03-risks
   ```
   Każdy worktree otwierasz w osobnym oknie VS Code z innym agentem.
4. Wieczorem w niedzielę: tag `v0.1-W0`, wklejasz do czatu z promotorem treść ADR-006 (wyniki spike'a)
   i zrzut ekranu aplikacji. Na tej podstawie dostajesz pakiety P05–P10 na W1.

## Prompt startowy dla pakietu (wklej do agenta, podmień Pxx)
```
Przeczytaj AGENTS.md oraz docs/tasks/Pxx-*.md. Nie pisz jeszcze kodu.
Odpowiedz:
1) Plan: lista plików do utworzenia/zmiany i po jednym zdaniu, co w każdym.
2) Interfejsy: nagłówki klas / Q_PROPERTY / sygnatury, które wprowadzasz.
3) Ryzyka i pytania: wszystko, co w pakiecie jest niejasne albo sprzeczne z AGENTS.md.
Poczekaj na moją akceptację.
```
Po akceptacji:
```
AAkceptuję plan [z poprawkami: ...]. Implementuj małymi krokami, buduj po każdym kroku.
Na końcu uruchom wszystkie komendy z sekcji 3 AGENTS.md, zrób lokalny commit i raportuj
według sekcji 10. Commit jednolinijkowy, bez linii "Co-Authored-By". Nie pushuj.
```
Zanim wkleisz akceptację agentowi, wklej ją też do sekcji `## Decisions (accepted plan)` w pliku
pakietu. Recenzent przeczyta ją stamtąd.

## Cykl pakietu (bez gh, przez przeglądarkę)
1. `git worktree add ../FootballDSS-Pxx -b feature/Pxx-nazwa` (z aktualnego main), skopiuj
   `CMakeUserPresets.json` do nowego folderu, otwórz go w osobnym oknie VS Code.
2. Plan → akceptacja (+ Decisions) → implementacja → lokalny commit agenta.
3. Ty: `git push -u origin feature/Pxx-nazwa`, na GitHubie „Compare & pull request”.
4. CI (format, Linux, Windows) — zielone? Review innym modelem, poprawki, push.
5. „Squash and merge”, „Delete branch”; lokalnie `git checkout main && git pull`,
   `git worktree remove ../FootballDSS-Pxx`.
6. Inne otwarte gałęzie: `git fetch && git rebase origin/main` (konflikty w `.ts` → AGENTS.md §6).

## Prompt do krzyżowego review (inny model niż autor)
```
Jesteś recenzentem. Przeczytaj AGENTS.md i docs/tasks/Pxx-*.md (łącznie z sekcją Decisions —
decyzje tam zapisane są zaakceptowane, nie zgłaszaj ich jako naruszeń), potem przejrzyj diff gałęzi
względem main (git diff main...HEAD). Nie poprawiaj kodu. Wypisz:
- naruszenia AGENTS.md (z plikiem i linią),
- błędy logiczne i przypadki brzegowe bez obsługi,
- rzeczy spoza zakresu pakietu,
- brakujące testy.
Posortuj od najpoważniejszych.
```

## Gdy agent „odpływa”
- Zmienia pliki spoza pakietu → odrzuć zmiany w tych plikach, przypomnij sekcję 10.
- Proponuje nową bibliotekę lub zmianę stacku → stop, pytasz promotora w czacie.
- Kręci się w kółko przy tym samym błędzie kompilacji > 3 razy → nowa sesja z samym błędem i plikiem.
- Testy czerwone, a agent chce je „dopasować” → zakazane (sekcja 12), naprawia kod, nie test.

## Rytm dnia (12 h)
- Rano 15 min: wybór pakietów, start agentów.
- Co 1–2 h: review planu / diffu, akceptacja, test ręczny.
- Merge tylko z zielonym CI.
- Wieczorem: tag, krótka notatka w `docs/devlog.md` (co powstało, co nie działa) — materiał do pracy.

## Models and roles (from W2 on)

Goal: spend the Claude Code limit only where thinking pays off.

| Step | Who | Model |
| --- | --- | --- |
| Plan of a packet | Claude Code | Opus |
| Implementation of an accepted plan | Claude Code | Opus |
| Review of the packet | Codex | – |
| Fix round after the review | Codex (fixes its own findings) or Opus | – |
| Short check of the fix round if Codex was fixing (diff only) | Claude Code | Opus |
| Fix round after CI or the manual test | Claude Code | Opus |

Session hygiene
- One Claude Code session per packet (plan → implementation → fixes). `/clear` before the next packet.
- Before a push you may run `ctest --preset dev-local-ci` yourself; the agent then runs plain ctest.

Prompt for Codex fix rounds:
"Fix your own review findings for <packet> (list below). Change only what the findings name, add the
test each one asks for, run §3, commit '<fix(Pxx): review findings>', report in ≤ 15 lines. Do not
push."

Prompt for the Claude check after a Codex fix round:
"Check commit <hash> (Codex fixed the review of <packet>). Diff only: does each fix address its
finding without breaking AGENTS.md or the Decisions? Findings only, ≤ 10 lines. Do not change code."