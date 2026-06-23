# Wave-15 TRUE 1:1 binary diff — the render frame spine (W15-FRAME)

MCP was LIVE this wave. Every function in the segment was decompiled fresh and
compared LINE-FOR-LINE against the reconstruction. The wave-13 NEEDS-LIVE-MCP
queue is RESOLVED. Segment files:
`src/render/frame.{h,cpp}`, `meshlist.{h,cpp}`, `clip.{h,cpp}`, `object_project.{h,cpp}`.

---

## 1. Per-function classification

| Function | Address | Verdict |
|---|---|---|
| RenderMainViewFrame   | 0x5B6074 | VERIFIED-1:1 |
| RenderUniverseFrame   | 0x5B3DE8 | VERIFIED-1:1 |
| BeginUniverseFrame    | 0x5B3900 | FIXED (elided poly-counter resets added) |
| DrawUniverseAndStats  | 0x5B3BBC | FIXED (ScrollUvCoords + 64-list anim walk + trailing resets) |
| RasterizeMeshList     | 0x5AEC88 | VERIFIED-1:1 (incl. the +76>=0 reproject sign-gate) |
| ClipPolygonToPlane    | 0x5AD7D8 | VERIFIED-1:1 |
| ProjectObjectVertices | 0x5AC970 | cull VERIFIED-1:1; projection simplification pre-documented (rule-8 fog boundary) |
| InitEngineDevice dispatch install | 0x5AF984 | VERIFIED-1:1 (6 slots all NullStub13) |

---

## 2. VERIFIED-1:1 (decompile matched the reconstruction)

### RenderMainViewFrame @0x5B6074
Decompile gate `if (byte_649D71 && dword_64A050 <= 0)` == recon
`if (!engineOn || reentrancy > 0) return;`. The two-branch clear selection
(`byte_649D70` -> ClearViewport vs `!byte_62D596` -> ClearRect) and the hoisted
`RenderUniverseFrame(64,1,1)` are byte-faithful.

### RenderUniverseFrame @0x5B3DE8
`BeginUniverseFrame(a1,a2); return DrawUniverseAndStats(a1,a2,1,a3);` — exact
(the third Draw arg is the literal 1, a3 is threaded into the 5th).

### ClipPolygonToPlane @0x5AD7D8
Full line-for-line match: the null guard `!(*a1 | (uint)(a1+8) | a1)`, the
ping-pong list stride `(i&1)<<9` (512 bytes / 128 ptrs), the close-the-poly copy
`v5[dword_649D78]=*v5`, plane dot `a*x+b*y+c*z`, inside test `dot >= plane[3]`,
emit-prev-if-inside, the boundary-cross vertex generation with
`t=(d-dotPrev)/(dotCur-dotPrev)`, the xyz/+12/+44 float lerps, the FIVE
byte-truncated colour lerps (+64,+65,+66,+67,+79 via `(int)((double)base+t*diff)`),
and the `return dword_13D8798 + (i&1)*512` final-side return. `_pad0c` confirmed
to be +12. No divergence.

### RasterizeMeshList @0x5AEC88 — incl. the wave-13 reproject sign-gate
The back-to-front walk (`v44 = count-1 .. 0`), dispatch index default 4 / 3-when-
translucent, the clip-vs-direct branch `((v0|v1|v2)[76] & 0x3F) != 0 || *(v3+4)`,
and the clipped-vertex REPROJECT all match. The wave-13 NEEDS-LIVE item — the
per-survivor `+76 >= 0` SIGN-GATE — is confirmed VERIFIED:
decompile `if (*((char*)*v19 + 76) >= 0)` == recon `if ((i8)VertexClipByte(v) >= 0)`,
and the reproject math is exact:
`r=1/z; sx=flt_13FCD0C*x*r+flt_13FCD18; sy=r*(flt_13FCAF8*y)+flt_13FCD10`
(the `r*(yScale*y)` grouping matches `v23 = v21 * (flt_13FCAF8 * v20[1]) + ...`).
The `dword_649D74 >= 3` survivor floor and the triangle-fan dispatch loop match.

### InitEngineDevice @0x5AF984 — the dispatch-table install
Confirmed the SOLE writer of `dword_13D8780[0..5]` sets ALL SIX to
`VIBE_Raster_NullStub13` (0x5F6EE8) at 0x5AFB87..0x5AFBA5 — verbatim in the
SpanDispatch ctor. The slot[3]=blend / slot[4]=opaque wiring stays the documented
HOST CHOICE (the binary's own slots are NullStub; its real textured path is the
D3D 0x5AE434, off this table). No 7th slot (`dword_13D8798` is the clip-input
array). VERIFIED.

### Depth sentinel value confirmed via get_bytes
`flt_62838C` @0x62838C = bytes `f9 02 15 50` = 0x501502F9 = **1.0e10f**. So the
recon's `if (runningNear == 1e10f) runningNear = 0` is exactly the binary's
`if (flt_13FD168[0] == flt_62838C) flt_13FD168[0] = 0.0`. VERIFIED.

### ProjectObjectVertices @0x5AC970 — backface cull
The LABEL_16 poly loop matches the recon cull: front-candidate gate `+36 & 0x80`,
the `+36 & 0x10` -> set `+36 |= 0x40` keep, and the projected signed-area test
`(v0.sx-v2.sx)*(v0.sy-v1.sy) > (v0.sx-v1.sx)*(v0.sy-v2.sy) && !(+38 & 4)` ->
clear `+36 & 0x7F`. The per-vertex projection in the recon is the simplified
shared form; the binary's actual body is the billboard/distance-fog projection
(byte_649D70 / byte_649DD8 branches, +79 alpha) — pre-documented in
object_project.h as the rule-8 fog boundary (runtime fog globals
flt_13FC544/5AC/58C + dbl_628074). Not a new divergence; left as documented.

---

## 3. FIXED (reconstruction diverged from the binary -> corrected 1:1)

### BeginUniverseFrame @0x5B3900 — elided per-frame poly-counter resets
The decompile resets SIX per-frame accumulators (0x5b3982..0x5b39b8) that the
recon did not model:
```
dword_64A060 = 0;                         flt_13FCF3C = 0.0;
dword_64A058 = 0;   *(*(obj+492)+256) = 0; flt_13FD168[0] = 1e10; *(*(obj+492)+252) = 0;
```
FIX: added `polyCounterA` (dword_64A060), `polyCounterB` (dword_64A058),
`shadowPolyCount` (*(obj+492)+256), `framePolyCount` (*(obj+492)+252) to
FrameState and zero them in the exact decompiled order alongside the existing
runningFar/runningNear. Pinned by `BeginUniverseFramePolyCounterResets`.

### DrawUniverseAndStats @0x5B3BBC — three resolved wave-13 items
1. **ScrollUvCoords** (0x5b3c7a): `v14 = dword_62EB38; ScrollUvCoords(v14);` — was
   only modeled as `timeNow()`. FIX: added `scrollUvCoords(t)` hook, called with
   the frame timestamp at the top of the a2 block.
2. **The unconditional projection WalkAndInvoke** (0x5b3c96) with the
   `v7 = result | 0x181` flag word — was unmodeled. FIX: added `projectWalk(flags,t)`
   hook with `flags = (i16)(appendedPolys | 0x181)`.
3. **The a3-gated 64-list anim pose walk** (0x5b3ca0..0x5b3d13): the binary loops
   `for (i=0..63)`, skips `i == dword_649D60`, reads list head
   `dword_13ECF48[i*246]`, and for each non-null head `!= &dword_13FCF4C` walks
   the linked list via `node[124]` calling
   `UpdateSkeletonPose(off_649D64, node, v7, v14|0x80000000)`. The recon collapsed
   this to a single `updateAnim(a2)` hook. FIX: reproduced the 64-iteration loop +
   the skip-index + the linked-list walk in the frame spine, exposing the leaf
   pieces through `animListHead(k)`, `animSentinel()`, `animNext(node)`,
   `animPose(node,flags,t)` hooks and `FrameState.animSkipIndex` (dword_649D60).
   The per-node `UpdateSkeletonPose` stays in the anim segment.
4. **The trailing per-frame poly-counter resets** (0x5b3c19..0x5b3c50):
   `byte_64A068=0; *(*(obj+492)+256)=0; *(*(obj+492)+252)=*(*(obj+492)+256);
   dword_1408A68=0; dword_1408A64=0;` — were unmodeled. FIX: added `drawFrameFlag`
   (byte_64A068), `mirrorPolyA` (dword_1408A64), `mirrorPolyB` (dword_1408A68) to
   FrameState; the obj+492 counters reuse `shadowPolyCount`/`framePolyCount`. The
   `framePolyCount = shadowPolyCount` copy (== 0) reproduces the
   `*(+252) = *(+256)` write order. These run inside the guarded block regardless
   of a2 (matching the binary). Pinned by `DrawUniverseStatsTrailingResets` and
   `DrawUniverseStatsResetsRunWithoutA2`.

The fps windows A/B were RE-CONFIRMED 1:1: window A numerator `v11 = dword_649DE0+1`
(pre-increment snapshot), window B uses `dword_649DE8` post-increment directly;
both thresholds (`>0x3C`/`>0xA` unsigned, `&& dt`), the `1000*frames/(dt*uDelay)`
integer division, and the accumulator/last-time resets match. The wave-13
"numerator spelling" cosmetic question is RESOLVED: the binary literally uses the
pre-increment `v11` for A and post-increment global for B, exactly as the recon
spells it.

---

## 4. Cross-segment coordination (NON-owned file edited — flagged)

`tests/unit/render_scene_test.cpp` (render_scene segment) has a `FrameWalkOrderAndGate`
golden that drove MY `RenderMainViewFrame` and asserted the OLD single-`anim`
order. After the 1:1 fix that golden is wrong vs the binary (the binary has no
single anim hook; it has scrollUv + projectWalk + the 64-list pose walk). Per the
brief ("if the binary differs from a current golden, the GOLDEN was wrong, fix the
source + the golden to the binary") I updated that one test's hooks + expected
order to the corrected 11-step sequence
(`clearRect,clearRect,terrain,resetLights,sceneWalk,particles,skyFlares,mirrors,
scrollUv,projectWalk,anim`). This is a test-only edit to keep build/ green; the
render_scene segment owner should be aware. No render_scene SOURCE was touched.

No other consumer installs the (now-legacy, retained-for-compat) `updateAnim`
hook, so no production wiring changed behavior.

---

## 5. Build / test status

- `render_frame_spine_test` — **81 checks, 0 failures** (was 55; +26 for the
  resolved resets, the 64-list walk, the skip-index, and the projectWalk flags).
- `render_scene_test` — 62/0 (golden updated to the 1:1 order).
- `render_clip_test` 48/0, `object_project_test` 10/0 — unchanged, verified intact.
- `wire_terrain_bridge_test` 36/0, `wire_atmos_bridge_test` 47/0,
  `mirror_render_wave6_test` 107/0, `frame_integration_wave6_e2e_test` 23/0,
  `render_binder_e2e_test` 32/0 — all green with the new FrameState/FrameHooks.
- itests: wire_terrain 16/0, app_render_frame 10/0, wire_atmos 14/0.
- `libguild.a` links clean (header additions are all default-initialized, no
  positional aggregate init anywhere -> no ABI break).

## 6. Still-deferred (address + reason)

- ProjectObjectVertices @0x5AC970 per-vertex projection: the billboard /
  distance-fog branches (byte_649D70 / byte_649DD8, +79 alpha via flt_13FC544/
  5AC/58C + dbl_628074) remain the documented rule-8 fog boundary — runtime fog
  globals, not reconstructed here. The header already states this.
- The leaf subsystems behind the frame hooks (the scene-graph WalkAndInvoke
  0x5AC738, VIBE_Anim_UpdateSkeletonPose 0x5CD1D8, particle render 0x5E1278,
  ProcessSceneNode 0x5ADD1C, ResetEngineState 0x5AF200) are owned by other
  segments; the spine wires them 1:1 through hooks.
