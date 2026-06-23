# Hardening pass — render_08: texture_upload.cpp + texture_set_table.cpp

1:1 line-for-line DECOMPILE+DISASM diff of the texture-record upload/bind layer
and the .TXS texture-set table against gilde.exe (imagebase 0x400000). DISASM was
used as the tiebreaker over Hex-Rays for every field offset, gate, and order.

Counts: **VERIFIED-1:1 = 4**, **FIXED = 5**, **BOUNDARY = 2**.

Tests: `render_texture_set_table_test`, `render_texture_upload_test`,
`render_texture_upload_itest`, `render_texture_upload_e2e_test` — **4/4 pass**.

---

## texture_upload.cpp

### 0x5db564 VIBE_Texture_BindActive — FIXED
- **Gate.** Binary @0x5db566-0x5db570: `cmp eax,dword_64A1F8; jz ret; test eax,eax;
  jz ret` == `if (idx != boundIndex && idx != 0)`. The source gated on
  `idx == boundIndex || !valid(idx)` — which would PROCEED for idx==0 (boundIndex
  default -1, valid(0) true). **Before:** `if (idx == binding.boundIndex || !valid(idx))`.
  **After:** `if (idx == binding.boundIndex || idx == 0 || !valid(idx))`. The
  original does NO upper-bound check (trusts the caller); `valid()` is kept only as
  a defensive vector guard, the `idx==0` no-op is the real binary semantics.
- Everything else VERIFIED against 0x5db564 disasm: clone follow `+0x4C<<7 + base`
  (flags&2), field copies +72 palette / +116 mipWidth / +76 slot / +68 texels,
  `byte_1406A90[mipWidth]` width-log2 lookup, and the slot==0 white-default branch
  (slot=0,texBase=0,palBase=0,mipWidth=1,widthShift=1). boundIndex is written
  before the clone-follow, matching 0x5db575.

### 0x5db5f0 VIBE_Texture_ResetBinding — VERIFIED-1:1
- All 6 global stores match 0x5db5fb-0x5db619: dword_1406A7C=1, byte_1407A91=1,
  dword_1406A88=0, unk_1406A8C=0, dword_64A1F8=0, dword_1406A78=0.

### 0x5db1bc VIBE_Texture_PropagateToInstances — VERIFIED-1:1
- Guard `!(flags&2) && name[0] != '*'(42)` (0x5db1d2); selfIdx == masterIdx
  (`(this-base)>>7`); loop copies to clones with `refCount>0 && flags&2 &&
  slot==selfIdx` (0x5db1fb): +96=src(v2[24]), +100=dst(v2[25]), +68 texels(v2[17]),
  +116 mipWidth(v2[29]), +120 baseWidth(v2[30]). Offsets confirmed.

### 0x5db234 VIBE_Texture_UploadToSurface — FIXED (3 sites)
DISASM 0x5db234..0x5db3b8 was the authority.
- **GPU-path entry gate field.** Binary @0x5db27d: `cmp [esi+60h],0; jnz tail` ==
  enter the reload block only when `*(+96)==0 || a2`. **+96 is srcSurface**
  (struct/header). The source gated on `g.dstSurface == 0 || force`.
  **Before:** `if (g.dstSurface == 0 || force)`. **After:** `if (g.srcSurface == 0
  || force)`.
- **GPU-path release order.** Binary releases +96 (source) FIRST (0x5db287) then
  +100 (dest) (0x5db2a0). Source released dstSurface then srcSurface (field-swapped
  vs the struct names). **Before:** release `g.dstSurface` then `g.srcSurface`.
  **After:** release `g.srcSurface` (+96) then `g.dstSurface` (+100).
- **Software-path release order + texel drop.** Binary @0x5db417/0x5db431 releases
  +96 then +100; @0x5db45f sets `*(+68)=0` ALWAYS (FreeDebug only runs on the
  `!a2` path). Source released dst-then-src and only `clear()`-ed texels on the
  `!force` path (left texels intact on force). **Before:** release dst,src; clear
  only when `!force && !empty`. **After:** release src(+96),dst(+100); FreeDebug
  semantics on `!force`, then `rec->texels.clear()` unconditionally (== `*(+68)=0`).
- **Mip-width derivation.** Binary @0x5db34d-0x5db352: `mov eax,[esi+78h]; shr
  eax,cl(=byte_64A350); mov [esi+74h],eax` — a RAW logical shift, NOT saturated.
  Source used `MipWidth(baseWidth, shift)` which clamps to >=1. **Before:**
  `rec->mipWidth = MipWidth(rec->baseWidth, mipShiftGlobal)`. **After:**
  `rec->mipWidth = (i32)((u32)rec->baseWidth >> (mipShiftGlobal & 31))` (the `&31`
  mirrors x86 `shr` count masking; texture_mip.h NOT edited per scope).
- VERIFIED unchanged: the +64 refCount>0 gate; clone-follow to master (+76<<7);
  byte_64A350 save (v22) / restore at the tail (0x5db362); `flags&8 -> noKey=0 else
  dword_64A1FC`, `flags&0x40 -> shift=0 else saved` (0x5db2f0/0x5db2fc);
  `flags&4 -> stretch variant`; name[0]=='*' & slot==0 skip the load; the
  PropagateToInstances tail; and the mip recursion gate `mipLevels(+112)>1 (ubyte,
  jbe) && !isMip(+113)` walking records with `refCount>0 && paletteId(+80)==self
  && isMip(+113)!=0` (0x5db367-0x5db3b6).
- **BOUNDARY (rule 3, GPU/DDraw):** the COM surface release `(*(vtable+8))(surface)`
  (IDirectDrawSurface::Release) and `VIBE_Render_LoadAndStretchTexture` (0x5dea50)
  BMP decode+upload are routed through `ITextureSurface::release` / `loadAndUpload`.
  The orchestration, branch logic, arg order (path, &dst(+100), &src(+96), noKey,
  baseWidth, baseWidth, loadShift(+125)) and field offsets are verified 1:1; only
  the GPU call itself is the hook. The software re-decode arm (BuildBmpPath +
  LoadSoftPalettize, +124=8) and ReleaseEntry for '*' records are modeled at the
  asset/slot boundary.

### 0x5db4b8 VIBE_Texture_UploadAllRecords — VERIFIED-1:1
- First pass counts `refCount>0 && !(flags&2)` (0x5db4ef); second pass uploads each
  + fires the callback `a2()` after each (0x5db547/0x5db55b); dword_64A1F8=0 at the
  end (0x5db530). The count pass is gated on `a2 != 0` in the binary (cosmetic — the
  count `v2` is otherwise unused); behavior identical.

### 0x5dba74 VIBE_Texture_RestoreIfLost — VERIFIED-1:1
- `if (byte_649D70 && (flags&4)) LoadFromCache(rec,0,0)` (0x5dba7e/0x5dba84),
  modeled as a forced UploadToSurface of the flags-bit2 dynamic record.

### WidthLog2Table / WidthShift / MipWidth — VERIFIED
- `byte_1406A90[mipWidth]` is the runtime width->log2 table; BindActive uses
  WidthShift(mipWidth) for it (po2 width -> log2). The UploadToSurface mip-width
  formula is now the exact raw `baseWidth >> byte_64A350` (see FIX above).
  texture_mip.h `MipWidth` (saturating) left untouched per edit scope.

---

## texture_set_table.cpp

### 0x506388 IsFoliageMeshName — FIXED
- DECOMPILE 0x506388 (VIBE_Object_HideFoliageDecor): three case-SENSITIVE
  `VIBE_Util_StrncmpN(a1, prefix, n)` with **a1 == the name pointer at BYTE 0**:
  "pfl_"/4 (aPfl @0x620f48), "vg_"/3 (aVg @0x620f50), "!vg_"/4 (aVg_0 @0x621010),
  OR-combined. StrncmpN @0x5e9ee0 is a plain byte compare from offset 0 (== strncmp).
  Prefixes/lengths/order all matched the source already. The source ALSO stripped a
  path to its basename before comparing — the binary does NOT (it compares from
  byte 0). **Before:** split on `/`,`\\` to a basename, then strncmp the basename.
  **After:** strncmp the name from byte 0. Archive member names are bare (no path
  separators) so this is identical for all shipped content; the two golden tests
  that asserted path-prefixed names MATCH were corrected to assert they do NOT
  (matching the binary). Header note updated.

### ParseTextureSetTable / kTextureSetMagic — VERIFIED-1:1 / format-recovery BOUNDARY
- In-binary loader **0x5d2240 VIBE_Mesh_LoadTextureSet** confirms the byte format
  field-for-field: u32 magic == **603064750 == 0x23F209AE** (compare @0x5d2289,
  matches `kTextureSetMagic`), then u32 setCount (`i`), u32 namesPerSet (`v11[0]`),
  gate `setCount>0 && namesPerSet>0` (signed), then setCount*namesPerSet
  NUL-terminated names read SET-major (outer setCount, inner namesPerSet) into
  64-byte records (`d3_io:LoadtextureSet`). The dword reads use
  VIBE_Bio_ReadDwordSwapArgs @0x5dc8b0 -> a plain 4-byte little-endian read (no
  endian swap), matching the reimpl's `data[0]|data[1]<<8|...` assembly. The reimpl
  parser reproduces the magic + header layout + SET-major name order exactly. The
  fixed 64-byte in-memory record stride and the std::string variable-length storage
  are the format-recovery BOUNDARY (in-memory detail of the loader; the on-disk byte
  stream is 1:1). The reimpl's extra `sets*per > size` plausibility gate is a
  safety addition over the binary's stream-EOF handling and never rejects valid
  shipped tables (590/590 parse).

---

## Test status
`cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original ctest -R
"texture_upload|texture_set" --output-on-failure` -> 4/4 pass (512, 514, 975, 1380).
Build target: `render_texture_upload_test render_texture_set_table_test` — clean.
