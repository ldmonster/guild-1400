# Hardening pass — render_recon4_ddraw + render_recon_objlist

Module owner: render_05 hardening sub-agent. IDA MCP (gilde.exe, base 0x400000)
used as reference of record. Files touched (only these + their headers/tests):
- src/render/render_recon4_ddraw.cpp
- src/render/render_recon_objlist.cpp
- tests/unit/render_recon4_ddraw_test.cpp
- tests/unit/render_recon_objlist_test.cpp

## render_recon4_ddraw.cpp

### ConfigureSurfaceCaps — 0x432260 — FIXED
- Bug: the reconstruction hardcoded `v52 = 0`, with a comment claiming the
  original reads `mode[781]` AFTER the memset (so v52 is always 0).
- Evidence (disasm): at `0x432269 mov al,[edx+30Dh]` (mode+781) is read BEFORE
  the memset (`rep stosb` at `0x432294`, base edi = edx+0x30C = mode+780).
  `shl al,5; shr al,7` -> `v52 = (u8)(32*mode[781])>>7` = bit2 of the PRE-memset
  mode[781]. Then `0x4322bf mov [edx+30Dh], bl` writes `4*(v52&1) | (mode[781]&0xFB)`.
- Before: `u8 v52 = 0;` then `std::memset(mode+780,0,44);`
- After:  `u8 v52 = (u8)((u8)(32*mode[781])>>7);` computed BEFORE the memset.
- Golden: new test `ConfigureSurfaceCapsV52PreMemsetBit` — presetting mode[781]
  bit2 (0x04) round-trips to mode[781] bit2 set; clearing it stays clear.

### EnumTextureFormatsCallback — 0x5dce20 — FIXED (abs width)
- The two "closest depth" comparisons used 64-bit `std::llabs((i64)...)`.
- Evidence (disasm): both abs sites are 32-bit `cdq; xor eax,edx; sub eax,edx`
  (`0x5dcfa6`/`0x5dcfb2` and `0x5dd07d`/`0x5dd089`), compared with signed `jl`.
  Hex-Rays mis-modeled the second as a 64-bit HIDWORD abs; disasm rules.
- Fix: added static `abs32(i32)` doing `t=x>>31; (x^t)-t` (matches the cdq
  sequence, well-defined for INT_MIN), and replaced both `std::llabs` compares.
  Behaviorally identical for all bit-depth inputs; now byte-faithful to the asm.
- Otherwise VERIFIED-1:1 (opaque-fourcc capture, flag gate, mask bit counts over
  32 iterations, alpha-slot select, byte_762718/76271F/762720 vs v35/v34/v36,
  byte_14080E4 == matchedBackbuffer gate, all qmemcpy(32) copies).
- Note: struct field `SurfaceChannelBits::alphaBits` maps to byte_762718 which is
  compared against v35 (RED-mask bit count). The NAME is cosmetically misleading
  but the 1:1 mapping (first field <-> byte_762718 <-> v35) is correct; left as-is
  to avoid churn outside the verified behavior.

### EnumModeCallback — 0x4336ac — VERIFIED-1:1
- caps@+76 bit0x40 gate, depth@+84 compare, width@+12/height@+8 vs 800x600 /
  1024x768 / 1152x864, byte_62D599/59A/59B sets, returns 1. Exact match.

### EnumZBufferFormatsCallback — 0x5dd534 — VERIFIED-1:1
- dword[1]==1024 gate, dword[3] z-depth, acc[5] best / acc[0] wanted, the
  `v3<a0 || v3-a0 > v4-a0 -> reject` filter, 32-byte copy into acc+8, returns 1.

### BOUNDARY (rule 3/6, unchanged)
- All pure DDraw/D3D vtable wrappers listed in the .cpp header banner
  (LockSurfaceRegion, CreateSurface, ReleaseSurface, BlitSurface, EndScene,
  CreateDeviceAndViewport, EnumDevices, InitDisplayMode, ClearRect, DrawLinesD3D,
  ApplyGfxSettings, etc.) — GPU API calls with no separable reconstructable math.

## render_recon_objlist.cpp

### AdjustTextureBudget — 0x5b379c — FIXED (return value)
- Bug: on the post-query early-out paths (total>=limitA & free>=0x20000, or
  arg2>=limitB) the reconstruction returned `ret = (u8)(tick-lastTick)`.
- Evidence (disasm): `0x5b37e1 call QueryAvailableVidMem` sets `LOBYTE(a1)` to
  the callee's al-return; the early-outs at `0x5b37d3` (jnb from `0x5b3881`,
  free>=0x20000) and `0x5b3893` (arg2>=limitB) return that `al`, NOT the prior
  `tick-lastTick`. Call form is `QueryAvailableVidMem(0,&free,0)`; in
  0x435ba8 that success path does `LOBYTE(a1)=v15; *a2=v15`, so al == low byte
  of `free`.
- Fix: after the query, `ret = (u8)freeMem;` so the early-out paths propagate the
  free value's low byte (matching the binary). Pre-query early-outs (inactive
  gate, `tick-lastTick < 0x40`) unchanged.
- Golden: `AdjustBudgetAmpleFreeMemNoEvict` now asserts `r == 0x00` for
  free==0x20000 (low byte). Control flow / pressure halving paths re-verified 1:1
  (limitA/limitB >>=1, missesA/missesB counters, elapsed>0x100, lastTick=tick).
- QueryAvailableVidMem's ecx (the "total" compared to limitA) is a memset-leftover
  register in the inlined original; the hook models it as a clean `total` return.
  This boundary modeling was already documented in the header and is unchanged.

### FreeObjectNode — 0x5e0f30 — VERIFIED-1:1
- owner-compare node[0x2F0]==off_649D64; splice prev=node[0x308], next=node[0x30C]
  with prev->next(+0x30C)=next, next->prev(+0x308)=prev; head/tail fixups
  owner[0xA4]/[0xA8] = node->prev when node is head/tail (confirmed disasm loads
  v6[194]/a2[194] = the prev word). texEntry@0xD4 release (only if non-zero);
  FreeDebug order: free node[0x28]; zero; free node[0xDC]; zero; free node[0xE0];
  zero; free node itself (`mov eax,edx` at 0x5e0fd4). Exact.

### InitObjectList — 0x5e0e00 — VERIFIED-1:1
- All 12 transform words + 3 bookkeeping words + listHead sentinel, returns
  1065353216 (1.0f). 1.0f patterns at block[2],[3],[5],[8],[10],[11]. Exact.

### FreeObjectList — 0x5e0e74 — VERIFIED-1:1
- `while (dword_1408438 != &unk_1408440) FreeObjectNode(...)`; modeled through the
  caller-supplied head()/advance()/sentinel boundary (head==sentinel empty test).

### FormatCardInfo — 0x5dcd50 — VERIFIED-1:1
- flags@+780 bit shifts: translucent=f&1, fake=(f<<6)>>7, perspective=(32*f)>>7,
  linear=(8*f)>>7, can_clip=(16*f)>>7 (all u8). dims from a1+784/+788/+792/+796
  (CardCaps minTexW/H/maxTexW/H). Format string matches 0x629770 verbatim. The
  sprintf is the CRT boundary hook (default std::sprintf).

### SetGammaTable — 0x5b9ef4 — VERIFIED-1:1
- `(BYTE)gamma != byte_64A02C` gate; TextureCache_Reset; byte_64A02C=gamma (dl=al);
  primary reload dword_64A028 first; loop over dword_13ECF74 (stride 246 ints /
  984 bytes, 64 entries, end 0xF600) skipping 0 and ==primary, `result=` each.
  Table walk routed through forEachFloorRecord hook (boundary; iteration order /
  stride owned by the hook). Logic/order match.

### BOUNDARY (rule 3/6, unchanged)
- QueryAvailableVidMem (DDraw GetAvailableVidMem), EvictManagedTextures vtable,
  Memory_FreeDebug, Texture_ReleaseEntry, UnlinkObjectNode, sprintf, the floor
  table walk — all routed through inert hooks; only the surrounding logic is recon.

## Tests
- render_recon4_ddraw_test:  89 checks, 0 failures (added ConfigureSurfaceCapsV52PreMemsetBit)
- render_recon_objlist_test: 1375 checks, 0 failures (added AdjustBudget post-query return assertion)
- ctest: 2/2 passed.

## Summary
3 behavioral fixes, all disasm-evidenced:
1. ConfigureSurfaceCaps v52 read order (pre-memset mode[781]) @0x432269.
2. EnumTextureFormatsCallback abs width (32-bit cdq, not 64-bit llabs) @0x5dcfa6/0x5dd07d.
3. AdjustTextureBudget post-query early-out return value (free low byte, not tick delta) @0x5b37e1.
All other listed functions VERIFIED-1:1; GPU/DDraw/CRT calls remain BOUNDARY hooks per rules 3/6.
