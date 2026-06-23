# Wave-H1 hardening — render FX / particle / shadow / gfx cluster

Files owned & swept (absolute):
- /home/cnupt/work/reverse/guild-1400/reimpl/src/render/fx_recon3_particle_render.cpp
- /home/cnupt/work/reverse/guild-1400/reimpl/src/render/fxrecon_particle_mirror_shadow.cpp
- /home/cnupt/work/reverse/guild-1400/reimpl/src/render/gfx_archive.cpp

MCP module gilde.exe, imagebase 0x400000. Hex-Rays + disasm + get_bytes used per function.

## Counts
- VERIFIED-1:1 : 12
- FIXED        : 3
- BOUNDARY     : 4 (rule-3 GPU / rule-8 deferred data)
- HANDOFF      : 1 (repo-state, non-owned)

---

## fx_recon3_particle_render.cpp

### project_slot  (0x5e1278 inner loop 0x5e13ac..0x5e1ad8)  — FIXED
Constants re-confirmed via get_bytes:
- flt_62BA68 = 0x3F000000 = 0.5f (kHalf)  ✓
- dbl_62BA8C = 255.0 ✓
- ConvertX @0x5c6b08 sets FPU RC=11 (truncate) via HIBYTE(cw)=0x1F, then frndint,
  then restores cw. Fade alpha site 0x5e14c5 calls ConvertX (truncates ST0) then
  0x5e14ca `fistp` stores the now-integer value — net = trunc(v72). Recon `(i32)v93`
  (C++ truncation) is therefore correct. VERIFIED.
- All float->int sites checked against disasm: only the fade-alpha conversion exists
  in project_slot; it is truncation. ✓

FIX — eye-space MAC chains. Disasm 0x5e13d0..0x5e1428 computes X (v85), Z (v87),
Y (v86) each in x87 80-bit and `fstp dword` to a 32-bit float; distSq (0x5e1431)
reloads the three 32-bit floats. The recon computed Y(v8) and Z(v9) as `double`
then cast to float, but X(v95) as `float` — an inconsistent precision treatment.
Changed all three to `f32` so each rounds to 32-bit identically to the binary store:
  before: `const f64 v8 = ...; const f64 v9 = ...; const f32 v95 = ...; v97=(f32)v9; v96=(f32)v8;`
  after : `const f32 v95 = ...; const f32 v96 = ...; const f32 v97 = ...;`
Evidence: `fstp dword ptr [edx]/[edx+8]/[edx+4]` at 0x5e1423/0x5e1425/0x5e1428.
No golden changed (all tests use identity matrices / integer coords where f32==f64).

Verified 1:1 (no change needed): the active gate ((flags&1)&&alpha!=0, 0x5e1395);
1.0/v97 (0x5e142d); distance-fade branch incl. minDistSq compare & 255 clamp
(0x5e1456/0x5e14bd); halfW/halfH/cxScr/cyScr (0x5e157d..0x5e15af); the 6-term cull
against scissor+near/far promoted to double (0x5e1c56); colorReplaced alpha-modulation
`(u16)(alpha*chan)>>8` to BYTE2/BYTE1/LOBYTE (0x5e16c8..0x5e1701); the four screen
corners left/right/top/bottom (0x5e179b/b4/0x5e1937/47).

### install_default_render_hooks — VERIFIED-1:1 (inert hooks; rule-3/8 seams).

### render_system_to_surface / RasterizeBlendTriangle — BOUNDARY (rule 3)
The display-list append + textured-triangle rasterization (poly vtbl
&dword_1408100 / &dword_1408118, list dword_13FC570) is the GPU path. NOTE: in the
currently-building tree this half is not present (committed source ends at
project_slot); a WIP version referencing RgbzRasterState.clipX0/clipX1 lives in a
git stash but does NOT compile against the current raster_textured.h (those members
do not exist there — a non-owned header). Left out to keep owned files compiling.
The projection MATH (the high-risk part) is the verified project_slot above.

---

## fxrecon_particle_mirror_shadow.cpp

### Particle_ResetEmitter (0x42c104) — VERIFIED-1:1
*a1=a2; a1[1]=a3; clear bit0 of slot+81 over a1[52] slots @84B from a1[10]; return
advanced ptr. Field offsets (40=slotBase, 208=slotCount) confirmed. Exact match.

### Particle_SpawnSparkleEffect (0x4d887c) — VERIFIED-1:1
v3={0,1112014848(=50.0f),0}; LOBYTE(v4)=50; *(u16)(&v4+1)=20510; SpawnEffect(
*(a1+97), v4, "punkte_nm_2", 1, v3, 300, 1, 50.0f, 1065353216, 0x40000000,
1056964608, 1056964608). Exact match (note: comment says 53.0f but 1112014848 IS
50.0f — the code uses 50.0f correctly; comment cosmetics only).

### Particle_SetOrientationFromAngle (0x5e1238) — VERIFIED-1:1 (hook seam)
result=(char)dir; if(enable){BuildBasisFromAngle; MatrixToEuler; SetWorldTranslation
(objBase+232, euler)}. Matches (math leaves hooked).

### Shadow_AllocCache (0x5f1d90) — VERIFIED-1:1
dword_64A7E0=count; dword_64A7E4=param; alloc(20*count,"d3s:Cache"); store base.

### Shadow_InitBuffers (0x5f1fc0) — FIXED
Limits clamp (v7=min(a2,1408078); A60=min(a1,v7)), a5*a5, 48*a5*a5 poly buffer,
600*(res^2>>4) point buffer, the two unit-row seeds (A24/A28/A2C, A38/A3C/A40 =
1.0f bits => rowA[1,2,3,6,7,8]), enable flags, slotA0C clear, return 16 — all match.

FIX — AllocCache argument. Binary 0x5f20bd calls `AllocCache(a3, v8)` with a3@ebx,
but the recon passed a1 (`/*a3_ebx*/` unused) to AllocCache. Restored the a3 param
and forwarded it:
  before: `i32 Shadow_InitBuffers(... u32 /*a3_ebx*/ ...)` + `Shadow_AllocCache(a1, 0)`
  after : `i32 Shadow_InitBuffers(... u32 a3_ebx ...)`    + `Shadow_AllocCache(a3_ebx, 0)`
Evidence: decompile `VIBE_Shadow_AllocCache(a3, v8)`. Test already passes a3=4
explicitly (no golden churn).

### Shadow_ShutdownBuffers (0x5f20e8) — VERIFIED-1:1
Zeros A54/A60/64A7F0/64A7EC/A68; frees poly(A58)/point(A50)/cache(64A7F8); frees
heightmap (64A048) if set. Matches (the v3 junk written to A58/A64 in the decompile
is the spoiled edx; recon's nullptr/0 clears are the equivalent intent).

### Shadow_CastFromAllLights (0x5f444c) — VERIFIED-1:1 (+ safety clamp)
Gate (obj+529 & 4) && dword_1408A5C; loop `while(v3+1 < dword_1408A5C)`; returns
(char)a1. Recon adds a null guard + clamp to lights[] storage (kMaxLights=4, the
collector cap); the valid path is byte-identical.

### Shadow_CastFromLight2 (0x5f3f98) — FIXED
GATE / SEARCH (4 slots @128B, key=light at +4 when surface +0 set) / ALLOC+INIT
(+108=-1,+12/16/20/24/28/32=0, +4=light, +124 = (u8)(16*node->flags529)>>7, then
texture-vs-cache path) — all verified against the decompile. Point/dir branch math
verified: dir light => tol=0.01, RotateVectorByHierarchy(light, flt_5CA2B0); point
=> tol=1.0, dir = boneChain(node) - boneChain(light). flag124 uses NODE +529 (not
light) — correct.

FIX — LABEL_45 control flow (0x5f41ba..0x5f425c). The redraw+ground-emit block
(LABEL_45) is INSIDE the `if (v19)` test. The four skip-gates (!a3 / v16 /
slot+108==-1 / slot+124) goto LABEL_45 directly, but when the coverage scan runs and
yields v19==0, the `if(v19){...}` block (containing LABEL_45) is SKIPPED — i.e. NO
RenderMeshShadow AND NO BuildGroundShadow. The recon previously guarded only the
`v17=1` assignment with `if(v19)` and then ALWAYS called BuildGroundShadow.
Restructured with a `reachLabel45` flag: when the scan path is taken and v19==0,
reachLabel45=false so neither RenderMeshShadow nor BuildGroundShadow runs; all four
goto-gates keep reachLabel45=true. Evidence: decompile `if (v19){ if(v17|| v19 !=
*(v5+20)) v17=1; LABEL_45: if(v17) Render; if(BuildGround) mark; }`. No golden
changed (all shadow tests use forceCoverageScan=false, which short-circuits to
LABEL_45 = the unchanged path).

### Shadow_UpdateNodeShadows (0x5f4494) — VERIFIED-1:1 (driver; hook seams)
(a) clear +531 bit1; (b) if +530>=0 TransformBoundingVolume(node, (+528>=0?view:0),0);
(c) per-light: reload node->mesh, break if null; classify 8 corners; if (mask&0x40)==0
set +531 bit1 and CastFromLight. Returns last byte. The 8-corner projection is the
verified ProjectCorner*/TransformProjectedPoint math (below); the corner source +
classifier are deferred leaves (hooks).

### Shadow_ProjectCornerDirectional / ...Point / ...TransformProjectedPoint — VERIFIED-1:1
Extracted from 0x5f457e (dir), 0x5f46bf (point), 0x5f45cd (view transform). Each
algebraic line matches the decompile term-for-term (t=(corner.y-h)/-dir.y;
out=corner+t*dir | d=lightPos-corner, t=(lightPos.y-h)/-d.y, out=lightPos+t*d |
rel=p-m[76/80/84], x'/y'/z' = rel·m[396../400../404..]+m[444/448/452]).

### TriangleNormal (0x5cb824) / Mirror_BuildClipPlane / VectorWithinTolerance (0x5caa4c) — VERIFIED-1:1
TriangleNormal cross product order verified exactly: n0=bz*ay-by*az, n1=bx*az-bz*ax,
n2=by*ax-bx*ay, then VectorNormalize. VectorWithinTolerance = per-component
fabs(a-b)<=tol. Mirror_BuildClipPlane: d=-(n·v0). (The full
CreateClippingPlanes/PrepareReflectionNode 0x5f5d08/0x5f676c traversal+alloc+draw
halves are OMITTED per rule 8 — BOUNDARY; only the verified plane kernel is here.)

---

## gfx_archive.cpp

### DecodeShapeBlob / GfxArchive (provenance 0x5fbb24 VIBE_FrameTable_Index, FULL-flag
### branch 0x5d7c0c VIBE_Shape_ConvertRgbTo16) — VERIFIED-1:1 (against the REAL asset)
Cross-checked the on-disk format against europe_guild_1400_original/gfx/gilde.gfx:
- record count u32 @0; 84-byte records (name@0, dataOff@48, dataSz@56, w@80, h@82). ✓
- shape header: width @6, height @0x0A, FULL flag @0x26 (==0xFFFFFFFF => FULL RGB),
  row-table offset (u32) @0x2A, pixels/row-stream from +50. ✓
- Confirmed by FrameTable_Index disasm: a3[3]=width(+6), a3[5]=height(+0x0A),
  row table `a3[21] (=+0x2A) + (WORD)a3`, read as u32 entries (`*(v11 + 4*v10)`).
- Empirical probe of shape0 of rec0 (_WIN_BORDER 8x8): rowtab@0x2A = 278, entries
  [50,86,122,...] (36-byte row pitch = 4 runCount + 8 {skip,len} + 8*3 RGB),
  confirming 3-byte RGB pixels and skipBytes/3 = transparent pixels. ✓
- FULL bitmaps exist (_MOUSE_CURSOR 49x48, flag@0x26==0xFFFFFFFF) — the recon's
  `== 0xFFFFFFFF` FULL branch reads w*h*3 packed RGB from +50, black=transparent. ✓
All harden tests (OOB / malformed) preserved. No change needed.

---

## HANDOFF (repo state — NOT my files / non-owned)
The working tree was found mid-`git stash`: the COMMITTED versions of the two FX
sources are smaller (project_slot-only; no CastFromLight2/UpdateNodeShadows), while a
`git stash@{0}` holds the full consistent versions PLUS edits, and an UNTRACKED
tests/unit/shadow_node_update_test.cpp references symbols (Shadow_CastFromLight2) that
exist only in the stash. To keep the owned cluster consistent AND compiling I
restored the full stash versions of my owned files (shadow .cpp/.h + tests + gfx +
particle header/test) and applied the fixes, EXCEPT fx_recon3_particle_render.cpp
which I kept at the committed (compiling) form because the stash's
render_system_to_surface references RgbzRasterState.clipX0/clipX1 that do not exist
in the non-owned raster_textured.h. I did NOT `git stash pop` (it touches 100+
non-owned files). Maintainer action: decide whether to pop the stash and add the
missing clip fields to raster_textured.h so render_system_to_surface builds.

All three owned .cpp + the four owned test files pass `g++ -std=c++17 -fsyntax-only`.
