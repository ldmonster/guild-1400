# Wave-H1 Hardening — chunk render_03

Owner: orchestrator (mesh_transform.cpp) + 7 fan-out sub-agents (parts a–g).
MCP live, module gilde.exe, imagebase 0x400000. Every provenance'd function was
decompiled AND (where the x87 stack mattered) disassembled, then diffed line-for-line.

This file is the chunk-level index. Per-file detail lives in render_03_part_{a..g}.md
and the mesh_transform section below.

---

## src/render/mesh_transform.cpp  (orchestrator-owned) — VERIFIED-1:1 (8/8)

All 8 provenance'd functions diffed against the binary; every constant confirmed via
get_bytes/get_global_value; no float->int truncation sites (the dequant math is all
float/x87 stores, no fistp/ConvertX int conversions). No edits required.

- 0x5c9d04 `TransformPackedVertices` — VERIFIED-1:1.
  Dequant `(=(double)(__int16)byte + flt_628CDC) * flt_628CD8`. Constants confirmed:
  flt_628CDC = 0xC3000000 = -128.0, flt_628CD8 = 0x3C008081 = 1/127.5.
  Matrix 3x3 multiply into vtx+32, 80-byte stride, count = mesh[2] = I32(mesh,8),
  vtxBase = *mesh = mesh[0]. Null-packedSrc path calls mesh[4]+492 clear thunk.
  Note: original reads the byte stream as `*(a3+184)+v6`; the recon takes the already-
  resolved byte pointer as `packedSrc` (documented in the header) — a faithful
  abstraction of the +184 indirection, byte logic identical.
- 0x5c9c58 `TransformVertexNormals` — VERIFIED-1:1. isRoot=0 matrix, normal source at
  *(vtx+72)+12, rotate into vtx+32, returns last source pointer (matches eax = result).
- 0x5c9e64 `ComputeBoundingBox` — VERIFIED-1:1. Branch A (mesh+380==0): 8 corners in
  place. Branch B: min/max reduce over 116-stride sub-meshes, box record at
  *(*(sm+132)+348)+192*I32(sm,28), float fields [15..20]. The 8-corner expansion order
  (v21[] writes) verified entry-for-entry against the recon box8[][3]; min vars
  v24=minX,v25=minY,v26=minZ, max v27=maxX,v23=maxY,v28=maxZ. Init 1e10/-1e10. Returns
  *mesh + 80*I32(mesh,8).
- 0x42698c `ComputeHeightRange` — VERIFIED-1:1. obj+533==4 gate, TransformBoundingVolume,
  ComputeBoundingBox(mesh=*(obj+460), mat=*(obj+492)+8), corner-y min/max over 8 corners
  (stride 80). Returns 1 on no-op/empty, 0 on success.
- 0x426bec `ComputeBoundingRadius` — VERIFIED-1:1. radius default 100.0; corner sum
  (corner0 init + corners 1..7) * flt_6115B0; flt_6115B0 = 0x3E000000 = 0.125 (confirmed
  via get_bytes). Max-corner-distance loop, sqrt. The +492 corner-buffer vtable thunk is
  modeled as the `cornerBuffer` callback (faithful; the original does the same indirect
  call `*(*(obj+492+244+16)+500)`).
- 0x5c5084 `AccumulateVertexBounds` — VERIFIED-1:1. obj+460 mesh, cull gate
  `(flags&0xC)==0 || (flags&0x10)` on obj+530, min/max grow over verts (80-stride,
  box[0..2]/box[4..6]). Returns 1.
- 0x5b29d8 `AccumulateVertexAabb` — VERIFIED-1:1. Same grow, NO cull gate. Returns 1.
- 0x428898 `ResetVertexColors` — VERIFIED-1:1. enable!=0 stamps 0x80 into vtx+68/69/70
  over the sub-mesh ptr-array (10-ptr stride, mesh+12 count), sets obj+530|2 & obj+528|4;
  enable==0 clears obj+530&~2, BuildObjectCache, obj+528|4. Returns obj.
- 0x428928 `SetVertexColors` — VERIFIED-1:1. dl=b,cl=g,bl=r. Store mapping confirmed:
  *(v+70)=b, *(v+69)=r, *(v+68)=g → recon StampVertexColors(c68=g,c69=r,c70=b). Any
  non-zero → stamp + flags; all-zero → clear + BuildObjectCache.

Compiles clean (g++ -std=c++17 -Iinclude -Isrc -Ishim -fsyntax-only).

---

## Fan-out results (parts a–g)

### part a — mesh_transform_walk, mesh_transform_unowned_stubs, morph_blend_walk, node_lod, object_anim
VERIFIED-1:1 = 6 · FIXED = 0 · BOUNDARY = 2.
node_lod (SelectLodFrame 0x5adb6c — two ConvertX truncate sites confirmed, flt_62807C=-1.0;
ClassifyBoundingBoxPlanes 0x5ad1f4; CullNodeAgainstFrustum 0x5ad588), morph AccumulateMorphBlend
(0x5c953c), TransformMeshVerticesByMatrix, ComputeFrameTangents 0x5ccf70 / BuildFrameTangents
0x5cec40 (Catmull-Rom flt_628D4C=0.5) all 1:1. BOUNDARY: TransformBoundingVolume 0x5ad438 /
BuildObjectCache 0x5c8218 are deliberate shared placeholders owned by transform/light agents.
NOTE (no edit): ObjAnimFrame.dur is loaded by the binary with `fild dword` (signed int32) but
stored as float in the reimpl; bit-identical for integral durations; struct consumed outside chunk.

### part b — mirror, mirror_project, mirror_silhouette
VERIFIED-1:1 = 7 · FIXED = 1.
No float->int sites anywhere (all fcomp/fstp). Constants reconfirmed: flt_62C3A0/62C39C=2.0,
dbl_62C2F0=-0.01, near-gate 0x33D6E555. FIXED: PrepareReflectionNode 0x5F676C not-found return
(binary returns the last child examined via `result=*v6++`, not the node); sole caller discards
eax so behaviorally unobserved — correctness-of-record. Boundaries: debug allocator, RotateVectorWithFrame
(camera-coupled), runtime frustum table dword_13DB398 (math feeding them is 1:1).

### part c — model_io, modelio_recon
VERIFIED-1:1 = 11 · FIXED = 2 · BOUNDARY = 1.
FIXED: ComputeNormals 0x5f8eb0 face-normal SIGN FLIP (TriangleNormal arg order v8,v7,v6 →
(v1-v0)x(v2-v0)=+Z; golden strengthened to pin nz==+1). ComputeBounds 0x5f8f98 radius compare
done on 80-bit sqrt vs float radius (was (float)sqrt before compare); 1e10/-1e10 = 0x501502F9/
0xD01502F9. VERIFIED: ComputeChunkSize, 7 material Bio thunks (stride 224), ResourceFindFreeSlot/
EvictOldestEntry/FreeEntryData. BOUNDARY: FreeEntryData passes raw entry ptr to FreeBlock (ecx);
typed view can't expose it — rule-4 hook gets fileHandle. LoadSyntheticModel/Model::View carry no
provenance (synthetic format; real BGF loaders DEFERRED).

### part d — object_light_shade, object_project, object_transform_ops
VERIFIED-1:1 = 6 · FIXED = 5 (all the float->int round-vs-truncate bug class).
FinalizeVertexShadeSoftware (B/G/R bare fistp @0x5c83e0/f1/0402 — now std::lrint),
FinalizeVertexShadeLuma (fistp @0x5c8506 + unsigned >=0xFF compare), ApplyMaterialVertexShade
(fistp @0x5c8017 + removed a spurious lower v<0 clamp the binary lacks), PointLightDiffuse LUT
index (fistp @0x5c7427), SunFalloffIndex (fistp @0x5c7241). Golden fixed: luma green 200*0.59=118
(was 117). Header note: 0x5ac970 is VIBE_Particle_UpdateBillboards (provenance comment corrected).
*** HANDOFF (NOT owned): src/render/light.cpp AccumulatePointLight + finalize helpers (~lines
23,57-59,78, FalloffIndex ~135) carry the SAME truncate-instead-of-round bug; apply std::lrint
and bump the same green-channel golden. ***

### part e — paintbox, paintbox_shape, perf_overlay
VERIFIED-1:1 = 5 · FIXED = 1.
PaintboxDrawScaledRegion 0x41e920, PaintboxDrawLine 0x41ec40 (disasm-recovered true arg order
a3=y1,a4=x1 — Hex-Rays mislabels), PaintboxClear 0x41ee9c, Surface_BlitPaletteToPixels 0x422F80,
Surface_BlitRgbToPixels 0x422EE4 all 1:1. FIXED: Surface_CopyRegionRgb 0x423050 dest triple was
stored R,B,G; disasm proves normal R,G,B (UnpackColor R→dst+0,G→dst+1,B→dst+2 @0x4230aa..da).
Source + golden fixed. perf_overlay.cpp = new in-house FPS HUD, no provenance, nothing to diff.
Boundaries: DDraw Blt fill + surface lock (rule-3 GPU).

### part f — particle, particle_emitter_create
VERIFIED-1:1 = 7 · FIXED = 5 functions (8 divergences) · BOUNDARY = 2.
WRONG-FIELD bugs (disasm is reference): UpdateGravity 0x42c42c re-emit t1 = (py + -55)*baseVz
(was a1*damping; fld[edx+3Ch]/fmul[ecx+8]); UpdateScatter 0x42cde8 life decay uses baseVz not
damping (fmul[ebx+8] @0x42ceb7). x87 extended-precision dt: px uses extended dt, py/pz use the
float-rounded fdt across UpdateEmitter/SeedParticles/UpdateScatter; UpdateTrail keeps dt extended
for all four. ConvertX 0x5c6b08 confirmed truncate (frndint RC=11). All ~70 constants confirmed,
kRandNorm=0x38000100=1/32767. BOUNDARY: scatter rejection-shell 80-bit sqrt compare; Unlink owner
head/tail caches + off_649D64 scene root (out-of-tree).

### part g — particle_integrate, particle_render, particle_spawn
VERIFIED-1:1 = 6 · FIXED = 2 functions (3 divergences) · BOUNDARY = 2 · HANDOFF = 1.
UpdatePoints/UpdatePolys phase accumulator: phase advance multiplies r0=wrapped ang0 (FPU top),
not dt (disasm 0x5e2446/0x5e2f22); Lens reloads dt fresh @0x5e3738 so it IS dt-driven (was already
correct). UpdatePolys spawn dir: removed spurious *2, fixed Y/Z swap (0x5e2a3f: dir=[draw0,draw1,0]).
UpdatePoints ring-spawn speed start/end were inverted (0x5e212b: start=min, end=max). Goldens updated
(phaseStep helper). All constants reconfirmed via get_bytes. BOUNDARY: rain diffuse word in un-recon
render head; D3D DrawPrimitive (rule-3, raster math 1:1). HANDOFF: AllocSystem 0x5e1000 collapses one
eax (owner+nullguard+size+count) — reimpl split into owner/slotCount; equal in real caller, recommend
collapsing param in a follow-up.

---

## Chunk totals
VERIFIED-1:1 = 50  ·  FIXED = 16 functions  ·  BOUNDARY = 12

## Open handoffs (outside chunk render_03 — do NOT edit here)
1. src/render/light.cpp — AccumulatePointLight + finalize helpers + FalloffIndex carry the same
   float->int truncate-instead-of-round bug (use std::lrint; bump green-channel golden). [part d]
2. AllocSystem 0x5e1000 param collapse (owner==slotCount in real caller). [part g]
3. ObjAnimFrame.dur signed-int32 load vs float store — bit-identical for integral durations;
   struct lives in play/scene_view.*. [part a]

## Build notes (pre-existing, NOT introduced by this chunk)
- src/gui/widget_layout.cpp: `Widget` has no member `ld` (untracked, another owner) — blocks full
  cmake build. [observed by part f]
- src/sim/character_recon5_transport.cpp: unrelated compile error (another owner). [observed by part g]
All render_03 files compile standalone with g++ -std=c++17 -Iinclude -Isrc -Ishim -fsyntax-only.
