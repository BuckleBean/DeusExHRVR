# Full VR arms: an unfinished prototype, and what is already known

*Draft for inclusion in the repository. Intended for someone who wants to take
this on, not for players.*

Jensen's hands follow your controllers in the shipped mod. This document is about
the attempt to extend that to **whole arms** — upper arm, forearm and the garment
over them, driven by inverse kinematics from the controllers.

It does not work well enough to ship, and it is **off by default and not in any
release**. It is published because the dead ends are expensive to rediscover. If
you are going to work on this, reading this first will save you a week.

Code: branch `feature/full-arm-ik`. Enable with `ControllerArmsIK=1` under `[VR]`.
It requires the existing model-aligned hands (`ControllerHands=1`,
`ControllerHandsModelAlignment=1`, `ControllerHandsMode=15`).

---

## What works

- **The hands are untouched.** The arm solver writes only non-hand arm bones; the
  accepted palm, grip and finger matrices stay authoritative. Keeping the working
  hands was a hard design constraint throughout and it holds.
- **Two-link IK** for shoulder, elbow and wrist, with a pose-history cache so
  repeated draws of one frame reuse the same solve, and strict context gating on
  player, scene, scale, tracking and reference epoch.
- **Arm geometry is extracted from the player's own mesh.** No new artwork. Group
  selection plus per-triangle filters: side, distance from the centreline, and
  arm-bone influence on every vertex.
- **Coat coverage is continuous** after the final refinement, which added the
  omitted garment groups 4/5/6/7. Non-hand arm geometry went from 5,492 to 6,543
  triangles on the coat variant; the coatless variant is 13,439.
- **The runtime solve is reliable.** Instrumented over roughly 9,700 frames of
  ordinary play, every one of sixteen rejection reasons fired **zero** times and
  arms drew on every frame — exactly 8 arm and 12 hand substitutions per frame.
  Whatever is wrong, the solve is not failing or falling back.

## What does not work

1. **A visual artifact while walking.** The author's description: *"When I begin
   walking, there are 'phantom arms' that pivot forward... like the game says,
   move arms forward as visual feedback. But we've also attached my own arms to
   the controllers."* It appears immediately on starting to walk. **Cause
   unknown.** It is not a dropout — see the counter result above.
2. **Shoulder and upper-arm deformation.** This is the most concrete open lead;
   see the next section.
3. **Wrist twist and pinching.** Hand roll is not distributed along the forearm,
   so torsion concentrates at the wrist. The original prototype's blended
   rotation broke the elbow-to-wrist axis — do not reintroduce that.
4. **Shoulder placement is an estimate, not native.** `FullArmIK::Apply` maps a
   fixed model point (0, 0, 587.326) onto the tracked head position using
   game-base heading. The reach clamp can also recompute the shoulder from the
   wrist in folded poses, so it is not guaranteed to stay at its anchor.
5. **Neck and waist closure, and retained coat/body visibility**, remain
   prototype limits.
6. **Physical torso rotation is deliberately not estimated.** Shoulder heading
   comes only from the horizontal game/controller base. Head *position* still
   moves the arms. An earlier head-rotation-driven version caused a twitch when
   the player looked at their own biceps and was removed. Do not reintroduce head
   following to fix the torso limitation.

## The most promising lead: the upper-arm helper bones

Measured against a real native capture — 185 samples of the 162-bone player
model, matched skeleton fixture, skin rotations orthonormalised by SVD before
comparison:

- Helper bones **97/98** and **126/127** rotate **independently of the main upper
  arm**, and those corrections lie along the reference upper-arm axis: maximum
  off-axis residual **0.212°**.
- Their pivots follow the parent upper-arm transform to within **0.016 game
  units** across all samples — so these are rotations at parent-carried pivots,
  not separate translations needing a fit.
- The first helper's signed rotation is approximately **1.8×** the second's,
  with wrapped discrepancy ≤ 0.768° (left) and 0.373° (right).
- The capture contains genuine motion, not one repeated pose: up to 120.3° (left)
  and 165.8° (right) of relative upper-arm change.

**The current solver gives both helpers the main upper-arm transform, losing
those corrections.** That is a proven behavioural difference against real data,
and it is where I would start.

Caveat: this measures the *result*, not its producer. A static search for a
shoulder-specific native calculation found nothing, and the behaviour may be
baked animation channels rather than a procedural rule. Do not assume a formula
exists to be recovered.

## Already ruled out

Each of these was tried and rejected **with evidence**:

- **Removing the "sleeve" group pair (23/24) from the arm selection.** Built,
  installed, and it moved the arms to visibly wrong positions while changing the
  walking artifact not at all. Those groups carry real arm surface; they are not a
  redundant inner copy. Reverted.
- **Relaxing the triangle filters inside the already-chosen groups.** Measured:
  removing the centreline rule and the bone rule entirely adds at most **142
  triangles** on the coat model and **22** on the DLC model. It is not the
  constraint. (It also does not settle whether the *group selection* is right.)
- **Moving clavicles 95/124 with the upper arm.** Tried in the connected
  prototype; no clear visual improvement and it visibly pulls adjoining chest
  surfaces in raised and folded poses. The solver header was restored
  byte-for-byte.
- **Heuristic helper-angle sweeps.** A prior sweep did not recover the native
  rule. Do not present another as new evidence.
- **A claim that no mesh group reaches the shoulder at all.** I wrote this on
  31 October from a nearest-vertex measurement, and **it does not follow**: a
  joint sits *inside* an anatomical surface, so surface vertices need not touch
  it, and nearest-vertex distance measures neither coverage, continuity nor
  closure. Treat the shoulder as an open coverage question, not a settled
  impossibility.
- **A draw-path counter intended to catch the walking artifact.** It was placed
  in the generic model-draw hook, which fires for every object in the scene, so
  it counted walls and furniture — about 726 per frame. The number is
  meaningless. A useful version must separate "no VR palette tag exists for this
  object" from "a tag exists but its witness did not match", and record which
  object the second case belongs to.

## The gate the project set for itself

Before another playable candidate, the project asked for a **side-by-side visual
comparison on the same mesh and pose**: intact native geometry versus the proposed
arm selection, with removed surfaces marked, showing the upper-arm boundary and
accounting for how that boundary is covered or closed. It should also state which
shoulders stay anchored and what happens beyond reach.

The reason is that this is a **geometry and attachment problem as much as an IK
problem**, and solver variants kept arriving before coverage was settled. If
preserving native coverage turns out to need additional chest geometry or mesh
editing, say so before implementing.

## Suggested order of work

1. **Helper articulation** (97/98, 126/127) — proven difference, real numbers.
2. **Coverage and attachment**, through the comparison gate above.
3. **The walking artifact**, with a correctly placed draw diagnostic.
4. **Wrist roll**, preserving the elbow-to-wrist axis and both joint endpoints.

Do not broaden into a body reconstruction before the first two are addressed.

## Building it

- The branch is based on the interaction-curl commit, which is **behind** the
  current release line. That is deliberate: it is the state the work was actually
  built and tested at, and rebasing it onto newer releases risks silently
  breaking something nobody will test. Expect to merge forward yourself.
- The game DLL and the 64-bit companion must be built and installed **together** —
  they carry a transport version and a mismatched pair refuses to connect.
- Build: Visual Studio 2026 Community, x86, NMake, Release, with pinned local
  OpenXR and MinHook sources. The existing generator and paths matter; moving the
  project breaks it.
- The offline suites cover real saved meshes and skeletons, heading combinations,
  pose history and cache behaviour. They are logic checks, not visual validation.
- A transient compiler ICE in `EngineCamera` has been seen. If a failed compile
  leaves a generated object, delete only that object and rebuild; do not rewrite
  working source to chase it.

## Honest summary

A serious attempt that produced working hands-preserving IK, continuous garment
coverage, and a lot of measured negative results — and did not reach usable
arms. The shoulder is the real obstacle, and the helper-bone finding is the best
thread anyone has. Contributions welcome; the author has stopped here.
