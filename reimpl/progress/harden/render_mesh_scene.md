# Wave-H1 hardening — render mesh_scene / mesh_attach_textures / menu_widgets

Chunk owner files:
- src/render/mesh_scene.cpp (+ mesh_scene.h, tests/unit/mesh_scene_test.cpp, tests/e2e/mesh_scene_e2e_test.cpp)
- src/render/mesh_attach_textures.cpp (+ mesh_attach_textures.h, tests/unit/mesh_attach_textures_test.cpp, tests/unit/mesh_attach_lods_test.cpp)
- src/render/menu_widgets.cpp (+ menu_widgets.h, tests)

MCP module gilde.exe, imagebase 0x400000. Every provenance-carrying function was
decompiled (mcp decompile + disasm) and diffed line-for-line. All constants/tables
confirmed with get_bytes; ConvertX/float->int sites checked against disasm.

## Result counts
- VERIFIED-1:1 : 8 functions
- FIXED        : 0
- BOUNDARY     : 1 (SelectLodFrame distance branch — runtime camera/fov globals; math 1:1, lives in node_lod.cpp)
- No-provenance: menu_widgets.cpp (GUI paint helpers; no `gilde.exe 0xADDR` functions to verify)

No source or golden edits were required — every reconstruction already matched the
binary. (Rule: a reconstruction already correct -> VERIFIED, no churn.)

---

## mesh_scene.cpp

### 0x5f4f7c VIBE_Mesh_ComputeBitmapMemorySize — VERIFIED-1:1
Decompile: v1=48; if a1[10] v1=a1[8]*a1[8]+48; if a1[9] v1+=24*a1[8]*a1[8].
Recon reads i32(32)=a1[8], u32(40)=a1[10], u32(36)=a1[9]; identical arithmetic and
branch order. Golden (lines 39/41/43): 48, 16*16+48=304, +24*16*16=6448 — correct.

### 0x5f4e08 VIBE_Mesh_ComputeNodeMemorySize — VERIFIED-1:1
guard `a1 && !(node[521]&1)`; v2=node[68]; v3=node[480]*node[484]+524; if v2>0 add
24*(v2+8); v1=56*node[76]+v3; set bit0 of +521. Recon identical (note v3 recomputed in
the >0 branch exactly as Hex-Rays writes it). Golden 1144 and 530 verified.

### 0x5f4e8c VIBE_Mesh_ComputeObjectMemorySize — VERIFIED-1:1
Signed +104 flag (i8>=0 gates); v3=128; alias branch (bit1): set bit7, a1 =
(a1[76]<<7)+dword_1406A84 (objectPoolBase); re-read +104; +96 gate; v6 = (u8 +124)>>3
round-up if low3 set; bit3 -> raw = v6*w*w else mip.
CONSTANTS confirmed via get_bytes: flt_62C2E8 @0x62c2e8 = 0x40800000 = 4.0f;
flt_62C2EC @0x62c2ec = 0x3eaaaaab = 0.33333334f. kMipAreaScaleA/B literals match.
FLOAT->INT: disasm @0x5f4f11 = `fmul flt_62C2EC; call ConvertX; fistp var_10`.
ConvertX @0x5c6b08 forces RC=truncate (HIBYTE ctrlword=0x1F), frndint, restores.
Net = truncation toward zero -> CoordTruncate((double)v7) == (int)v7. Correct.
Golden mip path: texels=8*8*3=192; 192*4.0*0.33333334 = 256.0000076 -> trunc 256
(verified numerically); golden 128+256 correct. countLodCopies *=+112 when !+113 — match.

### 0x5f4fb8 VIBE_Mesh_ComputeSurfaceMemorySize — VERIFIED-1:1
7288 header; +16 -> +dim*dim then +20/+24/+28 each +dim*dim; 3 mip levels each adding
(dim>>v)^2 if +36+4v set and 8x 0x4000 chunks scanning +48+4j over +32*v rows; +6624
pool (u8 count @+7277, 344 stride, object ptr @+0) -> ComputeObjectMemorySize(*,1);
8x8 descriptors (800-byte rows from +224, 25-dword/100-byte stride). Field indices
v11[6/10/12/15/7/11/13/5/9/17] and the costs 80/40/48/24/20 all match. Signed shift
`i32(4) >> (u8(7281)&0xF)` matches `*(int*)(a1+4) >> (...&0xF)`.

### 0x5f4d80 VIBE_Mesh_ClearDirtyFlags (object-pool branch only) — VERIFIED-1:1
Owned branch `(flags & 0x100)==0`: walks dword_1406A80 slots from dword_1406A84,
128 stride, clears bit7 of +104. Disasm loop reads [result+104] then result+=128 then
writes [result-24] (== +104) &0x7F — net = clear bit7 of +104 per slot. Recon
`p[104]&=0x7F; p+=128` identical. The two scene-list branches (+0x4 / +0x10 -> globals
dword_13FCCFC / dword_13FC760) are out of this module and correctly routed via hooks.

### 0x5f5588 VIBE_Mesh_SumObjectPoolMemory — VERIFIED-1:1
ClearDirtyFlags(20); for each of dword_1406A80 slots (128 stride) if +64>0 add
ComputeObjectMemorySize(slot,0). Recon identical. Golden 128 verified.

### 0x5f51e4 VIBE_Mesh_ComputeSceneMemorySize — VERIFIED-1:1
__usercall(eax=scene, dx=mask, ebx=out). HIWORD(v38)=a2 so the original `v38 & 0x1XXXX`
bit tests map to recon `mask & {0x1,0x2,0x8,0x80,0x20,0x40}` — mapping confirmed
(0x10000->0x1, 0x20000->0x2, 0x80000->0x8, 0x800000->0x80, 0x200000->0x20,
0x400000->0x40). ClearDirtyFlags(v38>>16)=ClearDirtyFlags(a2) -> recon forwards mask.
Group 0x1: 540(+428 if +488), block@+492: 2320 + per-node(384 stride, count u8@+2316)
costs (+244->80*(v+8) using +252; +248->40*+256; +264&&+260->4*+(+260 deref +480));
tail +2312(+8/+12 via +2304); +1404*80; +1408*40. Group 0x2: per-node
ComputeNodeMemorySize(+260) + block+1412. Group 0x8: +624 gate, 3 meshes (116 stride
from +376) -> ComputeMeshMemorySize hook, set +362. Group 0x80: object array at +264
(count = (+260 deref)+480 when +264), 4-byte (native-ptr) stride ->
ComputeObjectMemorySize(*,1). Tail: 0x20 -> +60 + 88*(*+464); 0x40 -> +924.
Every offset, gate, cost and side-effect order matches the decompile. Golden 540,
540+428, 530 verified.

### 0x42756c VIBE_Mesh_ComputeAabbExtents — VERIFIED-1:1
__usercall(eax=tri[3 vtx ptrs], edx=lo, ebx=hi). Per-axis min ladder (v32/v33/v34) and
max ladder (v31 X, v29 Z) reproduced exactly:
  axisMin: m=min(v0,v1); if m>=v2 ret v2; else (v0>=v1?v1:v0)
  axisMax: m=max(v0,v1); if m<=v2 ret v2; else (v0<=v1?v1:v0)
Double-promoted float compares preserved. Return:
  maxX>=lo[0] && minX<=hi[0] && minY<=hi[1] && maxZ>=lo[2] && minZ<=hi[2]
matches `v31>=*a2 && v32<=*a3 && v33<=a3[1] && v29>=a2[2] && v34<=a3[2]`. maxY is never
computed in the binary either (unused). Golden overlap/touch/disjoint cases verified.

(AccumulateVertexAabb 0x5b29d8 reused from mesh_transform.cpp per header note — not
redefined here; out of scope for this chunk.)

---

## mesh_attach_textures.cpp

### 0x5d1114 VIBE_Mesh_AttachStockTextures — VERIFIED-1:1 (under documented 64-bit ptr relocation)
Decompiled and diffed against the recon's named layout. Original 32-bit dword pointer
fields are relocated to native-width slots while non-pointer byte/dword fields keep
their exact engine offsets; the recon is internally self-consistent (writes and reads
use the same relocated slots), so it is behavior-identical to the original 32-bit run.
Verified piece by piece:
- step2 header: a2[2]=stock+68(vertCount), a2[3]=stock+76(polyCount), a2[4]=stock,
  +380=0 -> recon hdr::kVertCount/kPolyCount/kStockPtr/kFlag380; AllocPolysAndPoints hook.
- step3 vert loop `i < a2[2]+8`: +72=stockVertArr(stock+64)+i*24, +76=0, +64=-1, +68=-1
  (-1 stored as 0xFFFFFFFF). Stock array bases read with rdptr (real heap ptrs). Match.
- step4 poly loop (stride 56 stock / relocated 64 draw): UV2/UV1/UV0=0; +38&=0xFE;
  v12[4]=stockPoly; 3 vtx ptrs = drawVertBase+80*idx with vert+76|=0x80; +36=0;
  texRecIdx=stockPoly+36; <0 -> tex=0; else texRec=dword_1406A84+(idx<<7); blend dword
  build (mode110&1 -> 4x trans108; else (trans108<<24)|(0xFF<<16)|0xFFFF) into vert
  +64/+68; two-sided = (flags104&1): poly+38 bit3, vert+77. All match decompile.
- step5 texset: alloc 4*matCount(stock+480); per-mat scan polys for stockPoly+40==mat,
  copy poly tex handle (+20) + IncrementRefCount, else "stock object %s, texture %s not
  found"; then "Not all Textures..." verification. Match (logging via hook).
- step6 LOD bookkeeping (only when a2 != drawData+1396): ++drawData+2316; node+532=4;
  if !node+460 -> SelectLodFrame; fallback scan k*384+244 (k<3, off<1152) for first
  record with polyCount>0 && vertCount>0 && stockPtr!=0.
  CONFIRMED OFFSET MAPPING: original scan checks drawData+v62+256(=rec+12=a2[3]
  polyCount)>0 && +252(=rec+8=a2[2] vertCount)>0 && +260(=rec+16=a2[4] stock)!=0.
  Under relocation those are hdr::kPolyCount(20)/kVertCount(16)/kStockPtr(24) — the
  SAME offsets step2 wrote. Recon checks polyC>0 && vertC>0 && stockP!=0. Faithful.
- step7 ++stock+476 (refcount); return stock. Match.

### 0x5adb6c VIBE_Mesh_SelectLodFrame (adapter SelectLodFrameForNode) — VERIFIED-1:1 / BOUNDARY (distance branch)
The adapter reads raw node fields into a LodObject and delegates the selection math to
render::SelectLodFrame in node_lod.cpp (owned by another agent). Verified:
- guard: v2=node+492; if v2 && (u8)v2+2316 — recon returns 0 when drawData null or
  lodCount<=0 (matches the binary's result=0 path).
- frame validity: original validates result+8 && result+12; relocated to frame::
  kVertCount/kPolyCount which the adapter loads into LodObject frames for SelectLodFrame.
- forced-LOD branch `(node+531 & 0x30)!=0 || !dword_13FCD1C`: recon's view has
  worldPresent=false by default so the forced branch is taken exactly as the original
  does when the camera global is 0. v4 = ((u8)(4*flags)>>6)-1 clamp lodCount-1 lives in
  node_lod.cpp.
- dirty bit: result != node+460 || byte_64A068 -> node+528 |= 0x40; recon applies via
  `dirty` out-param. currentFrameIndex recovered from node+460 for the changed-test.
- BOUNDARY: the distance branch reads runtime universe globals dword_13FCD1C (camera),
  flt_13FC774 (fov), byte_64A068 (force-rebuild). Until SetLodSelectView supplies them
  the no-camera view forces the LOD branch (rule 8: named, not faked). The float->int
  sites in that branch use guild::util::ConvertX (x87 truncate) in node_lod.cpp —
  verified correct there (uses ConvertX, not bare cast/fistp).

### 0x5d1824 VIBE_Mesh_AttachStockObjectLods — NOT IN THIS CHUNK
Only referenced in comments in my files (correctly attributed to mesh_lod_name.*, owned
by another agent). No reconstruction of 0x5d1824 lives in my files; nothing to diff
here. No divergence between my comments and the cited address.

---

## menu_widgets.cpp — no provenance functions
DrawThreeSliceButton / DrawWindowFrame / Blit* / FillRect carry no `gilde.exe 0xADDR`
provenance (header only references VIBE_Menu_RunMainMenu @0x529d08 etc. as context, not
reconstructions in this file). These are GUI compositing helpers behind the SDL/Vulkan
paint boundary; nothing to diff against the binary in this chunk.

---

## Build status
All three owned .cpp files pass `g++ -std=c++17 -I. -Iinclude -Isrc -Ishim -fsyntax-only`
cleanly. NOTE: a full target build currently fails in src/gui/widget_layout.cpp
(`Widget::ld` member missing) — that file is NOT in this chunk (another agent's
in-progress work); it is a pre-existing break unrelated to these changes. My files were
not modified (no divergences found), so no churn was introduced.
