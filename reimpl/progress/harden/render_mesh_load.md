# Wave-H1 Hardening — render mesh load chunk

Files owned:
- src/render/mesh_load.cpp / mesh_load.h
- src/render/mesh_asset.cpp / mesh_asset.h
- src/render/mesh_lod_name.cpp / mesh_lod_name.h
- src/render/mesh_stock_object.cpp / mesh_stock_object.h
- tests/unit/mesh_stock_object_test.cpp, tests/unit/render_mesh_asset_test.cpp

MCP: gilde.exe @ imagebase 0x400000 (live). Every function with a `0xADDR`
provenance was decompiled + diffed line-for-line against the binary; constants
re-verified with get_global_value / get_bytes; key control flow cross-checked
against disasm.

## Counts
- VERIFIED-1:1 : 8
- FIXED        : 5  (across mesh_load.cpp x3, mesh_asset.cpp x1, mesh_stock_object.cpp x1 + 2 golden-test files)
- BOUNDARY     : 3

---

## mesh_load.cpp

### StrNCopyPad (namespace helper) — gilde.exe 0x5D9360 — FIXED
Evidence: decompile 0x5D9360 — a copy loop `for(i=a1; a3; --a3){ if(!*a2) break; *p++=*a2++ }`
FOLLOWED BY a pad loop `while(a3){ *p++=0; --a3 }`. It ZERO-FILLS the whole n-byte
window, it does NOT write a single NUL at the copy point.
Before: `for(i<n && src[i]) dst[i]=src[i]; dst[i]=0;`  (one terminator only).
After:  copy up to n stopping at src NUL, then zero-fill the remaining slots up to n.

### LoadBgfPostProcess — gilde.exe 0x5D2348 (post-parse pipeline) — FIXED (1 divergence) + rest VERIFIED-1:1
Decompiled the full 0xF51-byte VIBE_Mesh_LoadBgfFile and diffed every stage:
- Vertex dedup (0x5d2425..): pair scan, poly-index remap (==b→a, <b: -1), MemMove
  tail up, count-- without advancing b — VERIFIED-1:1 (disasm 0x5d24b8 confirms
  `*((_DWORD*)v16+6)` = +24/+28/+32, remap `if(v13==v17)=v12 elif(v13<v17)=v17-1`).
- Morph-target bake (0x5d25dc..): the full euler rotation vx/vy/vz → out, verified
  term-by-term against st-stack ops. Constants re-read: dbl_6290CC=-pi/2, dbl_6290D4=pi,
  dbl_6290DC=-1.0 — VERIFIED-1:1.
- Material dedup pass1 (memcmp 224, remap +40) — VERIFIED-1:1.
- Material dedup pass2 compaction: remap threshold is the KEPT-count (ecx/v48). The
  recon walk-index `a` is provably equal to v48 (used entries advance `a`, removed
  entries don't) — VERIFIED-1:1 (disasm 0x5d2900 `cmp ecx,edi`).
- Material reorder + count: reordered.size() == v146 (post-pass2 all remaining are
  referenced) — VERIFIED-1:1.
- Per-poly UV bake (0x5d2bcf..): vScale/uScale/half + rotation, second pair uses
  rotation+flt_6290E8(pi). flt_6290E4=0.5, flt_6290E8=pi(float) re-read — VERIFIED-1:1.
- **FIXED — texture flag2 selector.** Binary (0x5d2db2 `cmp byte ptr [eax+80h],0`)
  tests `name2[0]` (+128), NOT presentFlag (+192). The two branches differ only by
  the `|1` bit. Before: `if (mat.presentFlag)`. After: `if (mat.name2[0])`.
- Texture flag word assembly (v176 / flag1 / flag2 low bits), slot id `(ptr-base) sar 7`
  (signed, →-1 when null) — VERIFIED-1:1.
- Dummy copy (0x5d2ef7..): +92←+64, +96←+68, +100←+72,…,+112←+84, StrNCopyPad name —
  VERIFIED-1:1.

### BuildParsedFromBgf — adapter — FIXED (material-name disk-slot mapping)
Evidence: fast-chunk reader 0x5F87B8 reads the 3 material strings in disk order into
v59,v61,v60 and treats the 2nd disk string (v61) as the +128 name (top texture
priority) and the 3rd (v60) as the +64 name. The recon reader (bgf_loader, out of
chunk) fills bm.name1=2nd-disk, bm.name2=3rd-disk. So the correct map onto the
224-byte material is bm.name0→+0, bm.name1→+128(name2), bm.name2→+64(name1).
Before: name0→name0, name1→name1, name2→name2 (swapped +64/+128 priority).
After:  name0→name0, name1→name2(+128), name2→name1(+64).

### LoadBgfFile / morph fast-path — BOUNDARY
0x5D2348 dispatches fast-chunk-first: a real fast-chunk file is loaded by
VIBE_Model_LoadFastChunk and only gets ComputeBoundingExtents; the FULL post-process
pipeline runs ONLY on the AGF text path (VIBE_ModelIo_ReadChunkTag → ParseBlock).
The recon feeds fast-chunk output through the AGF post-pipeline (documented in
mesh_load.h) because the AGF *token* parser is a separate deferred module. Likewise
the morph-target fast path (caller-supplied +516 name block; gate
`a4 && a3>0 && a5==v146`) is not reachable from the fast-chunk entry and is modeled
as the always-allocate branch. Reason: data/subsystem not in this chunk (rule 8 ask).

## mesh_asset.cpp

### Mesh/Model/Anim_LoadByName + VfsSlurp — 0x5D2348/0x5F87B8/0x5E450C — BOUNDARY
VFS open/slurp front-ends. The parse logic is the reused 1:1 core; only the OS read
is the IFileSystem-shim boundary (rule 6). VERIFIED at the boundary level.

### Model_WriteFastChunk — gilde.exe 0x5F9558 — FIXED (vertex-block gate) + VERIFIED rest
0x5F9558 is the WRITE side of VIBE_Model_FastChunkIo (its serializer half). Diffed the
on-disk layout against BOTH the binary writer (0x5F9558 disasm 0x5fa27a) and the
authoritative reader (0x5F87B8):
- Header order magic, matCount, vtx, poly — VERIFIED (magic 0xFAB50005 == -88801275
  re-confirmed; matches reader +480/+68/+76).
- Material string write order is disk[0],disk[1],disk[2] in both binary (+0,+128,+64)
  and recon (name0,name1,name2) — identical on disk (the recon's name1/name2 are the
  positional disk slots; see BuildParsedFromBgf fix for the priority mapping).
- Material flag bytes: binary emits a 6-byte PACKED encoding from the 224-byte struct;
  the recon emits the BgfModel's 6 opaque flag bytes (its reader reads them back the
  same way). Self-consistent round-trip; NOT byte-identical to the 224-byte packing
  (the BgfModel lacks those fields). Handoff below.
- **FIXED — vertex block gate.** Binary writes the (vtx+8)-record block only when
  vertexCount>0 (write loop `*(model+17)>0`; ComputeChunkSize v4=24 when vtx==0).
  Recon wrote 8 records unconditionally. Now gated on `m.vertexCount > 0`.
- chunkSize: recon `4 + body.size()` now equals VIBE_Model_ComputeChunkSize (0x5f9430)
  with the vtx>0 gate — VERIFIED.
- Poly matIndex byte/dword (<=254 → byte, -1→0xFF), dummy block — VERIFIED.

### Mesh_LoadOrFindByName — gilde.exe 0x5D345C — VERIFIED-1:1
Diffed against 0x5d345c: BuildLodFileName(name,dir,base,0,key) → Find(key) else
Find(base); miss → LoadOrFind(base,key); "_s" variant; multi-LOD frames 1..2 with the
`while(!BuildLodFileName(..i..)){ if(++i>=3) return }` shape — exact match. Note the
cache LoadOrFind passes name=key (a2) as the mesh name, matching the binary
(LoadBgfFile(path_from_a1, a2)).

## mesh_lod_name.cpp

### BuildTexturePath — gilde.exe 0x5d1034 — VERIFIED-1:1
"*"+name+suffix (unk_628F14 == "*" re-read via get_bytes 0x628F14 = 0x2A), buf[272],
then VFS resolve routed through the textureExists hook (BOUNDARY: rule-6 VFS).

### BuildLodFileName — gilde.exe 0x5d15fc — VERIFIED-1:1
- a4<0 "_s" variant gated on `(i8)byte_64A098 < 0` (LOD enabled). aS_8="_s".
- a4==0 mode-2: the goto-LABEL_5 probe `%s_%i` downward (1,0), then plain-name re-test;
  the recon while-loop reproduces the exact dec/goto/re-test sequence (traced
  identical: 1→0→plain→return0). Second sprintf index == first (edx survives,
  disasm 0x5d1764 `push edx`).
- a4>0: switch-LOD flips a4=2-a4 (v6==2), v42=a4-1, `%s_%i`. VERIFIED.

### AttachStockObjectLods — gilde.exe 0x5d1824 — VERIFIED-1:1 (hook-routed) / BOUNDARY
String strategy + draw-block offsets (244 / 1396 / frameOff+244, frame stride 384,
cap <1152, lod<3, frameOff+=384 only on attach-success) all match 0x5d1824 exactly.
The FindStockObject gate + the `if(*(obj+492)==0) AllocDrawData` conditional are
subsumed into the attachStockTextures/allocDrawData hooks (object +492 draw-data
layout + Object_AllocDrawData @0x5b107c + AttachStockTextures @0x5d1114 are deferred
out-of-chunk object subsystems). Minor fidelity gap: the attachExisting branch calls
allocDrawData unconditionally rather than gated on found && +492==null (invisible with
inert hooks). BOUNDARY: object-subsystem internals not in this chunk.

## mesh_stock_object.cpp

### LoadAndRegister — gilde.exe 0x5d32d4 — FIXED (record key) + VERIFIED rest
Diffed against 0x5d32d4 + its caller 0x5d345c:
- LoadTextureSet(a2) else LoadTextureSet(a1) order — VERIFIED.
- Method-ptr table stock[122..126] = Delete/Transform/Interpolate/ComputeBB/GetRadius
  (0x5d1968/0x5c9c58/0x5c953c/0x5c9e64/0x5d329c) at +488/+492/+496/+500/+504 —
  VERIFIED-1:1.
- Append link block: stock[128](+512)=prevTail; tail=stock; stock[127](+508)=sentinel;
  prevTail+508=stock — order + semantics VERIFIED-1:1 (the recon's 564/572 native-ptr
  slots are the documented 64-bit repack of engine +508/+512).
- **FIXED — record key.** Binary opens the path composed from a1 (`BuildTexturePath(a1)`)
  but passes a2 to LoadBgfFile as the stored NAME (`LoadBgfFile(v13, a2, …)`, 0x5d33a4),
  so the record is keyed by a2 (== v7, the value FindStockObject(v7) is searched with
  first in 0x5d345c). The recon stored a1. In the live caller (WireMeshLoadOrFind /
  0x5d345c with secondName==name) a1==a2==name so both are equivalent, but the fix
  restores 1:1 (store `dir`/a2). Golden test updated accordingly (see below).

### FindStockObject — gilde.exe 0x5d10d0 — VERIFIED-1:1
v2=searchHead; if==sentinel ret0; while(StrCmpNoCaseN(name,v2,63)!=0){ v2=*(v2+508);
if==sentinel ret0 } ret v2. Exact match (StrCmpNoCaseN 0x5e0db0, 63-char). VERIFIED.

## Golden tests fixed (binary is truth)
- tests/unit/mesh_stock_object_test.cpp: LoadAndRegister calls switched to a1==a2
  (the live-call pattern; the binary stores a2 as the record key). All
  Find/list-discipline assertions hold under the corrected key.
- tests/unit/render_mesh_asset_test.cpp: unaffected by the Model_WriteFastChunk vtx
  gate (round-trip uses vtx=3); BuildParsedFromBgf name-slot fix is invisible (name0
  only). No assertion change needed; re-verified.

## HANDOFFS (out-of-chunk, not edited)
1. src/render/bgf_loader.{h,cpp}: the fast-chunk reader's material-name disk slots are
   labelled name0/name1/name2 = disk[0]/disk[1]/disk[2], but the binary reader
   (0x5F87B8) treats disk[1]=+128(top priority) and disk[2]=+64. My BuildParsedFromBgf
   fix compensates for this labelling; if bgf_loader is later corrected to decode the
   priority itself, revisit BuildParsedFromBgf. Also bgf_loader.h:31 comment cites a
   stale magic 0xFAB7E6C5 (actual kBgfFastChunkMagic=0xFAB50005 is correct).
2. src/gui/widget_layout.cpp: PRE-EXISTING compile error (`w.ld<i32>(...)` — Widget has
   no `ld` member) blocks the whole `guild` library build and thus all test targets.
   Unrelated to this chunk; flagged for the owning agent. My four .cpp + the two test
   files all pass `g++ -std=c++17 -fsyntax-only` against the project include set.
3. Object subsystem (object+492 draw-data, Object_AllocDrawData 0x5b107c,
   AttachStockTextures 0x5d1114) backing AttachStockObjectLods is deferred/hook-routed.
