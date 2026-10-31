# P04 — Repository setup (human, ~20 minutes)

**Branch:** `main` (initial commit)
**Owner:** you, not an agent

## Steps
1. Create a private GitHub repo `FootballDSS`, clone it.
2. Copy the contents of this kickoff pack into the repo root:
   `AGENTS.md`, `CLAUDE.md`, `GEMINI.md`, `docs/`.
3. Add `.gitignore`: `build/`, `dist/`, `*.user`, `.vscode/*` (but keep `.vscode/settings.json` if you want shared settings), `.DS_Store`, `*.sqlite`.
4. Add a `README.md` with one paragraph: what the app is, how to build (`cmake --preset dev`).
5. Install Qt 6.12 LTS with the online installer on Linux (and on the Windows machine you will test on):
   components Desktop (GCC on Linux, MSVC 64-bit on Windows), Qt Graphs, Qt Quick Controls, Qt Linguist tools, Qt Debug info optional.
   Set `CMAKE_PREFIX_PATH` to the Qt kit directory (e.g. `~/Qt/6.12.0/gcc_64`).
6. Check the compiler: GCC 14 or newer (`g++ --version`). On Pop!_OS install `g++-14` if needed and point CMake at it.
7. VS Code: install CMake Tools and the Qt extension (`theqtcompany.qt` pack) for QML highlighting/qmlls.
8. Commit: `chore: repo setup, agent rules, ADRs, task packets`, push.
9. Protect `main`: PRs required once CI (P02) exists.

## Done when
- [ ] Repo on GitHub with the files above.
- [ ] `qmake6 -v` or `~/Qt/6.12.0/gcc_64/bin/qmake -v` prints 6.12 (just to verify the kit; we never use qmake to build).
