# Flora

Procedural vegetation and forest simulation system. Standalone C++ core, with Unreal Engine integration as optional adapter. Core must run, test, benchmark, and generate vegetation without Unreal Engine installed.

Primary goals: botanically convincing procedural growth, deterministic variation, large-scale forest simulation, data-oriented performance, and clean separation between simulation and rendering.

Locked technical decisions live in `ARCHITECTURE.md`; current scope and milestones live in `ROADMAP.md`. Read both before starting work. Invariants below override everything else in repo.

## Invariants

- **Standalone core.** `flora-core` must never depend on Unreal Engine, editor APIs, rendering frameworks, or platform-specific game-engine types. Unreal integration depends on core, never reverse.
- **Simulation/presentation separation.** Botanical state, growth state, forest state, environmental state, and deterministic generation data never live in rendering, UI, mesh, or Unreal presentation code. Geometry is derived from simulation structure.
- **Structure before geometry.** Plant is not primarily triangle mesh. Maintain compact structural representation of stems, internodes, buds, branches, roots, foliage attachment, resources, age, and other simulation state. Render geometry is disposable output derived from structure.
- **Determinism.** Generation and simulation are seed-driven and deterministic. Never use global random state, wall-clock time, frame timing, container iteration order, thread scheduling, or nondeterministic IDs as simulation inputs. Same version, species definition, seed, environment, timestep, and settings should produce same result where practical.
- **Thread-count independence.** Changing worker count must not change simulation result. Random streams and update ordering must be designed around this requirement.
- **Fixed simulation steps.** Growth simulation uses explicit simulation time and fixed or otherwise deterministic steps. Never make botanical state depend on rendering frame rate.
- **Species are data.** Morphology and growth behavior should be parameterized through species definitions wherever practical. Do not create separate hardcoded C++ growth implementations for each tree species.
- **Scale matters.** Algorithm acceptable for one hero tree may be unacceptable for forest. Any algorithm operating over plants, buds, branches, attraction points, or environment must have understood complexity. Avoid accidental O(n²) work.
- **Simulation fidelity is explicit.** Expensive biology must be classified as detailed simulation, approximation, bake-time processing, or omitted behavior. Do not silently apply hero-tree algorithms to every forest individual.
- **No premature Unreal coupling.** Do not add `UObject`, `AActor`, `FVector`, `TArray`, Blueprints, Unreal serialization, Unreal logging, or Unreal build assumptions to core.
- **No premature GPU dependency.** CPU implementation is reference implementation unless `ARCHITECTURE.md` explicitly says otherwise. GPU acceleration may be added after profiling proves benefit.
- **Scope.** Implement only current `ROADMAP.md` phase. Do not build ecosystem simulation, roots, wind deformation, Unreal integration, editor tooling, GPU compute, or other future work ahead of roadmap unless explicitly asked.

## Repository Boundaries

Expected high-level ownership:

```text
flora-core/
    botanical structure
    growth simulation
    species definitions
    deterministic randomness
    environment
    spatial acceleration
    forest simulation
    geometry-independent LOD state
    serialization

flora-geometry/
    branch/trunk meshing
    foliage geometry
    topology simplification
    render LOD generation
    wind metadata generation

flora-export/
    glTF
    other standard formats
    compact runtime formats

flora-cli/
    generation
    simulation
    batch processing
    diagnostics
    export

flora-viewer/
    visualization
    parameter editing
    debug views
    simulation playback

integrations/
    unreal/

benchmarks/
tests/
```

Exact directories follow `ARCHITECTURE.md`. Boundary matters more than folder names.

Dependency direction must remain approximately:

```text
flora-core
    ^
    |
flora-geometry
    ^
    |
flora-export / flora-cli / flora-viewer / integrations
```

Higher-level modules may depend on lower-level modules. Core must not know they exist.

## Botanical Model

Prefer explicit botanical structure over mesh-driven procedural generation.

Likely concepts include:

```text
Plant
StemGraph
Internode
Bud
Branch
FoliageCluster
RootStructure
SpeciesDefinition
ResourceState
EnvironmentState
```

These names are not permission to invent abstractions without need. Follow architecture and current milestone.

Structural data should favor:

- contiguous arrays;
- stable integer handles or indices;
- predictable ownership;
- compact records;
- explicit parent/child relationships;
- bulk processing;
- minimal allocations;
- serializable state.

Avoid:

- one heap allocation per bud, node, branch, or leaf;
- pointer-heavy recursive trees;
- virtual dispatch in hot loops without measured need;
- hidden ownership;
- generic object hierarchies for botanical entities;
- storing render objects inside simulation records.

Graph invariants must remain valid after growth, pruning, simplification, serialization, and loading.

## Growth Simulation

Growth should model visible botanical behavior at useful cost, not molecular plant physiology.

Potential simulation phases include:

```text
sample environment
evaluate buds
estimate resources
allocate growth
activate or suppress buds
grow shoots
orient growth
perform secondary thickening
create foliage
prune or shed structure
age plant
apply mortality
```

Actual phases and ordering are defined by architecture. Do not change ordering casually: deterministic simulation behavior depends on it.

When adding biological behavior, answer:

1. What visible result does this produce?
2. What state does it require?
3. What is computational complexity?
4. Does every plant need it?
5. Can it be approximated or baked?
6. Does it remain deterministic?
7. How will it be tested?

If those questions have weak answers, implementation is probably premature.

## Biological Realism

Target is plausible growth, not scientific simulation for its own sake.

Prefer models that improve visible morphology, including where relevant:

- apical dominance;
- branching probability and order;
- internode growth;
- gravitropism;
- phototropism;
- crown competition;
- shade response;
- branch shedding;
- taper and secondary growth;
- phyllotaxis;
- environmental constraints;
- age-dependent behavior;
- resource limitation.

Do not add expensive physiology unless visible benefit justifies it.

For each expensive feature, classify it:

```text
FULL
Detailed simulation required because visual or structural impact is high.

APPROXIMATE
Cheaper model preserves important visible effect.

BAKE
Run expensive process offline and serialize result.

OMIT
Cost exceeds useful visual or simulation benefit.
```

Record important classifications in `ARCHITECTURE.md`, not source comments.

## Deterministic Randomness

Randomness is explicit data.

Never use:

- `std::rand`;
- global RNG;
- current time;
- random-device entropy in reproducible simulation;
- RNG whose sequence depends on worker scheduling.

Prefer deterministic streams derived from stable identifiers such as:

```text
world seed
species seed
plant ID
simulation phase
bud or branch ID
generation index
```

Do not consume one giant RNG sequence across unrelated systems. Adding one random draw in foliage generation must not unexpectedly change branch topology.

Use independent or hierarchically derived random streams where appropriate.

Tests must verify determinism.

## Species Definitions

Species behavior should be parameterized, versionable, and serializable.

Do not hardcode arbitrary species-specific constants inside growth algorithms.

Parameters may include:

- growth rates;
- branching;
- branch-angle distributions;
- apical dominance;
- internode length;
- tropisms;
- pruning thresholds;
- taper;
- thickness rules;
- leaf behavior;
- crown tendencies;
- shade tolerance;
- environmental requirements;
- age behavior;
- mortality;
- controlled variation.

Use distributions where biological variation matters. Do not reduce every parameter to one scalar if variation is required.

Schema changes are serialization changes and must follow serialization rules below.

## Environment

Environmental state is input to simulation, not presentation.

Possible inputs include:

- light;
- terrain;
- obstacles;
- neighboring vegetation;
- soil;
- water;
- climate;
- slope;
- wind exposure.

Do not implement all of these unless current roadmap requires them.

Environment queries must be abstract enough that core does not depend on Unreal collision, Unreal landscape, renderer depth buffers, or engine scene queries.

Expensive environmental queries require spatial acceleration or approximations.

## Spatial Data Structures

Do not assume octree, BVH, hashed grid, uniform grid, or k-d tree is best without workload evidence.

Choose based on actual query:

- local plant neighbors;
- crown overlap;
- branch proximity;
- obstacle lookup;
- attraction-point search;
- light approximation;
- forest density.

Document expected complexity.

Never introduce pairwise all-to-all checks at forest scale without explicit justification and benchmark.

## Forest Scale

Individual-tree realism and forest-scale simulation are separate fidelity problems.

Support, when roadmap reaches them, representations comparable to:

```text
Level 0: statistical individual
Level 1: position + crown + trunk abstraction
Level 2: structural plant
Level 3: full-detail hero plant
```

Names and levels may differ in architecture.

Do not require full branch structure for every distant forest individual.

Promotion/demotion between simulation fidelity levels must preserve identity and deterministic behavior where architecture requires it.

Rendering uniqueness and biological uniqueness are also separate. Hundreds of thousands of biologically unique plants do not imply hundreds of thousands of unique meshes.

## Performance

Performance is design requirement.

Every hot-path change should consider:

- algorithmic complexity;
- memory per plant;
- allocations;
- cache behavior;
- branch prediction;
- data layout;
- synchronization;
- scalability with plant count;
- scalability with structural node count.

Prefer data-oriented processing where it provides measurable benefit.

Do not optimize code merely because it looks low-level. Profile first unless algorithmic problem is obvious.

Avoid hidden allocations in simulation loops.

Large systems need benchmarks early, not after completion.

Relevant benchmarks should report useful quantities such as:

```text
plant count
structural node count
simulation step duration
plants processed per second
geometry generation duration
memory per plant
spatial query cost
worker scaling
```

Benchmark output must be reproducible enough to compare revisions.

## Parallelism

Prefer coarse independent jobs over shared mutable state.

Simulation architecture should make parallel evaluation possible without changing results.

Typical pattern:

```text
parallel read/evaluate
synchronize
parallel deterministic apply
```

Do not add lock-heavy parallel code to compensate for poor state ownership.

No race is acceptable merely because output "looks correct."

When adding multithreading:

- provide single-thread reference behavior;
- test equivalence across worker counts;
- avoid nondeterministic reduction order where floating-point differences affect state;
- document any unavoidable floating-point determinism limits.

## Floating-Point Behavior

Bit-identical cross-platform floating-point output may not always be practical. Do not claim it unless tested.

Required baseline:

- deterministic within supported build/platform configuration;
- thread-count independent;
- stable simulation ordering;
- explicit tolerances in tests where exact equality is inappropriate.

Avoid fast-math or compiler options that undermine required determinism unless architecture explicitly approves them.

## Geometry

Geometry is derived data.

Core botanical simulation must not require triangle meshes to function.

Geometry modules may produce:

- swept stems;
- adaptive radial segmentation;
- junction geometry;
- bark UVs;
- leaf cards or meshes;
- simplified branch structures;
- impostor inputs;
- wind metadata.

Do not generate full geometry for every simulated forest plant unless required by current workload.

LOD should understand botanical hierarchy where possible. Generic triangle decimation alone is not sufficient design for vegetation.

Prefer operations such as:

- removing insignificant twigs;
- collapsing low-priority branch hierarchy;
- reducing radial segments;
- replacing foliage groups with cards;
- preserving silhouette before internal detail.

## Unreal Engine Integration

Unreal is consumer, not owner, of simulation.

When integration phase begins:

- Unreal-specific types stay under integration module;
- translate coordinates and units at boundary;
- convert generated geometry into Unreal assets or runtime buffers;
- translate forest instances into Unreal-native instancing systems;
- expose data to Blueprint only where useful for wiring/editor use;
- never move simulation authority into Blueprint;
- never duplicate botanical algorithms in Unreal adapter.

Unreal integration must remain removable without affecting standalone generator.

Use current Epic documentation when implementing engine APIs. Do not rely on remembered API behavior if it can have changed between Unreal versions.

## Coordinate System and Units

Core uses canonical coordinate convention defined in `ARCHITECTURE.md`.

Do not leak Unreal's axes, handedness, centimeters, or coordinate assumptions into core.

Conversions happen only at integration/export boundaries.

Use explicit units for public parameters where ambiguity is possible.

## Code Style

Prioritize readability, predictable behavior, and performance over cleverness.

For standalone C++:

- follow project conventions defined by formatter/linter and architecture;
- use descriptive names;
- avoid unnecessary abbreviations;
- prefer explicit control flow;
- keep functions focused;
- use RAII;
- make ownership obvious;
- prefer value types and stable handles;
- use `const` meaningfully;
- avoid unnecessary inheritance;
- avoid macros except where build/platform integration requires them;
- avoid exceptions in hot simulation paths unless architecture explicitly permits them;
- avoid hidden work in constructors, getters, or operators.

For Unreal integration:

- follow Epic C++ conventions;
- use `A`/`U`/`F`/`S`/`E` prefixes correctly;
- use `b` prefix for booleans;
- use Unreal reflection only where integration requires it.

Preserve descriptive identifiers.

Good:

```text
BranchThicknessExponent
LightCompetitionRadius
InternodeGrowthRate
PruningThreshold
EnvironmentSampleIndex
```

Bad:

```text
BTE
LCR
igr
pt
idx
```

Prefer guard clauses and early returns to deep nesting.

Maximum 2 indentation levels inside function body where practical. Extract helpers only when they have clear responsibility; do not fragment code mechanically to satisfy style rule.

Comments:

- only where code cannot communicate reason clearly;
- concise and technical;
- explain constraints or non-obvious reasoning;
- never restate code;
- never write historical comments;
- never document old implementation or what changed.

Git contains history.

## Public APIs

Do not casually change:

- exported C++ APIs;
- serialized data;
- species schema;
- structural plant schema;
- forest state;
- file formats;
- seed semantics;
- deterministic ordering;
- coordinate conventions.

If current task requires such change, update relevant design/versioning docs and tests.

Do not preserve bad API forever merely because it exists. If existing design conflicts with architecture or project goals, identify issue before propagating pattern.

Existing code is evidence, not authority. `ARCHITECTURE.md`, tests, explicit user direction, and measured requirements take precedence.

## Serialization

Serialized state must be versioned.

This applies to:

- species files;
- plant structural state;
- forest state;
- generated runtime formats;
- caches whose compatibility matters.

When changing persisted format, determine whether roadmap expects:

```text
development reset
old data may be discarded

migration
old data must remain readable
```

If not specified and change would invalidate meaningful project data, ask before implementing migration policy.

Do not use raw memory dumps of structs as durable file formats.

Serialized results must not depend on pointer values, memory addresses, or unordered iteration.

## Asset and Export Rules

Standard exports are outputs, not source of truth for simulation.

Likely formats include glTF and project-specific compact binary formats. Add formats only when roadmap requires them.

Generated meshes, caches, benchmark dumps, and viewer output should not be committed unless repository explicitly treats them as fixtures or test data.

Do not commit build output.

Common ignored content should include equivalents of:

```text
build/
out/
bin/
generated/
cache/
```

Follow repo `.gitignore` as authority.

## Research and External References

When implementing algorithms based on papers, public documentation, or external projects:

- distinguish documented behavior from inference;
- do not claim knowledge of proprietary internals;
- record important sources in architecture/design docs;
- prefer original papers and official documentation;
- check licenses before incorporating code;
- do not copy code from incompatible licenses;
- derive requirements and algorithms independently when source licensing is unclear.

FloraSynth is reference for product behavior and ideas, not source code to reproduce.

If external implementation conflicts with project invariants, do not copy its architecture.

## Tests

After completing changes, before committing:

1. Decide whether change requires new test.

   - behavior changes need tests;
   - bug fixes need regression tests;
   - deterministic changes need deterministic tests;
   - serialization changes need round-trip/version tests;
   - performance-sensitive algorithms normally need benchmark coverage;
   - pure refactors/style/comments usually do not.

2. Run relevant unit tests and full test suite.

3. Run relevant benchmarks when touching hot paths or algorithms whose performance is part of requirement.

4. Verify standalone build without Unreal Engine.

5. If Unreal integration was changed, also verify Unreal integration build separately.

Never delete, weaken, skip, or rewrite valid test to make implementation pass.

If test is wrong, explain why and fix test explicitly.

## Required Test Categories

Maintain coverage for relevant systems.

### Determinism

Test:

- same seed produces same structure;
- repeated simulation produces same state;
- worker-count changes do not change state;
- unrelated random stream consumption does not alter another subsystem where stream independence is required.

### Structural invariants

Test properties such as:

- graph remains acyclic;
- parent references valid;
- handles reference live records;
- radii remain valid;
- lengths remain finite and non-negative;
- pruning leaves no dangling references;
- deleted nodes are not traversed;
- serialization round-trip preserves valid state.

### Botanical behavior

Prefer structural/numeric assertions over screenshot-only tests.

Examples:

- gravitropism biases expected direction;
- apical dominance suppresses appropriate buds;
- shade response reduces growth;
- pruning threshold removes expected branch;
- thickness follows configured model.

### Spatial structures

Compare optimized query result against brute-force reference on small deterministic datasets.

### Geometry

Test topology and numeric properties where possible:

- finite positions/normals;
- valid indices;
- no invalid references;
- expected bounds;
- deterministic mesh generation.

Visual regression tests may supplement these, not replace them.

## Benchmarking

Benchmark changes independently from correctness tests.

Never make timing threshold so strict that normal machine variance causes flaky CI.

Prefer comparative metrics or generous regression thresholds.

When optimizing:

1. capture baseline;
2. profile;
3. change one meaningful factor;
4. rerun correctness tests;
5. rerun benchmark;
6. record result when decision is significant.

Do not keep optimization that makes code substantially harder to maintain without measurable benefit.

## Build

Core project must support normal CMake workflow unless architecture states otherwise.

Typical verification should include equivalent of:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Use exact project commands documented in repo rather than blindly assuming these if build system evolves.

Development/debug configuration may be used for correctness. Performance claims require optimized build.

Compiler warnings introduced by change must be fixed, not ignored.

## Git

You may use git. Work in this order:

```text
inspect
change
build
test
benchmark when relevant
commit
push
```

- If worktree contains pre-existing user changes, do not overwrite, discard, reformat, or mix them into unrelated work.
- Do not automatically commit pre-existing work merely to clean working tree unless repo policy explicitly requires it.
- Make logically complete commits that build and pass tests.
- Split unrelated changes.
- Commit messages should be short and precise.
- Add body only when reasoning is not obvious.
- State in commit message body that commit was produced by an AI agent when repo policy requires attribution. Do not add fake human authorship.
- Never use `Co-Authored-By` unless explicitly requested.
- Never force-push.
- Never rewrite user's history without explicit instruction.
- Push only after required verification passes.
- Never merge failing branch.

## Working Method

Before modifying code:

1. Read `CLAUDE.md`.
2. Read `ARCHITECTURE.md`.
3. Read `ROADMAP.md`.
4. Inspect relevant existing implementation and tests.
5. Confirm work belongs to current milestone.
6. Identify invariants affected.

Then implement smallest complete change.

Do not respond with broad design discussion when task is implementation-ready. Make concrete change.

Do not rewrite unrelated code while touching file.

Do not build future abstractions "for flexibility" without current use.

Do not add dependency when small local implementation is clearer.

Do not silently reinterpret requested behavior.

If architecture is unclear but implementation choice is reversible, choose strongest default, document reasoning, and proceed.

If choice would lock project into wrong serialization model, dependency direction, deterministic behavior, or major public API, resolve it before implementation.

## Definition of Done

Change is done only when:

- requested behavior exists;
- project boundaries remain intact;
- deterministic behavior remains intact;
- structural invariants remain intact;
- code builds;
- relevant tests pass;
- new tests exist where required;
- relevant benchmark was run for performance-sensitive changes;
- serialization/versioning was handled if affected;
- documentation was updated when architectural behavior changed;
- no unrelated files were modified;
- commit contains only logically related work.

A visually convincing result that violates simulation architecture, determinism, or scale requirements is not done.
