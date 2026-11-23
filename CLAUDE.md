# Flora

Procedural vegetation and forest simulation system. Standalone C++ core; Unreal Engine is optional integration.

Read `ARCHITECTURE.md` for locked technical decisions and `ROADMAP.md` for current scope before non-trivial implementation work.

## Priority

When guidance conflicts:

1. Explicit task requirements
2. Invariants in this file
3. `ARCHITECTURE.md`
4. `ROADMAP.md`
5. Tests
6. Existing code

Existing code is evidence, not authority.

## Invariants

- **Standalone core.** `flora-core` must never depend on Unreal Engine, rendering frameworks, editor APIs, or engine-specific types.
- **Simulation owns state.** Botanical, growth, forest, environment, and deterministic state must not live in rendering or integration code.
- **Geometry is derived.** Structural plant state is source of truth; meshes are disposable output.
- **Deterministic simulation.** Results must not depend on wall-clock time, frame rate, global RNG state, unordered iteration, worker scheduling, pointer values, or nondeterministic IDs.
- **Thread-count independence.** Changing worker count must not change intended simulation result.
- **Species are data.** Prefer parameterized species definitions over species-specific growth implementations.
- **Scale matters.** Avoid accidental O(n²) work and designs that require full-detail simulation or geometry for every forest individual.
- **CPU is reference.** Do not introduce GPU dependency without explicit architecture decision and measured need.
- **Current scope only.** Do not implement future roadmap systems unless explicitly requested.

## Dependencies

Approximate dependency direction:

```text
flora-core
    ^
flora-geometry
    ^
flora-export / flora-cli / flora-viewer / integrations
```

Higher-level modules may depend on lower-level modules. Reverse dependencies are not allowed.

Exact ownership follows `ARCHITECTURE.md`.

## Data and Performance

Prefer:

- contiguous storage;
- stable integer handles or indices;
- explicit ownership;
- compact records;
- bulk processing;
- minimal allocations;
- serializable state.

Avoid without measured justification:

- allocation per small botanical entity;
- pointer-heavy recursive ownership;
- virtual dispatch in hot loops;
- hidden allocations;
- shared mutable state;
- all-to-all forest-scale queries.

Profile before micro-optimizing unless algorithmic problem is obvious.

Performance-sensitive changes should consider complexity, memory, allocations, cache behavior, synchronization, and scaling with plant and structural-node counts.

## Randomness

Randomness must be explicit and reproducible.

Do not use:

- `std::rand`;
- global RNG;
- current time;
- `std::random_device` for reproducible simulation;
- RNG streams whose consumption depends on thread scheduling.

Derive independent streams from stable inputs such as world seed, plant ID, simulation phase, structural ID, and generation index.

Unrelated systems should not share one sequential RNG stream.

## Parallelism

Prefer independent evaluation followed by deterministic application.

Do not use races, lock-heavy designs, or scheduling order as simulation behavior.

When parallelism affects simulation, verify equivalence across worker counts.

## Serialization

Persisted formats currently use a **development reset** model unless architecture says otherwise.

When format changes:

- update version;
- update repository-owned data using that format;
- accept current version and reject incompatible versions;
- do not add migrations or compatibility shims unless explicitly requested.

Never discard non-regenerable user data without explicit permission.

Serialized data must not depend on memory addresses, pointer values, or unordered iteration.

## C++ Rules

Follow rules from the cpp skill.
Comments should explain constraints or non-obvious reasoning, not restate code or repository history.

## Public Contracts

Do not casually change:

- exported APIs;
- serialized schemas;
- seed semantics;
- deterministic ordering;
- coordinate conventions;
- file formats.

If task requires such change, update relevant documentation, versioning, and tests.

## Tests

Changes should have tests when behavior changes.

- Bug fix: regression test.
- Deterministic behavior: determinism test.
- Serialization: round-trip/version test.
- Spatial optimization: compare against simple reference implementation where practical.
- Performance-sensitive algorithm: relevant benchmark.

Never weaken or delete a valid test merely to make implementation pass.

Run verification proportional to change. Use repository-documented commands.

Core changes must remain buildable without Unreal Engine.

Do not claim tests, benchmarks, or builds passed unless they were run.

## Git Safety

Do not overwrite or include unrelated pre-existing changes.

Do not commit, push, reset, rebase, amend, force-push, or rewrite history unless task explicitly authorizes it.

Keep changes logically scoped.

## Working Method

For implementation tasks:

1. Read relevant architecture and roadmap sections.
2. Inspect relevant code and tests.
3. Make smallest complete change.
4. Add or update required tests.
5. Run relevant build/tests/benchmarks.
6. Inspect diff for unrelated changes.

Do not create future abstractions for hypothetical flexibility.

If unspecified choice is local and reversible, choose a reasonable architecture-consistent solution and proceed.

Stop for clarification only when choice could materially affect dependency direction, deterministic semantics, serialization, non-regenerable data, or major public API.

## Done

Implementation is complete when applicable:

- requested behavior works;
- invariants remain intact;
- relevant build succeeds;
- relevant tests pass;
- required tests were added;
- relevant benchmarks were run;
- serialization/versioning was handled;
- no unrelated changes were introduced.
