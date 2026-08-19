# Research: FloraSynth and Procedural Vegetation Techniques

Date: 2026-08-19. Sources: FloraSynth public website/documentation (florasynth.com, docs pages:
introduction, trunk, branching, thickness, tropism, foliage, fruit, geometry, advanced, trimming,
plus SDK docs and the public "Florasynth 2.0" / "2.5" blog posts), and published literature cited
inline. Every claim is labeled **[FACT]** (publicly documented), **[INFERENCE]** (reasonable
deduction from public material), or **[PROPOSED]** (our design decision).

---

## 1. FLORASYNTH FINDINGS

FloraSynth is a browser tool + Blender add-on + native SDK (C DLL with Python/JS-WASM/C++
bindings) for procedural tree generation, by a single developer. It is explicitly a
*simulation-based* generator, not a prefab assembler.

### 1.1 Growth model

- **[FACT]** "Florasynth is a procedural tree simulation engine that models how trees grow,
  respond to their environment, and evolve over time." Trees grow in explicit yearly steps:
  `grow(sim, years)` with a per-year callback. Year-by-year growth replaced the v1 "age slider".
- **[FACT]** Since v2.0 there is no branch-density probability: "Florasynth now attempts to
  create new branches every year from each active growth. Whether these new branches survive
  and continue to develop depends on local conditions like light availability, shading, and
  other growth constraints." Overproduce-then-cull is the core loop.
- **[FACT]** Light drives everything: "Rays are cast inward from a hemispherical sampling
  pattern in the sky toward each branch tip. Depending on how many of those rays are obstructed
  by other branches, a solid approximation of light exposure is calculated" (raycast shade
  simulation). Light level influences whether a growth "pushes forward, slows down, or dies off".
- **[FACT]** Photosynthetic energy is computed per branch tip and "passed down toward the
  trunk"; this energy value drives secondary thickening and pruning decisions.
- **[FACT]** Trees in one simulation shade each other (inter-tree competition); canopies
  "self-organise over time — trees lean and thin where they compete".
- **[INFERENCE]** This is structurally the *self-organizing tree model* family (bud/shoot
  modules + light competition + resource flow + shedding), close to Palubicki et al. 2009,
  with raycast light instead of the paper's shadow-propagation voxels.

### 1.2 Structure representation (conceptual)

- **[FACT]** Trees are built from *growths* (shoots) made of *segments*; parameters reference
  "segments away from the tip" (foliageThreshold, fruitThreshold), per-segment ring geometry,
  and a `segmentLength` parameter. Branches emerge from growth points at `branchJointAngle`,
  `whorls` new branches per growth point per year.
- **[FACT]** The geometry pass decimates by radius (`decimationRadiusMin/Max`), skips rings
  lengthwise (`lengthwiseDecimationRate`), and `thinFineBranches` removes childless terminal
  branches in passes — explicitly *without* modifying the underlying simulation structure
  ("The full branching structure remains intact internally"). Structure and geometry are
  separated concerns.
- **[INFERENCE]** Internal storage is a parent/child segment hierarchy carrying per-segment
  radius, direction, age, energy. Actual data layout, memory model, and threading are not
  public and were not investigated.

### 1.3 Parameter surface (documented UI/JSON parameters)

**[FACT]** Species are flat JSON parameter files (`oak.json`, `pine.json`, ...). Documented
parameters (defaults observed in the public web app):

| Area | Parameters |
|---|---|
| Growth | `baseGrowthLength` (max annual shoot elongation), `baseGrowthThickness` (initial shoot radius), `scaleFactor` |
| Branching | `branchJointAngle` (deg, child emergence angle), `whorls` (new branches per growth point per year) |
| Apical dominance | `apicalDominance` (lateral growth rate as fraction of leader), `apicalDominanceDecay` (per-year weakening) |
| Tropism | `gravitropism` (upward bias), `phototropism` (bias toward brightest direction from shade sim), `gnarl` (random per-year directional offset) |
| Resource priority | `prioritizeBright` (light-weighted growth allocation, 0=off, 4–5+=aggressive) |
| Thickness | `thicknessGrowthFactor` (annual thickening per downstream photosynthesizing growth; apical-dominance-weighted) |
| Pruning | `dropShadedGrowths` (min light for new terminal growths), `dropWeakBranches` (min energy-production / maintenance-cost ratio), `dropLowPerformers` (legacy: production relative to parent), `branchDecay` (deadwood decay rate/year) |
| Bending | `branchStiffness` (material stiffness, typical 50–300), `integrateBend` (fraction of elastic bend made permanent per year) |
| Shade tuning | `leafShade` (occluder size of each tip in the shade sim) |
| Trunk | `trunkFlare`, `trunkFlareWidth`, `trunkFlareTaper`, `trunkFluting{Frequency,Height}`, `trunkRoughness`, `numTrunks`, `trunkSpread`, `minTrunkSpacing` |
| Foliage | `foliageModel` (user OBJ), `foliageSize`, `minFoliageSpacing`, `foliageThreshold` (segments from tip), `foliageAngle` (droop) |
| Fruit | `fruitModel`, `fruitSize` (`fruitModelSize`), `minFruitSpacing`, `fruitThreshold` (auto gravity orientation) |
| Geometry | `faces` (radial segments), `segmentLength`, `decimationRadiusMin/Max`, `lengthwiseDecimationRate`, `thinFineBranches`, `verticalTextureStretch`, `horizontalTextureStretch` |
| Junctions | `meldSteps`, `minMeldRadius`, `meldDistance`, `meldFlareTop/Bottom/Side`, `meldInset`, `maxMeldSize` |
| Age curves | `changesOverAge`, `changesOverIteration` (parameter values varying with age/iteration) |

All parameters are scalar constants; variation between individuals comes only from the seed.
No distributions/ranges in species files (relevant gap, see §3).

### 1.4 Mechanism details (documented behavior)

- **Branching [FACT]**: every active growth tries to branch yearly; `whorls` lateral buds at
  `branchJointAngle`; survival filtered by light and constraints.
- **Growth direction [FACT]**: shoot tips steer by gravitropism (up), phototropism (toward
  highest sampled light — "more responsive to dynamic shade" than gravitropism), and gnarl
  (per-year random offset).
- **Thickness [FACT]**: v1 used distance-from-tip taper; v2 thickens each segment yearly in
  proportion to the count/energy of downstream photosynthesizing growths, weighted by apical
  dominance (0..1 interpolation documented). This is functionally a pipe-model-like rule driven
  by simulated foliage rather than pure topology.
- **Pruning [FACT]**: three mechanisms — (a) terminal growths below a light threshold die;
  (b) branches whose photosynthetic production / transport+maintenance cost ratio falls below
  threshold self-prune ("Eventually the entire tree cannot support the cost of its massive
  trunk, and will die" — whole-tree senescence emerges); (c) legacy relative-to-parent cull.
  Dead branches decay gradually (`branchDecay`), leaving deadwood on older trees.
- **Bending [FACT]**: each segment treated as a cantilever beam ("simplified Bernoulli beam
  theory"); weight-based elastic bending, thicker branches resist "exponentially" better;
  `integrateBend` converts elastic deflection into permanent (plastic) shape per year.
- **Leaves/fruit [FACT]**: instanced user-supplied OBJ clusters placed near branch tips
  (threshold in segments from tip), minimum-spacing thinning, droop angle; fruit auto-oriented
  downward. No per-leaf simulation; foliage is a geometry-stage decoration of the structure.
- **Manual pruning [FACT]**: interactive prune tool cuts branches along a drawn line; cut
  branches taper to zero radius.
- **Randomness [FACT]**: "The same seed and properties will always produce an identical tree"
  (`set_seed`). Per-tree seed; new variation per generation in the web tool via spacebar.
  **[INFERENCE]** RNG stream design and thread-independence are not documented.

### 1.5 Geometry production

- **[FACT]** Ring-based swept tubes: `faces` radial vertices per ring, per-segment rings,
  radius-driven ring decimation, lengthwise ring skipping, trunk flare/fluting/roughness
  vertex displacement. Branch junctions use a "meld" system: the base rings of a child branch
  are conformed to the parent surface (ray-projected, flared, inset) and morphed over
  `meldSteps` rings into the branch's own cross-section — no boolean/skin surface.
- **[FACT]** Mesh API returns interleaved vertex buffers (pos/normal/uv, 8 floats) + indices,
  per mesh type (branches / foliage / fruit); `configure_mesh` is an explicit separate step
  after growth.

### 1.6 Export, serialization, integration

- **[FACT]** Exports: OBJ and GLTF/GLB from the web tool (premium); Blender add-on
  "Make Real" converts sim trees to standard meshes; SDK returns raw buffers.
- **[FACT]** Snapshot API serializes "the full state of the simulation — all trees, their
  growth data, and ages" to a binary blob, reloadable ("Format compatibility" callout exists).
- **[FACT]** Developer SDK: C ABI (`florasynth_*` functions, caller-owned memory,
  `florasynth_free_*`), bindings for Python (numpy), JS (WASM, async grow/mesh), C++ (RAII
  header), Blender.

### 1.7 Performance-related information

- **[FACT]** "the new raycasting-based shade simulation and resource transport system are
  far more advanced ... they are also more computationally demanding. Tree generation may take
  longer than in Florasynth V1." Web tool runs growth in a worker; regeneration is manual
  ("Apply Changes") because growth is slow. v2.5 "finer branching ... pushes vertex counts
  higher" with decimation controls to compensate.
- **[INFERENCE]** Raycast shading is per-tip × per-ray × occlusion tests; cost grows with
  canopy size and tree count. FloraSynth's documented scope is single trees to small groups
  (a Blender scene, "three willows competing"). Nothing public suggests forest-scale operation.

### 1.8 Features useful for games (worth adopting conceptually)

- Overproduce-then-shed branching (emergent, species-agnostic realism).
- Energy-based thickening (natural taper without hand-tuned taper curves).
- Cost/production pruning with deadwood decay (aging character, emergent senescence).
- Plastic bend integration (weeping/drooping forms, aged silhouettes).
- Instanced foliage clusters (industry-standard, performant).
- Radius-driven geometry decimation and childless-branch thinning (LOD-friendly).
- Year-by-year growth with snapshot serialization (growth animation, save/load).
- Data-only species (JSON presets) + strict seed determinism.
- Age-varying parameters (`changesOverAge`) — juvenile vs mature behavior.

### 1.9 What would NOT scale to huge forests

- **[INFERENCE]** Per-tip hemispherical raycasting against all branch geometry — acceptable
  for one hero tree, ruinous for 100k trees.
- Full segment-level simulation for every individual (no fidelity levels).
- Per-branch cantilever bending everywhere.
- No instancing/archetype strategy; every tree is unique geometry.
- No spatial acceleration story, no terrain/environment inputs, no distribution/ecology layer.

### 1.10 Missing functionality we must design ourselves

Forest distribution and stand simulation; multi-fidelity plant representations (statistical →
structural → hero); a light model cheaper than raycasts; environment inputs (terrain, soil,
water, wind, obstacles); species parameter *distributions*; deterministic multithreading;
botanical LOD + impostors; wind metadata; streaming serialization; Unreal adapter; ecology
(reproduction, succession, mortality).

---

## 2. OTHER APPROACHES

### 2.1 L-systems / parametric L-systems

Prusinkiewicz & Lindenmayer, *The Algorithmic Beauty of Plants* (1990); environment-sensitive
"open L-systems" (Měch & Prusinkiewicz, SIGGRAPH 1996). Grammar rewriting over shoot modules;
parametric variants carry continuous state; open L-systems exchange information with an
environment process.
**Strengths**: compact species description, strong botanical pedigree, exact phyllotaxis.
**Weaknesses**: global form is emergent from local rules and hard to art-direct; environment
coupling is awkward (string ↔ world communication); string rewriting maps poorly to
data-oriented/parallel execution; artists don't tune grammars well.
**Verdict**: adopt the *metamer/module* vocabulary (internode, bud, apex) but not string
rewriting as the engine.

### 2.2 Space colonization

Runions, Lane, Prusinkiewicz, "Modeling Trees with a Space Colonization Algorithm"
(Eurographics NPH 2007). Attraction points fill a crown volume; nearest skeleton node grows
toward mean of its attractors; kill radius removes conquered points.
**Strengths**: excellent space-filling and obstacle avoidance; trivially environment-aware
(delete/mask attraction points); good for shrubs, roots, climbers, art-directed crown shapes.
**Weaknesses**: not a growth-through-time model (no age structure, no resources); crown volume
must be authored; species identity weakly parameterized; global nearest-neighbor needs a grid
to avoid O(n·m).
**Verdict**: not the core; useful later as a *crown-shell constraint* option and for roots.

### 2.3 Self-organizing tree models (bud/resource models)

Palubicki, Horel, Longay, Runions, Lane, Měch, Prusinkiewicz, "Self-organizing tree models
for image synthesis" (SIGGRAPH 2009), building on Borchert & Honda (1984) resource-flux
architecture and Sachs' bud-competition ideas. Buds compete for light/space; per-bud light Q
flows to the root; resource v flows back up split by an apical-control weight
(λ, "priority model"); shoots grow proportionally to received resource; shadowed/starved
branches shed. Light via **shadow propagation**: each shoot deposits shade into a voxel
pyramid below itself; bud light = ambient − accumulated shade. Cost O(1) per bud per step.
**Strengths**: realistic emergent architecture; direct hooks for apical dominance, tropisms,
pruning, environment; per-plant data-oriented arrays; embarrassingly parallel across plants;
degrades gracefully (fewer buds = cheaper); the model FloraSynth's results resemble.
**Weaknesses**: needs tuning to hit named species; light model is approximate.
**Verdict**: **this is the core model.** [PROPOSED]

### 2.4 Geometric recursive parameterization

Weber & Penn, "Creation and Rendering of Realistic Trees" (SIGGRAPH 1995) — the SpeedTree-like
ancestry: recursive levels with per-level curves/angles/counts.
**Strengths**: fast, fully art-directable, industry-proven.
**Weaknesses**: no growth, no environment response, no emergent aging; "scattered prefab" look
this project exists to avoid.
**Verdict**: rejected as core; its *parameter ergonomics* (per-order overrides, curves over
position) inform our species schema.

### 2.5 Pipe model / allometry

Shinozaki et al. (1964) pipe model: cross-section ∝ supported foliage amount. Leonardo's rule /
Murray's law: parent^n = Σ child^n with n ≈ 2 (area-preserving) to ≈ 2.49–3. Gives principled
thickness from topology + foliage. FloraSynth's v2 thickness rule is a per-year incremental
approximation of this. **[PROPOSED]** Use incremental pipe-model thickening (energy-weighted,
exponent configurable per species) — cheap, one bottom-up pass per step.

### 2.6 Tropisms, reaction wood, bending

Gravitropism/phototropism as directional biases on shoot orientation are standard (ABoP).
Branch sag: cantilever beam under distributed load, with *plastic* shape integration matching
observed permanent droop (FloraSynth's `integrateBend`; also "reaction wood" counter-bending
in literature). **[PROPOSED]** APPROXIMATE: per-segment small-deflection cantilever with
yearly plastic integration, hero/structural fidelity only; skip at forest fidelity.

### 2.7 Light / crown competition at forest scale

Per-tip raycasts (FloraSynth) = FULL fidelity only. Shadow-propagation voxels (Palubicki 2009)
= cheap intra-crown + inter-crown approximation when the voxel field is shared. Forestry
stand models offer even cheaper abstractions: gap models (JABOWA, Botkin 1972; FORET),
SORTIE (Pacala et al. 1996) with species-specific light extinction and crown geometry;
crown-competition indices (e.g. Hegyi) and "field of neighbourhood" models operate on
position + crown radius + height only. Deussen, Hanrahan, Lintermann, Měch, Pharr,
Prusinkiewicz, "Realistic modeling and rendering of plant ecosystems" (SIGGRAPH 1998) is the
canonical pipeline blueprint: coarse individual-based ecosystem sim (position, radius, vigor)
→ parameterized plant models → heavy instancing for rendering.
**[PROPOSED]** Tiered light: L3 hero = raycast or fine voxel; L2 = shared shadow-propagation
voxel grid; L0/L1 = neighborhood crown-competition index. Same species parameters interpreted
at each tier.

### 2.8 Distribution, succession, mortality

Poisson-disk sampling (Bridson 2007) / dart throwing with density-map modulation for stand
generation; species/moisture/slope masks (Deussen 1998). Succession & mortality from gap-model
literature: growth-dependent mortality plus stochastic background rate; light-driven
recruitment. Seed dispersal kernels for spread. All operate on L0 statistical individuals —
cheap, deterministic with per-cell RNG streams.

### 2.9 Phyllotaxis, leaves, roots

Golden-angle (137.5°) spiral, decussate/distichous arrangements — trivial to parameterize per
species at bud placement. Leaves as instanced clusters near tips (FloraSynth's and the game
industry's approach) — correct choice; per-leaf simulation is OMIT.
Roots: space colonization in a soil half-space produces good visuals; simulation value is low
for games. OMIT until a milestone needs it (CLAUDE.md scope rule).

---

## 3. DESIGN IMPLICATIONS

1. **Core model [PROPOSED]**: bud-modular, resource-allocating self-organizing growth
   (Palubicki-style) on fixed yearly (sub-annual configurable) steps:
   sample light per bud → accumulate energy rootward → allocate resource tipward with
   apical-control weighting → shoot growth (elongation + new metamers + lateral buds by
   phyllotaxis) → orientation (gravi/photo-tropism + gnarl) → pipe-model thickening →
   cost/production shedding → aging/decay. This reproduces every FloraSynth-documented visible
   behavior with published, non-proprietary algorithms, and it parallelizes per plant.
2. **Light is the fidelity axis**: raycast (hero) / voxel shadow propagation (structural) /
   crown index (statistical). Design the environment query interface around "light available
   in direction d at point p" so backends are swappable.
3. **Species schema**: FloraSynth's parameter list is a validated starting vocabulary; extend
   scalars to distributions (mean/stddev/min/max) and add per-branch-order and over-age curves.
4. **Structure**: metamer arrays (internode + buds) with stable indices, SoA, no per-node
   allocation. Geometry (rings, melds, decimation, foliage instancing) strictly derived.
5. **Determinism**: counter/hash-based RNG streams keyed by (worldSeed, plantId, phase,
   entityId, step) — thread-count independent by construction; FloraSynth documents seed
   determinism but not thread independence; we make it a tested invariant.
6. **Forest**: FloraSynth has no answer; adopt Deussen-1998 pipeline: L0 stand sim +
   archetype baking + instancing; promote/demote individuals across fidelity levels
   preserving (species, seed, age) identity.
7. **Geometry**: ring-swept tubes + junction melding + radius decimation is proven and
   LOD-friendly; foliage as instanced clusters; childless-branch thinning as first botanical
   LOD operator.

## 4. UNKNOWN / NEEDS EXPERIMENTATION

- Shadow-propagation voxel resolution/kernel that matches raycast quality closely enough for
  L2; memory budget per crown vs shared forest grid.
- Resource-allocation weighting (Borchert–Honda λ vs FloraSynth-style dominance multiplier):
  which yields more controllable species presets; likely a blend.
- Sub-annual step count needed for smooth growth animation without breaking determinism or
  budget.
- How many baked archetypes per species avoid visible repetition (target experiment: 8/16/32
  variants at typical view distances).
- Bending: whether per-segment cantilever at L2 is affordable for thousands of trees or must
  be baked into archetypes.
- Species preset fitting: whether parameter distributions can be fit from reference
  photos/allometric tables (Python-side experiment, later).
- Cross-platform float determinism limits (documented baseline: per-platform deterministic,
  thread-independent; bit-identical cross-platform unverified).

## 5. ANSWER: CORE REPRESENTATION AND ALGORITHM

**Representation**: plants as flat arrays of *metamers* (internode records: parent, direction,
length, radius, order, age, flags) plus *bud* records, stable 32-bit indices, SoA-friendly,
positions derived on demand. Species = versioned data (parameters as distributions + curves).
Forest = arrays of statistical individuals (position, species, seed, age, height, crown
radius, vigor) with per-individual fidelity level.

**Algorithm**: deterministic fixed-step, bud-based self-organizing growth with tiered light
approximation, Borchert–Honda-style resource allocation with apical-control weighting,
pipe-model incremental thickening, cost/production shedding, tropism-based orientation, and
plastic bend integration at high fidelity — i.e., the published self-organizing tree model
family, engineered data-oriented and thread-count-independent, with FloraSynth's documented
parameter vocabulary as the species-facing control surface.

Rejected as core: pure L-systems (control/parallelism), space colonization (no time/resources),
Weber–Penn (no growth/environment). Each contributes vocabulary, constraints, or later options.
