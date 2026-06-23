# Wave 5 — W5-TEX: colour-key flag (+104 bit 2) + soft-palettizer re-verify

> IDA Pro MCP LIVE (gilde.exe, imagebase 0x400000). Every verdict below is from
> a fresh decompile pulled this wave; addresses cited per claim. Owned files:
> `src/render/texture.{h,cpp}`, `src/render/texture_palettize.{h,cpp}` and their
> unit tests. No other `src/` file edited (bind-site handoffs at the end).

---

## 1. COLOUR-KEY FLAG — root cause confirmed and fixed

### The bug
The wave-3/4 raster routing used **record +104 bit 3 (0x08)** as the colour-key
trigger (the black-tree-backdrop fix). Bit 3 is the **"_NM" name flag** — a
mip-bias selector, NOT a colour key. The real transparency/colour-key flag is
**bit 2 (0x04)**.

### Bit-by-bit +104 verdicts (writer 0x5da714, consumer 0x5db234)

The fresh-load arm of `VIBE_Texture_LoadByName` @0x5da714 writes +104; the
relevant stores (around 0x5dac81–0x5dad88):

| bit | value | meaning | decisive evidence | verdict |
|-----|-------|---------|-------------------|---------|
| 0 | 0x01 | a4 & 1 call-arg low bit | `v91 = v88 & 1` (0x5da799) → `v55 & 0xFE \| v91` store (0x5dacaa) | CONFIRMED |
| 1 | 0x02 | alias/clone record | consumer 0x5db258 `if((*(a1+104)&2)) v4 = base + (*(a1+76)<<7)`; fresh load sets it (`v24 \|= 2`, 0x5daa01, non-alias path) | CONFIRMED |
| **2** | **0x04** | **TRANSPARENCY / COLOUR-KEY** | **writer 0x5dad15: `v60=dword_140809C; v61 = !v60 && v79>8;` then 0x5dad52 `*v59 = (4*v61) \| v62`** — set iff global gate off AND bmp bpp (v79) > 8 (24-bit). **consumer 0x5db319: `if((*(v4+104)&4))` picks &unk_14080C4 (transparent descriptor) else &unk_14080A0 (opaque)** for LoadAndStretchTexture @0x5dea50 | **CONFIRMED — this is the colour key** |
| **3** | **0x08** | **name contains "_NM"** | **writer 0x5dad5b: `v64 = loc_5CB930(rec+104, aNm("_NM"))` then 0x5dad75 `*v65 = (8*(v64!=0)) \| v66`.** consumer 0x5db2f4: `if((*(v4+104)&8)) v9=0; else v9=dword_64A1FC` — v9 is ONLY arg 5 (mip-bias) of LoadAndStretchTexture. NOT a colour key. | CONFIRMED — **this is the bit wave-3/4 used by mistake** |
| 5 | 0x20 | 8-bit indexed source | `v54 &= ~0x20u` then `(32*(v52&1))\|v54` with v52=v85 (0x5dac7b/0x5dac92) | CONFIRMED |
| 6 | 0x40 | "don't downscale" | writer 0x5dad82 `if((v83[110]&2)) v83[104] \|= 0x40` (else a name probe sets it, 0x5dae30); consumer 0x5db300 `if((*(v4+104)&0x40)) byte_64A350 = 0` | CONFIRMED |

The colour-key VALUE (per /tmp/guild_w5_evidence/colourkey.md, LoadAndStretch
@0x5dea50): both descriptor arms colour-key on **palette entry 0's RGB**
(`pal[0].R | pal[0].G<<8 | pal[0].B<<16`) — the standard index-0-transparent
convention. The masked span `FillSpanTexturedMasked` @0x5F721A (source index 0
skipped) is the faithful software equivalent.

### The fix (texture.{h,cpp})
* `texture.h` `Texture::flags` doc rewritten with the per-bit provenance above
  (was: bit2 "selects unk_14080C4 arm", bit3 "v9 arg, MEANING UNVERIFIED").
* New `enum TextureFlagBits` (kTexFlag*) — named +104 bit constants.
* New predicate **`bool render::TextureIsColourKeyed(const Texture& t)`** =
  `(t.flags & kTexFlagColourKey/*0x04*/) != 0`, full provenance comment.
* `texture.cpp` unchanged (predicate is a header inline; no logic in .cpp).

### Tests
`tests/unit/render_texture_test.cpp` → new TEST `RenderTexture.ColourKeyFlagBit2`
(13 checks): a bit-2 (24-bit) record is keyed; an 8-bit (bit-5) record is not;
a "_NM" (bit-3) record is NOT keyed (the wave-3/4 mistake); bit2|bit3 is keyed;
alias/no-downscale companion bits don't key; the bit constants equal the exact
+104 values. Suite green: **113 checks / 0 failures**.

### EXACT bind-site handoff (universe_render.cpp + city_view3d.cpp — NOT my files)

Replace the wave-4 stop-gap `const bool keyed = false;` at the textured-span
binding (universe_render.cpp ~line 84, city_view3d.cpp ~line 94) with:

```cpp
const bool keyed = render::TextureIsColourKeyed(*bt->tex);
```

Also update the surrounding comments: the colour-key trigger is +104 **bit 2**
(transparency / 24-bit source), NOT `flags & 8`. The `flags & 8` mention in the
universe_render.cpp banner (lines 48–54) and the city_view3d sibling are wrong —
bit 3 is the "_NM" mip-bias flag. (universe_render.cpp line 49 currently reads
`v9 = (rec->flags & 8) ? 0 : noTransparency` and calls it the colour-key arm;
that arm is the MIP-BIAS arm, not the keyed arm.)

NOTE for the flag PRODUCER: bit 2 is only set by the engine's fresh BMP load
(0x5dad52) for a >8bpp source with `dword_140809C` off. In the reimpl the
decode/resolve layer (play::RealTextureSource) must set `tex->flags |=
render::kTexFlagColourKey` on records whose source BMP is >8bpp (24-bit) when the
global gate is off — otherwise decoded records carry flags==0 and the new
predicate is behaviour-neutral (same as today). Whichever agent owns the
decode→record bridge should set bit 2 there; once set, the masked span fires for
24-bit-sourced transparency textures (the black-tree-backdrop fix's true path).
texture_upload.h's `loadAndUpload(..., bool stretch, ...)` already takes a
`stretch` flag fed from bit 2 — that wiring is correct; only the raster-side
`keyed` selector and its comments were wrong.

---

## 2. SOFT-PALETTIZER RE-VERIFY — line-for-line, all CONFIRMED

Decompiled the full chain fresh this wave; compared each against
`render/texture_palettize.cpp`. The reconstruction matches the binary
line-for-line. **No divergence found — no code change.**

### Call-tree entry (LoadSoftPalettize @0x5da34c)
Confirmed: gated on `dword_1406A64`; `VIBE_Bmp_LoadBuffer` @0x5f0ce4 (flags arm)
feeds a 24-bit source through the quantizer; then used-colour compaction
(`v20[v7+256]=1` mark, planar pack, renumber) + `VIBE_HiColTab_FindOrBuild`
@0x5da04c re-bases indices into the shared shading bank. The per-texel COLOURS
are decided entirely by the quantizer (reconstructed in texture_palettize); the
HiColTab stage only renumbers indices — matches the reimpl's documented posture
(HiColTabBank already reconstructed; the in-tree bind paths build per-texture 565
LUTs so the renumber has no observable effect).

### Per-function verdicts

| addr | function | check | verdict |
|------|----------|-------|---------|
| 0x6029f0 | QuantBuildPalette | tablesInit gate → AllocColorNodes → BuildHistogram(w*h) → HeapReduceColors(maxColors) → TreeCollectPalette(0,0) → MapImageToPalette → FreeColorNodes → CopyPaletteEntries; order + structure exact | **CONFIRMED** |
| 0x602aa4 | InitLookupTables | rTab=word_1409508 bits {2,5,8,11,14}; gTab=word_14092EE/F0 bits {1,4,7,10,13}; bTab=word_14090EE/F0 bits {0,3,6,9,12}; squares table `d*d` for d∈[-255,255] at `dword_1409E0C[d+255]`, `dword_140A210 = &squares[1]`. The reimpl's three formulas reproduce every term (`32*v2 == (i&0x80)<<5`, etc.) | **CONFIRMED** |
| 0x602be0 | AllocColorNodes | 6 levels {1,8,64,512,4096,0x8000} × 24-byte zeroed nodes; palCount=0 | **CONFIRMED** |
| 0x602d2c | BuildHistogram | 24-byte stride into `dword_1409504` (= levels[5] base − 12); `++(+12)` count; leaf heap entries `{level=5, idx}` 1-based at `dword_64ADB4+4*i`; `+16` = count; leaf sums = `count*(channel5 + 4)` (bucket centre); ancestors levels 4..0 accumulate `+16` and OR child-mask `+20`; final `for i=leafCount..1: HeapSiftDown(i)` | **CONFIRMED** |
| 0x602f3c | HeapSiftDown | min-heap by node `+16`; `half = leafCount>>1`; child `2*v1`, pick smaller of `2v1`/`2v1+1` when `2v1 < leafCount`; `key <= child → break` | **CONFIRMED** |
| 0x603068 | HeapReduceColors | loop while `i(maxColors) < leafCount`; top at heap[1]; if parent `+12`(count)!=0 → `heap[1]=heap[leafCount]; --leafCount`, else `heap[1]={pl,parentIdx}`; parent += child sums/count; clear child-mask bit `(idx&7)`; HeapSiftDown(1) | **CONFIRMED** |
| 0x603180 | TreeCollectPalette | children bit 7..0 first (gated on `+20` mask); then if `+12`(count): `palIdx = dword_140A214`; `palR=((c>>1)+sumR)/c` (byte_1409708), G (byte_1409808), B (byte_1409907[v12+1] = byte_1409908[palCount]); `++palCount` | **CONFIRMED** |
| 0x603a30 | FindClosestColor | query `(c&0xF8)+4`; sqdist over `dword_1409A08` leaves via `dword_140A210[4*d]`; order B+G+R; bestD init 200000 | **CONFIRMED** |
| 0x6033b4 | MapImageToPalette | both arms. Dither arm: value-clamp v45 (`v60=v45+256`: [.-256..-1]=0,[0..255]=id,[256..511]=255); error-clamp v46 (`v61=v46+256`: ±20 saturating, identity in [-20,20]); 0x8000 i16 cache init -1; two `3*(w+2)`-i16 row buffers, **v49(=bufA) pre-zeroed and is the first-row `cur`**; serpentine (v50 toggles), per-pixel `(acc+8)>>4` 16ths consumption, FS error spread 3/5/7/1-pattern via the retreating v24 / advancing v23, row turnaround `a1 += 3w+3` after odd rows. Undithered arm: per-occupied-cell `tbl[code]=FindClosest(de-interleaved 5-bit, NO +4)`, then `dst[i]=tbl[rTab+gTab+bTab]` | **CONFIRMED** |
| 0x602a70 | CopyPaletteEntries | planar `out[k]=palR`, `out[256+k]=palG`, `out[512+k]=palB` for k∈[0,256) | **CONFIRMED** |
| 0x602c8c | FreeColorNodes | free level arrays + heap | **CONFIRMED** (lifetime model) |

The persistent-palette state note (byte_1409708/808/908 retain prior load past
the colour count; CopyPaletteEntries copies all 256) is reproduced and was
re-checked against TreeCollectPalette (only writes up to palCount) and the
quantized image (indices never exceed palCount). CONFIRMED.

### Tests
`tests/unit/render_texture_palettize_test.cpp` already pins 6 tests / 162 checks
(bucket-centre goldens, collect-order, undithered arm, the cross-validated
serpentine-dither regression map, 1024→256 reduce, DecodedBmp wiring). All green
this wave (162/0). No new vectors needed — the reconstruction was already exact;
this wave re-derived every function from a fresh decompile and found zero
divergence, so the existing goldens stand.

---

## Files changed (W5-TEX)
* `src/render/texture.h` — +104 flag doc rewritten per fresh decompile; new
  `enum TextureFlagBits` + `TextureIsColourKeyed()` predicate (provenance).
* `tests/unit/render_texture_test.cpp` — new `RenderTexture.ColourKeyFlagBit2`
  (13 checks).
* `src/render/texture.cpp`, `src/render/texture_palettize.{h,cpp}` — UNCHANGED
  (palettizer verified CONFIRMED line-for-line; predicate is a header inline).

## Test results
* `render_texture_test`: **113 checks / 0 failures** (forced clean rebuild).
* `render_texture_palettize_test`: **162 checks / 0 failures**.
* Build: `render_texture_test`, `render_texture_palettize_test` compile clean
  (cmake configure rc=0, build rc=0).

## Named gaps (rule 8)
* **Flag PRODUCER for decoded records**: the reimpl decode/resolve layer does not
  yet set `kTexFlagColourKey` on >8bpp source records (bit 2 producer @0x5dad52).
  The predicate + masked routing are wired and unit-covered; the bit must be set
  at the decode→record bridge (owner: RealTextureSource / decode layer) for the
  black-tree backdrop to route through the masked span at runtime. Until then the
  predicate returns false (behaviour-neutral, == today).
