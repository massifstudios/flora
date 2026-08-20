# Flora Architecture

Locked technical decisions. Changes here are deliberate design changes, not implementation
details. See `RESEARCH.md` for the evidence behind these decisions and `ROADMAP.md` for
sequencing. Invariants in `CLAUDE.md` override everything.

## 1. Requirements

- Botanically convincing individual trees grown by simulation, not assembled from prefabs.
- Species defined as data; one preset + seed → recognizable species with believable variation.
- Deterministic: (version, species, seed, environment, timestep, settings) → identical
  structure; thread-count independent.
- Scales: one hero tree (full detail) to hundreds of thousands of simulated forest
  individuals and millions of rendered instances.
- Standalone C++20 core; CMake; Linux + Windows; no Unreal/rendering dependency in core.
- Geometry, export, viewer, Unreal are consumers of simulation structure.

## 2. Non-goals

- Scientific plant physiology (organ-level hormone transport, photosynthesis chemistry).
- Per-leaf simulation; leaves are instanced clusters attached to structure.
- Real-time growth in shipping game frame budget (growth is offline/background; runtime
  consumes baked or streamed results).
- Cross-platform bit-identical floats (baseline: deterministic per build/platform config,
  thread-count independent, tested).
- GPU compute before a profiled CPU reference exists.

## 3. Module layout

```text
flora-core/       # simulation authority; zero rendering/engine deps
  math/           # Vec3, Quat, deterministic helpers
  rng/            # hash-based deterministic streams
  structure/      # metamers, buds, stem graph, plant state
  species/        # species definitions, distributions, curves, (de)serialization
  growth/         # simulation phases, per-plant growth pipeline
  environment/    # light backends, terrain/soil/climate inputs, query interface
  spatial/        # grids and neighbor queries
  forest/         # stand state, distribution, fidelity levels, lifecycle
  io/             # versioned binary serialization of plants/forests
flora-geometry/   # ring meshing, junction melding, foliage instancing, LOD, wind metadata
flora-export/     # glTF, OBJ, compact runtime format
flora-cli/        # generation, batch, diagnostics, export
flora-viewer/     # SDL3 + fixed-function OpenGL diagnostic viewer (optional target)
integrations/unreal/  # optional adapter (later)
benchmarks/       # custom micro/macro benchmark harness
tests/            # doctest-based unit/property/regression tests
```

Dependency direction: `flora-core` ← `flora-geometry` ← (`flora-export`, `flora-cli`,
`flora-viewer`, `integrations`). Core knows none of them.

## 4. Coordinate system and units

Right-handed, **+Z up**, meters, radians internally (degrees allowed in species files,
converted on load). Simulation time in years (fixed steps; sub-annual step count is a
simulation setting, part of the determinism key). Converters live in exporters/adapters only
(glTF: +Y up conversion at export; Unreal: cm/left-handed/Z-up at adapter).

## 5. Core representation

### 5.1 Plant structure

A plant is flat arrays of records with stable 32-bit indices (`Index`, invalid = 0xFFFFFFFF):

- **Internode**: parent index, first_child/next_sibling links, unit direction, length (m),
  base radius (m), branch order, creation step, flags (alive/dead/decaying).
- **Bud**: bearing internode index, orientation, type (apical/lateral), state
  (dormant/active/dead), creation step.
- Positions are derived (root-to-tip walk), never stored as authority.
- Pruning marks subtrees dead; compaction is explicit, order-preserving, and remaps indices
  deterministically. Graph invariants (acyclic, valid parents, live references, finite
  non-negative lengths/radii) must hold after every phase; a `validate()` is test-time and
  debug-assert enforced.

One heap allocation per array, never per node. No virtual dispatch in structure or growth
hot paths.

### 5.2 Species

Data, versioned, serializable (JSON on disk). Parameters are **distributions**
(constant | uniform | normal, with clamps) sampled through deterministic streams, plus
optional curves over age and branch order. Vocabulary follows RESEARCH.md §1.3 (growth,
branching, apical dominance, tropisms, thickness, pruning, bending, foliage, geometry hints).
Schema carries a version integer from day one.

### 5.3 Forest

Array of individuals: position, species id, seed, age, fidelity level, and statistical state
(height, crown radius, vigor). Identity key is (species, seed, plantId); promotion/demotion
between fidelity levels regenerates structure deterministically from that key + age.

## 6. Growth simulation

Fixed phase order per step (changing order is an architecture change):

```text
1  sample environment (light per bud, via active backend)
2  accumulate energy and wood volume rootward
3a drop shaded terminal growth (light threshold, childless stems only)
3b shed weak branches (production vs maintenance cost, before resources are spent)
4  allocate resource tipward (apical-control weighted, brightness-biased,
   surviving outlets only)
5  bud fate (dormancy limits, activation thresholds)
6  shoot growth with orientation (tropisms + gnarl steer each metamer;
   phyllotactic lateral bud placement)
7a secondary thickening (incremental pipe model, production/allocation weighted)
7b bending under self weight (cantilever, plastic integration)
8  deadwood decay (delayed removal of shed structure)
9  age, mortality
```

Phase 3a culls childless stems below `dropShadedGrowthLight`; 3b only reaches
branches that have already grown expensive, so without 3a a crown silts up with
unproductive interior twigs. Phase 7a blends its driver between collected
production and apical-control-weighted allocation (`thicknessApicalWeight`): a
tree reinforces the stems it *feeds*, so a suppressed lateral carrying a
productive tip stays slender instead of thickening into a second trunk.

Shedding runs before allocation so no resource is wasted on doomed branches.

Tree form comes from five mechanisms that separate a leader from its branches;
without them a plant grows as a coppiced clump of equal stems:

- **Organ-specific tropisms** — the leader is orthotropic (`gravitropism`,
  `phototropism`), branches are plagiotropic (`lateralGravitropism`,
  `lateralPhototropism`). One shared value turns every branch into a trunk.
- **Gravitropic set-point angle** — a branch shoot steers toward
  `lateralSetPointAngle` from vertical, keeping its azimuth and correcting only
  its inclination (Digby & Firn 1995; classified APPROXIMATE — the visible
  effect is the angle a limb holds, not the auxin transport that holds it).
  `lateralSetPointStrength` 0 falls back to a plain ±Z pull. The set point is
  what makes branch orientation *restoring*: gnarl and stacked branch angles
  are a random walk over inclination, and a plain pull does not correct it, so
  deep branch orders end up pointing in arbitrary directions with half of them
  at the ground.
- **Apical control by order** — each branch order extends `branchVigorFalloff`
  of the order above it, permanently.
- **Apical dominance** — lateral buds within `apicalDominanceRange` of an
  *extending* apex stay latent (they do not draw resource and do not age out).
  A stalled apex releases them, so a leader that runs out of hydraulic reach
  turns an excurrent sapling into a decurrent crown by itself.
- **Borchert-Honda group split** — resource divides between the continuation
  outlet and the laterals *as a group*, v_m = v·λQ_m / (λQ_m + (1-λ)Q_l).
  Weighting outlets individually lets numerous laterals outvote the leader.

Size is bounded by hydraulic limitation, not by a height parameter: extension
growth scales with `1 - (pathLength / maxPathLength)^hydraulicFalloff` over the
root-to-tip stem path, so shoots shorten as the transport path lengthens and
stop at the species ceiling (Ryan & Yoder 1997, classified APPROXIMATE — the
visible effect is the asymptote and the short upper-crown shoots, not the
pressure physiology). Two consequences are load-bearing: apical meristems are
never aborted by the dormancy limit (only unused lateral buds are), so a crown
that has stopped extending keeps its growing points instead of dissolving; and
maintenance cost counts only the outer `sapwoodDepth` shell of each stem, so an
old trunk's heartwood is free and thickening cannot starve the tree it belongs
to.
Bending (phase 7b) treats each internode as a cantilever loaded by the wood beyond it:
deflection is `torque × length / (branchStiffness × radius⁴)` with the load arm taken
horizontally from the internode's base to the subtree's volume centroid. Only the fraction
`bendIntegration` of each year's deflection is applied, and it is applied permanently — the
elastic component is not tracked, so a limb never springs back when it sheds a branch.
Classified APPROXIMATE; `bendIntegration = 0` skips it below structural fidelity. Sag
shortens the load arm, so the process converges; a per-step cap keeps a thin overloaded stem
from folding in one step.

Crown architecture depends on this: without it branches are straight radial spokes, since
tropisms alone never respond to load.

### Fidelity classifications

```text
FULL         raycast per-bud light            hero trees only
APPROXIMATE  voxel shadow propagation light   structural trees; bending; phyllotaxis;
                                             gravitropic set-point angle
APPROXIMATE  crown-competition index light    statistical individuals
BAKE         archetype meshes + impostors     forest rendering
OMIT         roots (until roadmap), per-leaf physiology, molecular detail
```

### Fidelity levels

```text
L0 statistical individual   (allometric growth curves from species params)
L1 crown + trunk abstraction
L2 structural plant         (metamer simulation, voxel light)
L3 hero plant               (raycast light, bending, full geometry)
```

## 7. Environment

Core defines an abstract environment interface; backends are core-owned implementations.
Engine adapters may feed *data* in (heightfields, obstacle sets) but never own queries.
Expensive queries go through `spatial/` acceleration; complexity of any query must be
documented at the call site of its introduction.

Finalized (M6) light query: `Environment::sampleLight(position) -> LightSample
{intensity, direction}` — external light before self-shading. Backends:
`ConstantEnvironment` (reference/tests), `SkyEnvironment` (analytic ambient + sun over an
opaque ground plane — below `groundHeight` no sky is visible, so a drooping limb that reaches
the ground starves instead of growing on underground).
Terrain/soil/water queries join this interface when their milestones arrive.

The raycast backend weights each hemisphere ray by its zenith cosine (uniform-luminance sky).
Weighting rays equally credits a limb under a crown with a full sky's worth of grazing light
and keeps the understorey alive, which flattens a tree's crown against the ground.

Self-shading is simulation-internal, not part of `Environment`: each step rebuilds a
`ShadowVoxelGrid` from the plant's own leafy tips. Two fields are available.
Pyramid propagation (Palubicki 2009; build O(casters × depth³), query O(1))
reaches only `shadowDepth × voxelSize` below a caster — metres, not crown
heights, so a tall crown never shades its own lower branches and never lifts.
`VerticalColumn` instead carries shade down every column with Beer-Lambert
extinction (`shadowVerticalRetention`, `shadowLateralSpread`) in O(cells),
swept at canopy resolution (`shadowColumnVoxelSize`); its deposits are per
square metre of canopy so the field does not change meaning with cell size.
This is the L2 default: crown lift depends on it. Per-bud light = external sample minus
accumulated shadow (own contribution excluded); shaded buds steer toward the field's
negative gradient. Fidelity is a simulation setting (`ShadeModel`): `Off` (reference
path), `VoxelPropagation` (L2 default), `Raycast` (L3 hero: occupancy grid + fixed
Fibonacci-hemisphere rays, transmittance-weighted light direction). Casters are foliage
clusters (M7). At forest scale the grid becomes shared per-cell rather than per-plant
(§14).

Foliage (M7): clusters are derived data, recomputed from structure each step by a
deterministic O(n) placement pass (live internodes within `leafBearingTipDistance` of the
nearest live tip, at least `leafClusterSpacing` apart along the axis path, droop-tilted
orientation; `leafMaxBearingRadius` remains only as a girth cap against stubs).
Proximity to a tip is the primary rule because leaves belong to young shoots: keying bearing
to stem radius instead ties leaf area to the thickening calibration, and since a radius
threshold must sit just above `baseGrowthThickness`, any change to thickening drags the whole
leaf-bearing zone with it — and below that threshold a seedling bears nothing and dies.
Clusters are both the photosynthetic producers — energy = shaded light × cluster area ×
`leafProductivity` — and the shadow casters, so thick leafless interior wood starves and
sheds without any special-case rule. Leaf geometry is instanced cards in flora-geometry,
never per-leaf simulation.

## 8. Determinism

- RNG: stateless hash-based streams (SplitMix64 finalizer over a key tuple), keyed by
  (worldSeed, purpose/phase id, plantId, entityId, step). No global RNG, no time, no
  address-based ids. Golden-value tests pin sequences per version.
- Parallelism model: parallel read/evaluate → synchronize → parallel deterministic apply.
  Work partitioning never feeds back into state; reductions use fixed ordering.
- Iteration order: array index order only; no unordered containers on simulation paths.
- Tests must cover: same-seed reproduction, worker-count independence, stream independence
  (extra draws in one subsystem don't shift another).

## 9. Geometry pipeline (flora-geometry)

Structure → mesh, disposable output:

- Ring-swept tubes, radial `faces` from species geometry hints, radius-driven ring count
  decimation and lengthwise ring skipping.
- Junction melding: child base rings conformed to parent surface, morphed over meld steps.
- Foliage/fruit: instanced cluster placement from structure tips (threshold, spacing, droop).
- Wind metadata: branch depth/order, normalized root distance, stiffness proxy, attachment
  pivots — engine-agnostic vertex/auxiliary channels.
- Botanical LOD operators (before generic decimation): childless-branch thinning passes,
  branch-order culling, radial segment reduction, foliage cluster merging → cards →
  impostor inputs. Silhouette before internal detail.
- Twig cards (M9): a leafy twig — its branchlets and every foliage cluster on them — is
  replaced by one alpha-cut card textured with a photographed twig, and the wood it stands
  for is not swept at all (`BranchMeshParameters::cullBelowRadius`). Cards are cut to one
  twig length so a card always depicts what its texture depicts. This is a geometry-side
  merge: the simulation keeps placing clusters at full density, because they are the
  shadow casters and energy producers.
- Materials are presentation data in a separate versioned file (`flora-materials`), never
  in species definitions: a species is biology, a material set is one look for it. Colour,
  normal and roughness maps travel to exporters and engines; the diagnostic viewer uses
  colour maps only.

## 10. Forest strategy

Deussen-1998-style pipeline: deterministic distribution (Poisson-disk with density/species/
slope/moisture masks, per-cell RNG streams) → L0/L1 stand simulation (neighborhood crown
competition, growth suppression, lifecycle) → archetype baking (N variants per species ×
age class, instanced) → hero promotion where needed. Uniqueness budget: biological identity
for all individuals; unique meshes only for hero/near-field.

## 11. Serialization

All persisted formats versioned (magic + u32 version). Species: JSON. Plant structure &
forest state: compact little-endian binary, explicit field order, no struct dumps, no
pointer/address content. Round-trip tests mandatory. Until first release, format changes are
development-reset (no migration), noted in commit messages.

## 12. Threading

Coarse parallelism across plants (and later forest cells); per-plant growth is
single-threaded. A minimal job interface (std::thread pool) with deterministic partitioning;
no locks inside simulation phases. Single-thread reference path always available; equivalence
across worker counts is a standing test.

## 13. Testing & benchmarks

doctest (vendored single header, MIT) for tests; custom lightweight benchmark harness
(no external dep) reporting counts, per-op time, and derived throughput; comparative, not
threshold-flaky. Benchmarks live beside the systems they measure and run from one executable.

## 14. Performance risks (watched)

- Voxel light field memory at forest scale → shared hierarchical/hashed grid, crown-local
  fallback.
- Bud count explosion in overproduce-then-shed loop → hard caps per plant + early fate
  culling (caps are species parameters, part of determinism key).
- Pruning compaction cost → deferred compaction with dead-fraction threshold.
- Neighbor queries at 100k+ individuals → uniform/hashed grid, benchmarked against brute
  force on small deterministic sets.

## 15. Alternatives considered (rejected)

- **L-system core**: poor art control, string rewriting hostile to SoA/parallel, environment
  coupling awkward. Kept: metamer vocabulary.
- **Space colonization core**: no time/resource dimension; kept as later crown-constraint
  and root option.
- **Weber–Penn geometric core**: no growth/environment; kept: parameter ergonomics.
- **Per-tip raycast light everywhere** (FloraSynth's documented approach): O(tips × rays ×
  occluders) unaffordable beyond hero; kept as L3 backend.
- **ECS framework / general scene graph**: unneeded indirection; flat arrays suffice.
- **Google Benchmark / GTest deps**: doctest single header + tiny bespoke bench keeps
  mandatory deps minimal.
