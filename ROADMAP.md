# Flora Roadmap

Every milestone leaves the project building, tested, and runnable. Current phase is marked.
Scope rule: implement only the current milestone (see `CLAUDE.md`).

## M0 — Repository, build, test, benchmark harness  ✅

CMake workspace (C++20, warnings-as-errors for project code), `flora-core` library skeleton,
doctest test runner, custom benchmark harness executable, `flora-cli` stub, `.gitignore`,
CI-ready `cmake -S . -B build && cmake --build build && ctest`.

## M1 — Structural tree graph + deterministic RNG  ✅

`Index`-based metamer/bud arrays (`StemGraph`), parent/child/sibling links, order-preserving
prune + compaction with remap, derived positions, `validate()` invariants; hash-based RNG
streams with golden-value tests; unit/property tests; graph construction/traversal/prune
benchmarks; memory-per-node reporting.

## M2 — Deterministic growth simulation (minimal)  ✅

Fixed-step growth loop with phases 2–5,9 of ARCHITECTURE §6 on a constant-light environment:
bud fate, shoot growth, phyllotactic lateral placement, aging. Same-seed and structural
regression tests. Growth benchmark (single tree to N years).

## M3 — Basic branch geometry  ✅

flora-geometry: ring-swept tubes, radius decimation, normals/UVs; deterministic mesh tests
(finite, valid indices, bounds); OBJ dump in flora-cli for eyeballing. Geometry benchmark.

## M4 — Species definitions  ✅

JSON schema v1 with distributions and over-age curves; species loader; two contrasting
presets; sampled-parameter determinism tests; schema version round-trip.

## M5 — Tropisms, thickening, pruning  ✅

Phases 6–8: gravitropism/phototropism/gnarl, incremental pipe-model thickening,
cost/production shedding + deadwood decay. Botanical behavior tests (direction bias,
suppression, thickness monotonicity, prune correctness).

## M6 — Light and space competition  ✅

Voxel shadow-propagation backend + analytic sky; environment query interface finalized;
self-shading changes morphology tests; hero raycast backend (L3) behind same interface.
Light query benchmarks.

## M7 — Foliage  ✅

Cluster placement from structure (threshold/spacing/droop), foliage geometry instancing,
leaf-shade coupling into light backend.

## M8 — Standalone viewer (first cut)  ✅

Stack: SDL3 + fixed-function OpenGL (system deps only; target skipped when absent).
Debug views: solid bark/foliage, branch orders, light values, buds, vigor/deadwood;
step playback via deterministic re-simulation; orbit/zoom/pan camera.

## M9 — Botanical LOD  (in progress)

Thinning passes, order culling, radial reduction, cluster merge/cards, impostor inputs;
LOD determinism + silhouette metrics.

Done: twig cards (cluster merge → one textured card per twig) with the branchlets they
represent culled from the branch mesh; material sets (colour/normal/roughness) with MTL
export and a textured viewer. Open: thinning passes, order culling, radial reduction as
explicit LOD levels, impostor inputs, silhouette metrics.

## M10 — Forest distribution

Terrain/density/species masks, Poisson-disk with per-cell streams, slope/altitude
constraints; forest state serialization; distribution benchmarks at 100k+.

## M11 — Forest competition & fidelity levels

L0/L1 individuals, neighborhood crown competition, promotion/demotion preserving identity,
suppression/mortality. Worker-count-independence at forest scale.

## M12 — Optimized batch simulation

Job system, SoA passes profiling, memory budgets, archetype baking pipeline.

## M13 — Asset export

glTF (+Y conversion), compact runtime format, wind metadata channels.

## M14 — Unreal integration

Thin adapter: species import, generator invocation, meshes/instances/wind data, streaming.

## M15 — Ecosystem & lifecycle

Reproduction, dispersal, succession, disturbances, deadwood; long-run stand tests.
