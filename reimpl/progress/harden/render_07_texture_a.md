# Harden wave — render texture parsing (07_texture_a)

Files audited (line-for-line vs IDA decompile + disasm, constants/tables via get_bytes):
- src/render/texture.cpp
- src/render/texture_asset.cpp
- src/render/texture_bin.cpp
- src/render/texture_cache.cpp

MCP: gilde.exe @0x400000, LIVE. CRC table + globals read with get_bytes/get_global_value.

## Per-function verdicts

### texture.cpp
- **TextureSetSize / CreateRecord — 0x5db724 VIBE_Texture_CreateRecord** — VERIFIED-1:1 (host-collapsed model).
  - texelMask `*((_DWORD*)v21-7) = (v20-1)|(v20*v20-1)` (v20 = width @+116) → `TexelMask(w)=(w-1)|(w*w-1)`. MATCH.
  - width stored at both +116 (`*((_DWORD*)v10+29)`) and +120 (`*((_DWORD*)v10+30)`). MATCH (mipWidth/baseWidth).
  - refCount `*(v11+64)=1`, paletteId `*(v11+80)=-1`, texel buffer `*(v11+68)=AllocDebug(w*w)`. MATCH.
  - name `VIBE_Util_StrNCopyPad(rec, name, 63)` → `name.substr(0,63)`. MATCH.
  - The original additionally packs flag bits into +104 (bits 0/1/2/3 from a5/a6/a4/a9) and the cache/HiColTab branch on `byte_649D70`. These are deliberate host stand-ins (the bind/flag state + HiColTab requantize is the unreconstructed @0x5da34c arm, documented in texture.h). Geometry math (the load-bearing part for this wave) is exact.
- **FindFree / FindActive** — host slot-scan model of the FindActiveRecord back-scan (0x5da244). VERIFIED (model, not byte-identical — original walks `dword_1406A84 + i*128` records; reconstruction uses a vector). Geometry/refCount semantics preserved.
- **IncrementRef — 0x5da2e4** — VERIFIED-1:1. Branch `a2 && *(+112)>0 && !*(+113)` → group bump over records sharing paletteId(+80); else `++*(+64)`. Reconstruction's `mipLevels>0 && !isMip` maps +112/+113. MATCH.
- **ReleaseEntry — 0x5d9a0c** — VERIFIED (host-collapsed). Original decrements `v1[16]` (+64 refCount), on `<=0` clears slot (`v1[16]=0`, `--dword_1406A74`) — reconstruction's `--refCount; if<=0 clear`. The surface-free + group-recursion (+113 mips, ReleaseSurfaces) are foreign subsystems; refCount transition is exact.
- WhiteDefaultTexture / WhiteDefaultPalette / Rgb24 + ColourKey switches — BOUNDARY / host stand-ins (documented null-bind of 0x5db564/0x5db5f0; no binary table backing). Not byte-checkable; left as-is.

### texture_asset.cpp
- **TextureReadSquareInfo** (square check, 0x5F0C10 header) — VERIFIED. `width!=height || width<=0 → reject` matches LoadByName's `v77==v78` test. Header parse delegates to bmp.cpp (BmpReadHeaderInfo, owned elsewhere).
- **DecodeBmpIntoTexture / LoadByName — 0x5DB724 + 0x5DA714 + 0x5da34c** — VERIFIED (software model). Geometry via TextureSetSize, 8-bit decode + 24-bit palettize arm wired to PalettizeDecodedBmp (the @0x6029f0 quantizer). Index buffer copied verbatim into +68; palette pointer at +72. FindActive hit → IncrementRef early-return matches LoadByName. No divergence in offsets/stride.

### texture_bin.cpp
- **DecodeBmpBuffer — 0x5F0C10 / 0x5F0CE4** — VERIFIED. 8-bit: palette read as BGRA→RGB, RGBA expand by index. 24-bit: BmpLoadBuffer(24) top-down → RGBA. Palette/offset handling (0x36 base, 1024B BGRA) lives in bmp.cpp; this front-end's index→RGBA math is correct. Malformed/truncated → ok=false (tests cover empty/short/non-BM/truncated).
- **Mount / buildIndex / ResolveName / Decode / DecodeBuffer** — host VFS/name-index layer (case-insensitive .BMP stem index). No binary table; BOUNDARY-adjacent. Name-resolution semantics match the material-name → "*"+name+".BMP" wildcard intent. VERIFIED (model).

### texture_cache.cpp  — the binary-parsing heart of this wave
- **BuildSignature — sampling from 0x5ba0b0 / 0x5ba1e8** — VERIFIED-1:1 for the faithful input domain.
  - Original sig layout: `v24[5*row + col] = src[width*(mask&(vCoord+row)) + (mask&(uCoord+col))]`, mask=`width-1`. Row stride is **5 bytes** (`v28 += 5`), block side = `size+1` (the buffer is 25 bytes ⇒ the engine only ever uses size=4 ⇒ 5×5). For n=5 the reconstruction's contiguous `k++` packing is byte-identical to the 5-stride packing (5 cols/row). Golden test (sig[0]=0, sig[5]=8, sig[24]=36, crc=1273329001) confirms. Mask & wrap MATCH. (The reconstruction exposes a free `n` for host generality; only n=5 is engine-faithful, and that is what every caller passes.)
- **TileCache::Reset — 0x5b9f54** — VERIFIED-1:1. Per-slot clear: data(+0)=0, +64 byte=0, stamp(+4)=0, size(+8)=0, key(+12)=0, memset(slot+36,0,25). The '*' group-record release walk and Floor_InvalidateTiles sweep are foreign subsystems surfaced via callbacks. frame/salt untouched (matches: no write to dword_649D58/dword_649D60). MATCH.
- **FindLruSlot — 0x5ba03c** — VERIFIED (host-collapsed, behavior-identical via sole caller).
  - Slot stride confirmed: `result += 17` ints = 68 bytes; stamp at `result[1]` (+4). MATCH.
  - Free slot (`*result==0`): stamp=frame, return. MATCH.
  - LRU track: `if (v1 > result[1])` → `bestStamp > stamp`, init `v1=dword_649D58=frame`. MATCH.
  - NOTE (no observable divergence): on eviction the ORIGINAL does NOT zero slot data(+0); it zeros the evicted *tile record's* +88 back-pointer (`*(v5+88)=0`, v5=*v4). The reconstruction collapses the separate tile record into `data` and zeros `data` instead. The sole caller (GetOrBuildTile) immediately overwrites `data`, so the collapse is behavior-identical through the public API. Left as-is (documented in source).
- **LookupTile — 0x5ba0b0** — VERIFIED-1:1.
  - key = `VIBE_Util_Crc32(sig,25) + dword_649D60` → `CrcCompute(0,sig,25)+salt`. CRC equivalence proven: 0x5dc6e0 is init=~0, table dword_5DC2E0, `(v4>>8)&0xFFFFFF ^ table[low]`, final ~ — standard reflected CRC-32. Table bytes confirmed: [0]=0x00000000,[1]=0x77073096,[2]=0xEE0E612C,[3]=0x990951BA (poly 0xEDB88320). MATCH.
  - match: `*v20!=0 && a5==v20[2] && key==v20[3]` (size@+8, key@+12) → `data!=0 && size==s.size && key==s.key`. MATCH.
  - on hit `*(result+4)=dword_649D58` → `s.stamp=frame`. MATCH. Returns index. MATCH.
- **GetOrBuildTile — 0x5ba1e8** — VERIFIED (host-collapsed). Lookup-hit returns; miss → FindLruSlot, fills key(+12)/size(+8)/origin(+16,+20), marks data occupied. The original additionally builds the downsample tile (CreateTileRecord, dword_5B8CC0 "x4" string table, width clamp dword_64A038<<byte_64A045) — that tile *construction* is the floorgfx tile-build path (TileCacheSampleSubBlock/TileCacheWidthClamp live in floorgfx_recon.cpp, separate owner). The cache slot-claim semantics this file models are exact.

## Constants / tables confirmed via MCP
- CRC-32 table dword_5DC2E0[0..3] = canonical poly-0xEDB88320 entries. (CRC = compress::CrcCompute.)
- Globals: dword_649D58 (frame)=0, dword_649D60 (salt)=0, dword_64A034 (slot count)=0, byte_64A044/45=0 at init.
- Slot stride 68 bytes / 17 dwords; stamp+4, size+8, key+12. Sig scratch slot+36, 25 bytes.
- Record geometry: width +116/+120, texelMask +88 = `(w-1)|(w*w-1)`, texels +68, refCount +64, paletteId +80, palette/HiColTab +72.
- Signature block: 5-byte row stride, 5×5 (size=4), mask = width-1.

## FIXED
- None. All four files are correct 1:1 / faithful host-collapsed reconstructions. No constant, offset, stride, mask, CRC, or branch divergence found. No churn applied.

## Tests (all green)
render_texture_test, texture_bin_test, render_texture_palettize_test,
render_texture_set_table_test, render_texture_upload_test, render_tile_textures_test,
real_texture_driver_test — 7/7 PASS.

Counts: 4 files; ~16 provenance-tagged functions audited;
VERIFIED-1:1 (exact): 6 (TexelMask geometry, IncrementRef, BuildSignature, Reset, LookupTile, CRC) ;
VERIFIED (faithful host-collapsed model, behavior-identical): 8 ;
BOUNDARY / host stand-in (no binary table): WhiteDefault*, Rgb24/ColourKey switches, TextureBin name-index ;
FIXED: 0.
