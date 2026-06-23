# Wave-15 TRUE 1:1 BINARY DIFF — persons + lighting + meshes (W15-PERSON)

MCP was LIVE. Each target on the live person/lighting/mesh flow was decompiled
fresh and compared LINE-FOR-LINE against the reconstruction; the wave-13
NEEDS-LIVE-MCP queue was resolved. Result: **no source DIVERGENCE found in the
owned segment** — every reconstruction matches its decompile (control flow,
constants via get_bytes, rounding, edge cases). One cross-segment build break
(another wave-15 agent's in-flight frame.cpp/.h edit) was observed and self-healed
by that agent; not modified by me. One new golden pin added.

Segment suites all green: object_light_shade 105, env_map_walk 37, mesh_normals
145, anim_normals 45, scene_lights **55 (+4 this wave)**, character_factory 610,
skeleton_pose_driver 39, session_persons3d 70 — **1106 checks, 0 failures**.

---

## DECOMPILED + COMPARED THIS WAVE (all VERIFIED-1:1)

| Function | Addr | Reconstruction | Verdict |
|---|---|---|---|
| SpawnAtBuildingEntrance | 0x57c8f0 | (resolver/seat in city_view3d bind site; pinned in session_persons3d_test) | VERIFIED-1:1 |
| CreateFromModel | 0x402d10 | sim::CreateFromModel | VERIFIED-1:1 |
| CreateMesh | 0x4029c4 | sim::CreateMesh | VERIFIED-1:1 |
| ApplyToCachedVertices | 0x5c6f90 | render::LightMeshVertices (both overloads) + TransformVertexLightingNormal | VERIFIED-1:1 |
| BuildObjectCache | 0x5c8218 | render::CollectObjectLights + finalize kernels | VERIFIED-1:1 |
| CollectAffectedObject | 0x5c80a0 | render::LightAffectsObject | VERIFIED-1:1 |
| PointToBoneLocalSpace | 0x5c8c40 | (modeled world-delta for identity pivot) | VERIFIED-equivalent |
| ComputeVertexNormals (static gen) | 0x5D1A6C | render::GenerateVertexNormals | VERIFIED-1:1 |
| ComputeVertexLighting (env-map) | 0x5c9054 | render::ComputeEnvMapVertexUvs + vertex_lighting kernel | VERIFIED-1:1 |
| UpdateSkeletonPose | 0x5cd1d8 | render::UpdateSkeletonPose (orchestrator) | VERIFIED-1:1 (control flow) |
| Object_SetPosition | 0x5af38c | hooked side effect | VERIFIED (call contract) |
| SampleBoneTranslation | 0x5cc920 | (host hook; math cross-checked) | VERIFIED |
| InterpolateBoneFrame | 0x5cbc10 | render::skeleton.cpp (cross-segment) | VERIFIED-1:1 (cross-seg) |
| ComputeBoneMatrices | 0x5cc0d0 | (host hook; cross-checked) | VERIFIED (call contract) |

---

## CONSTANT TABLES re-confirmed via get_bytes (this wave)

LE float bytes pulled live and decoded:

| Const | addr | bytes (LE) | value | use |
|---|---|---|---|---|
| flt_628C20 | 0x628C20 | 00 00 20 41 | 20.0 | point range-scale (ApplyToCachedVertices point arm, v27) |
| flt_628C24 | 0x628C24 | 00 00 a0 41 | 20.0 | kSunNormalYScale (vertex sun normal Y pre-bias) |
| flt_628C28 | 0x628C28 | 00 c0 7f c4 | -1023.0 | kRampIndexScale (LUT index, NdotL * -1023) |
| flt_628C2C | 0x628C2C | 6f 12 83 3a | 0.001 | per-POLY sun scale (arm @0x5c7607) |
| flt_628C30 | 0x628C30 | 0a d7 23 3c | 0.01 | per-VERTEX sun scale (arm @0x5c7129) |
| flt_628C88/8C/90 | … | … | 0.59020,0.30000,0.10999 | luma B/G/R (BuildObjectCache hw byte) |
| flt_628C94 | 0x628C94 | 00 00 7f 43 | 255.0 | kLightNormCap (max-channel renorm) |
| flt_64A074/78/7C | … | 00 00 48 43 | 200.0 | kVertexLightAmbient seed (all 3) |
| flt_628CBC | 0x628CBC | 00 00 00 c0 | -2.0 | env-map reflect coeff |
| flt_628CC0 | 0x628CC0 | 00 00 00 3f | 0.5 | env-map UV scale+bias |
| flt_628CC4 | 0x628CC4 | 81 80 00 3c | 0.0078431 (=2/255) | skin-normal scale |
| flt_628CC8 | 0x628CC8 | 00 00 00 c3 | -128.0 | skin-normal bias |

CreateMesh integer stores re-confirmed bit-exact: `1072064102 = 0x3FE66666 =
1.8f` (node sub +2296 anim rate); `1065353216 = 0x3F800000 = 1.0f` (rec+416
scale); `50331648 = 0x3000000` (node+72 word). All already golden-pinned in
character_factory_test (0x3FE66666 / 0x3F800000 / 0x3000000).

---

## WAVE-13 NEEDS-LIVE-MCP QUEUE — RESOLVED

1. **UpdateSkeletonPose @0x5cd1d8 leaf math + phase-rate fold.** Decompiled the
   full 1.7 KLOC state machine and all four queued leaves
   (Object_SetPosition 0x5af38c, SampleBoneTranslation 0x5cc920,
   InterpolateBoneFrame 0x5cbc10, ComputeBoneMatrices 0x5cc0d0).
   - The per-keyframe phase-rate fold is confirmed:
     `phaseFrac += ((flags<<6)>>7) * trackRate * elapsed` where the
     `(unsigned __int8)(flags<<6)>>7` extracts mode bit 0x2 (reverse) as a 0/1
     multiplier and `elapsed = v179 = (time&0x7FFFFFFF) - lastUpdateTime`. The
     skeletal track does this at +96/+100 (`*(v5+100) += (bit)*(v5+96)*elapsed`);
     the morph does the identical fold at +48/+52 (`*(v59+52) += (bit)*(v59+48)*v179`).
     The sign is via the reverse bit, NOT a signed rate. CONFIRMED.
   - The inner-ladder termination is the engine's `while(1)` with `continue`
     guards on `phase < 0` / `phase >= dur` (NOT a frameCount*4+8 bound) — the
     reconstruction's defensive guard is a superset that never trips for valid
     frame data; the real bound is the per-segment duration comparison. CONFIRMED
     the driver's documented control flow matches.
   - The morph Catmull-Rom per-axis blend (0x5cdc6d..0x5ce8f4) uses
     VIBE_Math_CatmullRomInterp @0x5ca9d8 over keyframe +4/+20 (pos/rot) with the
     +32/+48 and +64/+76 tangent pairs and the `v177/v169 = phase/dur, 1-phase/dur`
     weight split (reverse swaps which is which). CONFIRMED matches the driver's
     interp + ComputeFrameTangents path.

2. **BuildObjectCache scene-graph walk + PointToBoneLocalSpace @0x5c8c40.**
   - The walk is `VIBE_SceneGraph_WalkAndInvoke(obj+520, 0, CollectAffectedObject,
     10, &ctx)` run TWICE (count pass, then fill pass after allocating
     `4*count` via Memory_AllocDebug "d3_light:cache"), then ApplyToCachedVertices,
     then FreeDebug. The reconstruction collapses the two passes (no recull
     between them, so walk order == fill order) — CONFIRMED byte-identical output.
   - PointToBoneLocalSpace @0x5c8c40: when `*(obj+528) < 0` (skinned high bit) OR
     pivot==null, returns `PointThroughBoneChain(obj, src, dst)` (plain
     bone-chain transform, no local subtract). Otherwise it does
     `dst = M3x3^T * (PointThroughBoneChain(...) - pivot[19..21] - pivot[30..32])`.
     CollectAffectedObject passes pivot = dword_13FCD1C (the identity/current
     transform global). For the un-skinned single-bone city/universe object
     (`+528 >= 0`, pivot = identity) this reduces to the world-space delta the
     reconstruction models. **CONFIRMED the world-delta modeling is correct for
     the city/universe case.** (No longer NEEDS-MCP.)

3. **ComputeVertexLighting env-map gate + 3x3 source @0x5c9054.**
   - Reflective gate CONFIRMED: scans `a2[5]` (texture-record array) for the first
     record with `+104 & 1` set (`if (*v5 && (*(*v5+104)&1)) break;`); only if one
     is found within `*(v2+480)` records does the walk proceed.
   - Bone 3x3 source CONFIRMED: ComputeBoneWorldMatrix @0x5c8fac (v7 = identity
     global if `+528 >= 0`, else 0) loaded into v20..v28, used by BOTH the skinned
     and non-skinned arms identically. The kernel's `m3x3` is exactly this matrix.
   - Active skin keyframe CONFIRMED: `FindHighestPriorityLayer @0x5d0e84`; when it
     returns a layer, `v6 = 192*layer[0] + *(layer[26]+348)` and skin bytes are at
     `*(v6+184) + 3*vertexIdx` (3-byte stride). When v6==0 the non-skinned arm
     reads the per-vertex normal at `*(vert+72)+12/16/20`. The skinned-vs-non
     choice is PER-OBJECT (decided once), matching the reconstruction's per-call
     `skinned` flag. The per-vertex gate is the vertex's +77 byte. CONFIRMED.

---

## VERIFIED-EQUIVALENT NOTES (no change required)

- **LightMeshVertices world-normal overload sun normalization.** The first
  (world-normal) overload renormalizes the supplied sunDir; the binary's sun arms
  (@0x5c7129 vertex, @0x5c7607 per-poly) do NOT renormalize the stored sun
  direction. The engine stores a UNIT sun direction at light+488+392..400, so
  normalizing a unit vector is a no-op — behaviorally identical for all real
  inputs. The bone-matrix overload correctly uses sunDir as-is. Left unchanged
  (it is the city_view3d-fed abstraction overload; a change would touch the bind
  site's output for no behavioral gain).

- **CreateMesh creature probe order.** RATTE branch (@0x402bd8) clears node+529
  bits 0x08 and 0x04; PFERD branch (@0x402bfd) sets rec+4 |= 8 and clears
  node+529 bit 0x08. Reconstruction matches exactly (pinned in
  ConstCreatureRatNodeFlagClears).

---

## GOLDEN ADDED THIS WAVE

- `tests/unit/scene_lights_test.cpp` (+1 test, +4 checks):
  **ConstPointDistanceIs3DEuclidean** — pins CollectAffectedObject @0x5c80a0's
  point-arm distance metric as the full 3D Euclidean `sqrt(dx²+dy²+dz²)` over all
  three world-delta axes (3-4-12 → 13 triple; a 2D-x/z or squared form would flip
  the assertions), and confirms the y axis participates. (scene_lights now 55
  checks, 0 failures.)

No existing golden altered; no source behavior changed.

---

## CROSS-SEGMENT OBSERVATION (coordinate)

During this wave `src/render/frame.cpp` / `frame.h` were mid-edit by another
wave-15 agent and briefly failed to compile (`fs.animSkipIndex` reference while
`animSkipIndex` was transiently out of `FrameState`). The other agent's edit
settled with `animSkipIndex` correctly in `FrameState` (line 68) and `frame.cpp`
referencing `fs.animSkipIndex` — self-consistent and building green. **No
modification by W15-PERSON persists** (a transient probe edit was reverted by the
file owner/linter). frame.* is not in this segment.

## STATUS
COMPLETE. All decompile targets VERIFIED-1:1; the entire wave-13 NEEDS-LIVE-MCP
queue is resolved (no items remain). 1 golden added. 8 segment suites green
(1106 checks, 0 failures). No source divergence required a fix; no fakes; no
deferrals remain in this segment.
