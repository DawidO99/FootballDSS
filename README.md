# FootballDSS

FootballDSS is an offline desktop decision-support system for football coaches, from amateur clubs to
professional teams. It keeps squad, training and match data in a local SQLite database and turns it into
explained recommendations.

## Requirements

- Qt 6.12 with Qt Graphs (which also needs the Qt Quick 3D and Qt Shader Tools modules)
- CMake ≥ 3.28 and Ninja
- GCC 14, MSVC 2022 or Apple Clang

## Build and test

Point CMake at your Qt kit and pick the compiler, then use the `dev` preset:

```bash
export CMAKE_PREFIX_PATH=~/Qt/6.12.0/gcc_64
export CXX=g++-14
cmake --preset dev
cmake --build --preset dev
ctest --preset dev --output-on-failure
```

`qml_smoke` needs a display with OpenGL; for machines without one see `AGENTS.md`, section 3.

## Run

```bash
./build/dev/FootballDSS                                # Linux
build\dev\FootballDSS.exe                              # Windows
./build/dev/FootballDSS.app/Contents/MacOS/FootballDSS # macOS
```

## Documentation

- `AGENTS.md`: build rules, stack and conventions
- `docs/SPEC.md`: scope, screens, data model and algorithms
- `docs/adr/`: architecture decisions

License: to be decided
