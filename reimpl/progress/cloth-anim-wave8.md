# Wave-8 W8-CLOTH — Flag / Banner ("Wimpel") animation

Module: `src/render/cloth_anim.{h,cpp}` + `tests/unit/cloth_anim_test.cpp`.

## TL;DR — the key finding (rule 8: documented, not invented)

The brief anticipated a **per-vertex cloth-wave grid** (a sine/cosine vertex
displacement over a flag/banner/sail mesh driven by wind, analogous to the water
wave grid `AnimateWaterWaveGrid` @0x5be428). **No such function exists in
`gilde.exe`.**

- The **only** sine/cosine per-vertex wave grid in the binary is the **water
  surface** (`VIBE_Floor_AnimateWaterVertices` @0x5be428 / `AnimateWaterWaveGrid`),
  already reconstructed by W5-WATER in `render/floorwater` + `render/water_vertices`.
- Flags, banners and pennants in Die Gilde are **skeletal `.baf` bone-animation
  objects**. The wind wave is baked into the animation file and replayed per frame
  by the same skeletal/object-anim pose driver that drives characters and
  vegetation: `VIBE_Anim_UpdateSkeletonPose` @0x5cd1d8
  (`render/skeleton_pose_driver`, already reconstructed). There is no separate
  `AnimateClothWaveGrid`.

### Evidence (addresses + asset names)

| What | Address | Asset / call |
|------|---------|--------------|
| Character flag attach | `0x4b5d98` `VIBE_Character_AttachFlag` | `LoadObjectAnimation(node, "sonstiges\sp_WIMPEL.baf", 1)` on every `dummy_FAHNE` node |
| Building flag mesh+anim | `0x50d01c` `VIBE_Building_LoadAndAlignGebaeudeModel` | loads `%s*sp_BAU_WIMPEL.ogr`, animates with `sonstiges\sp_WIMPEL.baf` (`0x50d530`/`0x50d53c`) |
| City-tower banner | `0x52e2b7`, `0x52ecf5`, `0x55ab0b` | `sonstiges\wimpel_STADTTURM.baf` |
| Flag-node gather | `0x4b62c0` `VIBE_Character_CollectFlagNodes` | matches name substring `sp_WIMPEL` |

`.baf` = bone-animation file ⇒ skeletal. `dummy_FAHNE` (German *Fahne* = flag),
`Wimpel` = pennant, `sp_BAU_WIMPEL` = building pennant. Searches for `segel`
(sail) returned no animator; awnings/sails are part of the same `.ogr`/`.baf`
object pipeline, not a vertex wave.

So the genuine reconstructible work in this module's ownership is the **flag
attach / refresh plumbing** — the code that makes a flag *exist, wave (by
attaching+playing the skeletal flag animation), and show the right heraldry* —
which was **not** previously reconstructed (the sibling `RefreshAllFlags`
@0x4b5fa4 and `SyncTurnState` @0x531e60 are done in `sim/character_recon4_flags`;
the four functions below were the gap).

## Reconstructed 1:1

| Function | Address | Notes |
|----------|---------|-------|
| `AttachFlag` | `0x4b5d98` | Per-node: if node == `dummy_FAHNE`, attach flag obj at `PointThroughBoneChain(node)`, orient 180° (Euler `{0,π,0}` via `MatrixFromEuler`), apply heraldry texture set (`byte-62`) when `heraldry!=0xFFFF`, set `+535=4`, `LoadObjectAnimation(sp_WIMPEL.baf)`, `+530=(old&0xB3)|0x44`, `+529&=~2`. Always returns 1 (keep walking). |
| `ShowFlag` | `0x4b5e90` | Per-node: if node == `sp_WIMPEL` and `heraldry!=0xFFFF`, re-apply the heraldry texture set only (no re-attach/re-animate). |
| `RefreshFlagAnimation` | `0x4b5ef8` | Top-level: gate `universeNode!=0 && heraldry!=0xFFFF && byte_12CE912[536*heraldry] ∈ {5,6,7}`, then scene-graph-walk invoking `AttachFlag` per node. |
| `CollectFlagNodes` | `0x4b62c0` | Per-node: if name contains `sp_WIMPEL`, append node to `acc[++count]`; return `count < 32`. |

### Constants (get_bytes-verified)
- `kFlagYawPi = 3.14159274101257324f` — `0x4b5df2` stores `1078530011` (`0x40490FDB`
  = float π) into Euler Y; X/Z = 0 ⇒ 180° yaw.
- Heraldry texture index = `LOBYTE(word_12CE910[268*heraldry + 42]) - 62`
  (`0x4b5e3f`/`0x4b5ed5`; person table stride 268 words = 536 bytes; word 42 = byte +84).
- Build-type gate `{5,6,7}` (`0x4b5f3b`); flag-node cap `32` (`0x4b62db`).
- Asset strings: `dummy_FAHNE` (`0x61dea4`), `sp_WIMPEL` (`0x61deb0`),
  `sonstiges\sp_WIMPEL.baf` (`0x61debc`).

### Leaves routed through hooks (`FlagAnimHooks`, inert defaults)
`VIBE_Util_StrCmpNoCase` 0x5cb8f0 (default impl provided),
`PointThroughBoneChain` 0x5c8b38, `Object_AttachToUniverseNode` 0x5b3e30,
`Object_ApplyParentTransform` 0x5b7e24, `Object_SelectTextureSet` 0x5b3f54,
`Character_LoadObjectAnimation` 0x426488. `MatrixFromEuler` 0x5cb1bc reused
directly from `render/scene_transform` (the 180° yaw math is faithful, not hooked).
`VIBE_Util_StrStr` 0x5cb930 reproduced as `NameContains` (faithful strstr; empty
needle ⇒ match, matching `0x5cb93c`).

## Tests
`tests/unit/cloth_anim_test.cpp` — 10 tests, **70 checks, 0 failures**:
heraldry bias math, 180° yaw matrix (vs `0x40490FDB`), full `AttachFlag` path,
no-heraldry path, non-`dummy_FAHNE` skip + case-insensitivity, `ShowFlag`
re-apply/skip, build-type gate, `RefreshFlagAnimation` walk, `CollectFlagNodes`
substring+cap, `NameContains` predicate.

Build: `cloth_anim_test` configures, builds, links and passes cleanly. (An
unrelated concurrent agent's `tests/unit/vegetation_anim_test.cpp` fails to
compile — a 1-arg `TEST(...)` misuse — that is not this module.)

## Handoff — exact bind site (orchestrator wires; do NOT edit bind files)

These run on the **CityView3D per-frame / object-refresh path**. The animation
itself is the existing skeletal driver — `cloth_anim` is the *attach/refresh*
arm that ensures the flag object exists with its `.baf` playing and the correct
heraldry, after which `VIBE_Anim_UpdateSkeletonPose` (skeleton_pose_driver) waves
it every frame like any other skeletal object.

1. **When a character/building flag must (re)appear** (heraldry change, building
   placed, player switch — the same trigger as `RefreshAllFlags` @0x4b5fa4),
   call `guild::render::RefreshFlagAnimation(H, person, buildType, nodeFlagBit528,
   player, nodes, nodeCount, produced)` where:
   - `person` = `{universeNode = personRec+97, heraldry = personRec+39,
     heraldryByte = LOBYTE(word_12CE910[268*heraldry+42])}`,
   - `buildType` = `byte_12CE912[536*heraldry]`,
   - `nodes` = the person's universe-node children (name + node ptr) — the host
     supplies these from its live scene graph (the `VIBE_SceneGraph_WalkAndInvoke`
     @0x5ac738 traversal),
   - `H` = `FlagAnimHooks` bound to the real engine leaves (in
     `play/object_mesh_render` / the object-attach wiring).
2. The flag objects it produces are then driven each frame by the **already-wired**
   `UpdateSkeletonPose` call in the object-refresh loop — no new per-frame call is
   needed; the `.baf` IS the wave.
3. `ShowFlag` is the lighter per-node refresh (texture-set only) for when the flag
   object already exists; `CollectFlagNodes` is the gather pass when the caller
   needs the list of live flag nodes (cap 32).

No bind-site/integration file was edited; `progress/INDEX.md` untouched.

## TRUE 1:1 DIFF VERIFICATION (later pass) — CONFIRMED, no changes

Re-decompiled `AttachFlag @0x4b5d98` and `RefreshFlagAnimation @0x4b5ef8`
line-for-line against the source:
- **No-vertex-cloth-wave finding CONFIRMED.** `AttachFlag` contains no
  fsin/fcos/time term; the wave is `LoadObjectAnimation(obj,
  "sonstiges\\sp_WIMPEL.baf", 1)` (skeletal `.baf`) at 0x4b5e6b. The 180° yaw is
  `MatrixFromEuler({0, 1078530011=float π, 0})` at 0x4b5df2/0x4b5e0c. Heraldry
  selector `LOBYTE(word_12CE910[268*h+42]) - 62` at 0x4b5e53. Render-flag
  bookkeeping `+530 = (old&0xB3)|0x44`, `+529 &= ~2`. All match the reimpl.
- `RefreshFlagAnimation` gate `+97 != 0`, `+39 != 0xFFFF`,
  `byte_12CE912[536*h] ∈ {5,6,7}` at 0x4b5f3b, then `WalkAndInvoke(...,
  AttachFlag, 256, ...)`. Matches the reimpl's data-level model.
- `cloth_anim_test`: 84 checks, 0 failures. Both functions **VERIFIED-1:1**, no
  source change required.
