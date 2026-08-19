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
./build/flora-cli/flora presets                      # list built-in parameter presets
./build/flora-cli/flora grow 42 30                   # grow one plant, print stats
./build/flora-cli/flora grow 42 30 --preset excurrent --obj tree.obj
./build/benchmarks/flora-bench                       # benchmark suite (Release build)
```

`--obj` writes a solid branch mesh (ring-swept tubes, normals, UVs) viewable in Blender or
MeshLab — import with Z up. `--skeleton` writes a line-element debug OBJ. Until M5 simulates
secondary thickening, preview radii use a pipe-model display heuristic. The interactive
viewer is M8.

## Layout

```text
flora-core/      simulation authority: structure, growth, species, environment, RNG
flora-geometry/  render geometry derived from structure (tubes, decimation, UVs)
flora-cli/       command-line generation and diagnostics
tests/           doctest unit/property/regression tests
benchmarks/      deterministic benchmark harness
```

Geometry, export, viewer, and engine integration modules arrive in later milestones
(`ROADMAP.md`).
