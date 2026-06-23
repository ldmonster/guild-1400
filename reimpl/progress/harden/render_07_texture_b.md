# Harden wave — render_07: texture_loader.cpp + texture_mip.cpp

Module under review:
- `src/render/texture_loader.cpp`
- `src/render/texture_mip.cpp`

MCP (IDA Pro, gilde.exe, imagebase 0x400000) used to decompile + disasm every
provenance-tagged function and diff line-for-line against the reconstruction.

## Counts
- VERIFIED-1:1: 4
- FIXED: 1 (with 2 golden tests corrected)
- BOUNDARY / documented partial: 1
- Non-provenance helpers (out of 1:1 scope, noted): 3

Test targets run green:
`render_shadow_test`, `render_texture_upload_test`, `render_texture_test`,
`real_texture_driver_test`, `render_tile_textures_test` — all PASS.

---

## texture_mip.cpp

### VIBE_Render_ComputeChannelShifts @ 0x4358d8 — VERIFIED-1:1
`ChannelShiftOne` / `MipChannelShifts`.
- loop1 (i<32): `(mask&1)==0` -> ++trailing, mask>>=1  ✓
- loop2 (j<32): `(mask&1)==1` -> ++width, mask>>=1     ✓
- `*a3 = 8 - width` (down) ; `*a5 = trailing` (up)     ✓
Decompile out-params confirm down = 8-width, up = trailing-zero count. No drift.

### VIBE_Render_BuildChannelLUT @ 0x435a3c — VERIFIED-1:1
`*v5 = result >> v12[0] << v10[0]` where v12=down, v10=up.
Reconstruction `out[i] = (i>>down)<<up` for R/G/B at offsets 0/256/512. Loop
`for result in 0..0x100`. Exact match. Golden `ChannelLut565` correct.

### VIBE_TextureCache_BuildBlendLut @ 0x5b9230 — VERIFIED-1:1
16.16 weight recurrence:
- `v11 = 0x100 / a2` (unsigned), `v9 = 4*a2` stride.
- `v2=(v11*v14)<<8`, `v3=0xFFFF-v2`, `v4=v11*((0xFFFF-v2)>>8)`.
- per-col decrement `dec = ((int)((v11*v14)<<8) >> 8) * v11` — signed (sar) shift
  of the *original* v2, loop-invariant; reconstruction hoists it correctly.
- bytes written are `BYTE1(x) = (x>>8)&0xFF` for v3,v5,v2,v6.
- `v3-=v4`, `v2-=dec`, `v5+=v4`, `v6+=dec`.
Return = `v9` (=4*a2). Reconstruction's `for(v14<a2)` vs binary `do/while` differ
only for a2==0, which faults in the binary (`0x100/0` div-by-zero) — not a real
input. Hand-verified n=4 golden (`BlendLutGolden`) byte-for-byte: row0
255,0,0,0 / 192,63,0,0 / ... matches.

### MipBlockSize — head of VIBE_TextureCache_BuildMipmap @ 0x5b903c — FIXED
Binary: `0x5b905c xor edx,edx ; 0x5b905e div ecx` => **UNSIGNED** division
`dword_1404E6C = a2[3]/a2[4]`; clamp `cmp eax,10h/jnb` + `cmp esi,40h` is
unsigned to [16,64].
- BEFORE: `int b = height ? width/height : width;` then `(unsigned)b` compares —
  used **signed** division.
- AFTER: `u32 b = (u32)width / (u32)height;` (unsigned `div`), unsigned clamp.
  Documented that height==0 faults in the binary (callers pass height>0); the
  `?:` guard was a fabricated non-faulting path and was removed.
Goldens `BlockSizeAndWidth` (256/256->16, 512/16->32, 4096/16->64) still pass.

### MipWidth — tail of VIBE_Texture_UploadToSurface @ 0x5db234 — FIXED (+2 goldens)
Binary @ 0x5db347-0x5db352:
```
mov  cl, byte_64A350
mov  eax, [esi+78h]      ; *(v4+120)  base width
shr  eax, cl             ; UNSIGNED shift
mov  [esi+74h], eax      ; *(v4+116)  mip width
```
There is **NO saturation to 1** in the binary, and the shift is `shr` (unsigned),
not `sar`.
- BEFORE: `int w = baseWidth >> shift; return w < 1 ? 1 : w;` (signed shift +
  fabricated clamp).
- AFTER: `return (int)((u32)baseWidth >> shift);`
- Golden discipline (rule 8): two tests encoded the fabricated clamp and were
  corrected to the binary:
  - `render_texture_upload_test.cpp` `MipWidthSaturates` -> renamed `MipWidthShift`;
    `MipWidth(2,8)` expected 1 -> **0** (2>>8==0).
  - `render_shadow_test.cpp` `BlockSizeAndWidth`: `MipWidth(2,4)` expected 1 ->
    **0** (2>>4==0).
  Evidence cited in both: disasm of 0x5db350 bare `shr eax,cl`.
- Header comment updated (removed "Saturates to >= 1").

### Non-provenance helpers (NOT 1:1-tagged — out of diff scope, noted)
`MipLevelCount`, `DownsampleIndex2x`, `BuildIndexMipChain` carry no `gilde.exe
0xADDR` comment. The header states BuildMipmap's pixel loop / DDraw upload are
DEFERRED (vendor/surface-format coupled via off_64A03C and the runtime 16-bit
colour LUTs dword_1404260/660/A60). These software-index-mip helpers are
engine-plausible scaffolding, not translations of a specific binary function;
left unchanged. No float->int sites in them (pure index copy).

---

## texture_loader.cpp

### TextureLoadByName @ 0x5da714 — BOUNDARY / documented partial
The original VIBE_Texture_LoadByName is a ~400-line __usercall
(eax=name, edx, cx=flags, bl=flags) that manages the 128-byte texture-record
array (`dword_1406A84`/`dword_1406A80`), packs ~12 flag fields into bytes at
+104/105/106/114, dedups via VIBE_Util_StrCmp + flag compares, allocates via
VIBE_Texture_FindActiveRecord, and dispatches to VIBE_Texture_LoadAnimatedSet /
VIBE_Bmp_ReadHeaderInfo / VIBE_Texture_BuildBmpPath. That whole record subsystem
and the DDraw/animation coupling live outside this file.

The reconstruction here is a deliberate cache front-end shim (name->slot) with a
reduced signature; it is not a line-for-line translation of 0x5da714 and is
framed as such. No isolated 1:1 arithmetic site to harden inside it. Flagged as
BOUNDARY: faithful full translation requires the texture-record + animation-set +
BMP-decode subsystems (separate modules) and the DDraw surface boundary. No
float->int, shift, or table math in the shim to diverge.

---

## Float->int / shift audit summary
- No `ConvertX`/`fistp`/`(int)`-cast float->int sites exist in either file (all
  integer math).
- Shift signedness corrected: MipWidth now `shr` (unsigned); MipBlockSize now
  unsigned `div`. BuildBlendLut's `>>8` decrement is correctly signed (sar) as in
  the binary. BuildChannelLUT/ComputeChannelShifts use unsigned `>>` on u32 as in
  the binary.
