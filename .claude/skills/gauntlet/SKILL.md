---
name: gauntlet-loop
description: Strict step-by-step development protocol for visual, procedural, graphics, and simulation programming. Enforces atomic single-step changes, metric diagnostics, and human-in-the-loop review checkpoints before advancing.
---

# Gauntlet Loop Protocol

You are operating under the **Gauntlet Loop Protocol**. When this skill is active, you must never write large, multi-feature code dumps or leap ahead across pipeline stages. Every turn consists of exactly one isolated implementation, verified with diagnostics, followed by an immediate halt for user review.

---

## 1. Execution Rules

1. **Strict Atomic Steps**:
   - Implement only **one** isolated subsystem, parameter change, or anatomical/structural target per turn.
   - Never implement downstream features (e.g., foliage, texturing, shaders) until upstream structural primitives (e.g., trunk, branching hierarchy, physics curves) are approved.
2. **Expose Tunables**:
   - Do not hardcode magic numbers. Expose tunables in schemas, configuration files, or inspectable parameter structs.
3. **Run Multi-Seed & Quantitative Diagnostics**:
   - Provide concrete metrics for every change: vertex/triangle counts, branch counts, seed panel variance, angle distributions, or clearance percentages.
4. **Mandatory Execution Halt**:
   - Stop generation immediately after outputting the **Gauntlet Review Checkpoint**. Do not start the next step, do not write hypothetical implementations, and do not assume approval.

---

## 2. Standard Stage Progression Pipeline

Follow this canonical order when generating procedural biological or visual assets:

```text
[1] Base Skeleton & Spine
      └── [2] Primary Whorls & Hierarchy
            └── [3] Kinematics / Gravitropism / Sags
                  └── [4] Higher-Order Branchlets / Streamers
                        └── [5] Abscission & Pruning Rules
                              └── [6] Foliage Cards & Phyllotaxis
                                    └── [7] LODs & Mesh Budgets
```
