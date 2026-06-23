# Wave-H1 hardening — render chunk C (model_io.cpp, modelio_recon.cpp)

Scope: every function carrying `gilde.exe 0xADDR` provenance in
`src/render/model_io.cpp` and `src/render/modelio_recon.cpp`, diffed line-for-line
against the binary via MCP (decompile + disasm + get_bytes/disasm of constants).

## Summary counts
- VERIFIED-1:1: 11 functions
- FIXED: 2 functions (1 sign-flip bug in normals; 1 float-precision compare in bounds)
- BOUNDARY: 1 documented hook-arg nuance (freeBlock)
- Non-provenance helper (out of 1:1 scope): 2 (LoadSyntheticModel, Model::View)

All owned files compile; `modelio_recon_test` 78/78, `render_model_io_test` 13/13,
`render_mesh_e2e_test` 16/16 pass.

---

## FIXED

### VIBE_Model_ComputeNormals @0x5f8eb0  — face-normal SIGN FLIP
Binary call (0x5f8ee8): `TriangleNormal(v8, v7, /*out*/v4, v6)` where
v8=verts+24*face[+24]=v0, v7=face[+28]=v1, v6=face[+32]=v2.
VIBE_Math_TriangleNormal @0x5cb824 (verified by disasm of its cross product)
computes `out = (a2-a1) x (a4-a1)`, so the binary's face normal is
`(v7-v8) x (v6-v8) = (v1-v0) x (v2-v0)`.

The reimpl `guild::util::TriangleNormal(a,b,c,out)` computes `out = (b-a) x (c-a)`
(confirmed from src/util/math.cpp:141), so the faithful call is
`TriangleNormal(v8, v7, v6, out)` (a=v8,b=v7,c=v6).

- BEFORE: `TriangleNormal(v8, v6, v7, outNormal)` → out = (v6-v8)x(v7-v8) =
  NEGATED face normal. For the golden triangle v0=(0,0,0),v1=(1,0,0),v2=(0,1,0)
  this produced nz = -1.
- AFTER: `TriangleNormal(v8, v7, v6, outNormal)` → nz = +1, matching the binary.

Evidence: decompile 0x5f8eb0 (call-site arg order) + decompile 0x5cb824
(out = (a2-a1)x(a4-a1)) + src/util/math.cpp:141 (reimpl out = (b-a)x(c-a)).

Golden fixed too: `tests/unit/modelio_recon_test.cpp` — the normals test only
checked `fabs(nz)` (sign-agnostic, so it never caught the flip). Replaced with a
hard `nz == +1` assertion citing the addresses, so the sign is now pinned to the
binary. (vertex pass-2 accumulation/normalize was already 1:1.)

### VIBE_Model_ComputeBounds @0x5f8f98  — radius compare truncated to float
Disasm 0x5f8fcf-0x5f8fd1: `fsqrt; fcomp [esp+..var_1C]`. The 80-bit fsqrt result
is compared directly against the float radius (var_1C) — it is NOT rounded to
float before the compare. The store on update (var_1C) does truncate to float.

- BEFORE: `float r = (float)sqrt(...); if (r > radius)` — truncated the sqrt to
  float before the compare (can flip the branch in boundary cases).
- AFTER: `double r = sqrt(...); if (r > (double)radius)` — compares the double
  sqrt against the float radius, matching `fcomp`. Store still `(float)sqrt(...)`.

Init constants verified by disasm/get_bytes: 1e10 = 0x501502F9 (`1.0e10f`),
-1e10 = 0xD01502F9 (`-1.0e10f`) — literals already correct. The 8 box-corner emit
order (n+0..n+7) and the even/odd 1e10/-1e10 init loop verified 1:1 against
0x5f9003-0x5f9421. Bounds golden test (RadiusAndCornerVerts) unaffected, still passes.

---

## VERIFIED-1:1

### VIBE_Model_ComputeChunkSize @0x5f9430
v4 init (24, or 24*(v3+8)+24 when rec[17]>0); the <=254 → 49*faceCnt vs
>254 → 52*faceCnt branch; v6 = 6*matCount+v5+v4; material loop strlen(+64,+128,+0)
each +1, stride 224, bounded by rec[120]; sub loop 24*subCount base + strlen(v12)+1,
stride 88, bound reloaded `aux[+52]` each iter. All match decompile 0x5f9430.

### Material Bio thunks @0x5e3fac / 0x5e3fd4 / 0x5e3ffc / 0x5e4024 / 0x5e404c / 0x5e4074 / 0x5e409c
All: `Bio(stream, 224*ctx[+8] + ctx[+16] + off)`. Verified offsets 200/201 (ReadByte)
and 204/208/212/216/220 (ReadDword), stride 224, idx@+8, table@+16 — match
decompile of 0x5e3fac/0x5e3fd4/0x5e3ffc/0x5e409c (rest follow the identical pattern).
ReadByte vs ReadDword split correct.

### VIBE_Resource_FindFreeSlot @0x40df94
Scan stride 740, "in use" = dword at slot+4, bound 378140 (==740*511), return idx
or -1. 1:1. The original reads global `dword_69FFB4` as the table base; the reimpl
takes the base as a parameter (documented in modelio_recon.h) — the only diff, a
deliberate non-aliasing shim, logic identical.

### VIBE_Resource_EvictOldestEntry @0x40decc
Scan from index 1; eligibility `floor > e[+72] && e[+64]<=0 && (e[+68]&1)==0 &&
e[+52] && e[+56]`; on match v1=idx, running-min=e[+72]; if none, return 0; else
FreeDebug(e[+52]), usedMem(dword_62D20C) -= e[+56], e[+52]=0, return 1. 1:1 vs
decompile 0x40decc.

### VIBE_Resource_FreeEntryData @0x5d91d4 (verified against DISASM)
Hex-Rays collapsed the ecx==entry aliasing; verified against the disasm:
- +0x0C dword != 0 guard → return -1 (0x5d91e3).
- flags2 = byte +0x0D; bit 0x10 → FlushBuffer (0x5d91f8).
- off_64A910(entry[+10h]) (default NullStub) (0x5d9209).
- FreeBlock; if != -1 → FlushAndSeek(entry[+10h], 0) (0x5d9216-0x5d9224).
- alsoClose → CloseHandle(entry[+10h]); `or esi,eax` (0x5d9229-0x5d9235).
- flags1 = byte +0x0C bit 0x08 → ReturnToFreeList(blockHdr[+8]); blockHdr[+8]=0
  (0x5d9237-0x5d924b). blockHdr = entry[+8].
- flags2 bit 0x08 → BuildTempFileName(buf, blockHdr[+14h]=tempId);
  CloseHandleThunk(buf) (0x5d9252-0x5d926d).
- off_64A914(entry[+10h]) (default NullStub) (0x5d9275).
- alsoClose → off_64A91C(entry[+10h]) (default ExitHandlerThunk) (0x5d9282).
- return esi. All bit masks, branch conditions and the OR accumulation match 1:1.

---

## BOUNDARY (documented, not changed)

### VIBE_Resource_FreeEntryData @0x5d9211 — `freeBlock` argument
Disasm: `mov eax, ecx; call VIBE_Memory_FreeBlock` — FreeBlock is passed the ENTRY
pointer (ecx), not the file handle. The reimpl hook is `int freeBlock(int fh)` and
the code passes `ent.fileHandle`. Because the reimpl's ResourceEntryView is a typed
field-view and does not expose the raw 32-bit entry pointer (rule-4 platform
boundary: FreeBlock is the memory allocator), the entry pointer cannot be handed to
the hook as the binary does. The teardown control flow is 1:1; only the opaque
allocator arg differs. Left as a boundary; the hook semantics are test-driven, not
1:1-pointer-faithful. No source change. (Handoff candidate if a future ResourceEntry
gains a raw-pointer field; no file outside this chunk needs touching today.)

---

## Out of 1:1 scope (no provenance address)
- `model_io.cpp` `LoadSyntheticModel` / `Model::View` — explicitly a SYNTHETIC
  helper format (header documents the real loaders VIBE_Mesh_LoadBgfFile @0x5d2348
  and VIBE_Model_FastChunkIo @0x5f9558 as DEFERRED). No `gilde.exe 0xADDR`
  provenance → nothing in the binary to diff against. Reads operate on an in-memory
  buffer only (no file I/O), so no IFileSystem boundary issue. Left unchanged.

## Handoffs
None requiring edits outside this chunk. (FreeBlock arg noted above is a
boundary, not a fix.)
