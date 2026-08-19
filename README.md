# Flora

Procedural vegetation and forest simulation. Standalone C++20 core; Unreal Engine integration
is a later, optional adapter. See `RESEARCH.md` (background), `ARCHITECTURE.md` (locked
decisions), `ROADMAP.md` (milestones).

## Build

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

## Run

```sh
./build/flora-cli/flora version
./build/flora-cli/flora diag 42     # diagnostic structure + invariant check
./build/benchmarks/flora-bench      # benchmark suite (use a Release build)
```

## Layout

```text
flora-core/    simulation authority: structure, growth, species, environment, RNG
flora-cli/     command-line generation and diagnostics
tests/         doctest unit/property/regression tests
benchmarks/    deterministic benchmark harness
```

Geometry, export, viewer, and engine integration modules arrive in later milestones
(`ROADMAP.md`).
