# shape-convert16 — the 16bpp shape converters (THE session-hud NAMED GAP, CLOSED)

Module: `src/render/shape_convert16.{h,cpp}` (namespace `guild::render`)

The lazy load-time conversion chain gilde.exe runs on every SHAPBANK before its
shapes can reach the depth-1-only blitters. This was the named gap of
`progress/session-hud.md` ("FINDING — the named gap"): all 172 gilde.gfx banks
ship as pixel-format 2 (24bpp RLE) and were a behaviorally proven no-op in
`VIBE_Shape_ShowFromBank` @0x5d861c until converted.

## Translated (addr → symbol)

| gilde.exe | original | reconstruction |
|---|---|---|
| 0x5d7c0c | `VIBE_Shape_ConvertRgbTo16` (`__usercall eax = f(shape@eax)`) | `ShapeConvertRgbTo16(st, shape)` |
| 0x5d7924 | `VIBE_Shape_Convert8To16` (`__usercall eax = f(shape@eax)`) | `ShapeConvert8To16(st, shape)` |
| 0x5d80a8 | `VIBE_ShapeBank_ConvertNew` (`__usercall eax = f(bank@eax, target@dl, keep@ebx)`) | `ShapeBankConvertNew(bank, target, keepSource)` |
| 0x5d8080 | `VIBE_Shape_ConvertToNew` | already 1:1 in `render_leaves9` (`Shape_ConvertToNew`); its two hook slots now bind THESE converters via `InstallShapeConvertersIntoLeaves9()` |

Cross-checked against the raw disassembly (not just the Hex-Rays text) for:
* the `rep movsd/movsb` copy lengths — shape header `0x32`, bank header region
  `0x845`, row staging `2*n`, row table `4*height`;
* the register-staged pixel byte order (`al = p[0] → r`, `dl = p[1] → g`,
  `bl = p[2] → b` into `VIBE_Result_Handler_Final` @0x434f30 == the existing
  `PackColor`);
* the unsigned `div` in the skip re-encode (`2*skipBytes / 3u`, truncating);
* the 8bpp palette LUT stride (`add ecx, 4` — `byte_1406530` is RGBX quads);
* the `xor edi, edx` j=0 idiom and the AddShape/Free temporaries order in
  0x5d80a8.

## Behavioral points preserved 1:1

* **RLE branch** (spanFlag @+38 != -1): per-run skip re-encoded `2*skip/3u`
  (3-byte → 2-byte pixel gap), a pixel that PACKS to 0 is replaced by
  `Pack(5,5,5)` (= 0x0020 in 565; 0 is the transparent slot), the half-bright
  flag (+13 == 1) then applies `(px>>1) & word_1406944` — which can faithfully
  re-zero a replaced pixel (0x0020 → 0x0010 → masked → 0). Header rebuilt:
  size@0, depth@12=1, runTotal@38, opaque@46; row-offset table appended,
  rowTableOffset@42 points at it.
* **RAW branch** (spanFlag == -1): full-bitmap pack, NO 0→(5,5,5) replacement
  (black stays transparent), spanFlag stays -1, rowTableOffset = 0, opaque = 0.
* **8bpp**: palette-quad LUT, skip×2 re-encode, no 0-replacement; the RAW 8bpp
  form is rejected with the original's exact message
  `"shp_Convert8To16: Converting of NoReadAndSkip Shapes not surported..."`
  (typos included) through the `reportMessage` hook (= ReportMessage @0x438da8).
* **Bank driver** 0x5d80a8: 0x845-byte header copy, cursor/count reset, format
  byte, per-shape ConvertToNew→AddShape(@0x5d8330)→free, trailing sequence-data
  re-append (+62 offset / +67×8 bytes), `if (!ebx) free(source)`; pass-through
  return for non-(2|0)/target-1 combos; null when writeCursor == 0. Alloc sizes
  exact: `cursor - opaqueSum` (fmt 2) / `cursor + opaqueSum` (fmt 0).
* **Global-state model** (`ShapeConvertState` + `ActiveShapeConvertState()`):
  `byte_762719..76271E` → `ColorFormat`, `word_1406944` → `darkMask`
  (`ShapeConvertDarkMask` reproduces the InitColorMasks @0x5d4ad4 formula:
  0x7BEF @565, 0x3DEF @555), `byte_1406530` → `palette1024` (zero default = the
  .bss image). Allocation via std::malloc where the original used
  `VIBE_Memory_AllocDebug("d2:shp:NewShape"/"d2:shp:NewBank")`.
* **Faithful stack bounds**: row staging WORD[1152], row table DWORD[864] —
  same out-of-contract envelope as the originals.

## Wired (rule 13)

* `render_leaves9` hook slots `ConvertRgbTo16` / `Convert8To16` →
  `InstallShapeConvertersIntoLeaves9()`; installed by
  `world::InstallRealWorldNetWiring()` (src/world/wire_worldnet.cpp — these two
  slots are no longer inert; docs + tests updated).
* `play::SetHudSpriteBankFromGfx(blob, size)` (src/play/wire_hud_bridge.*):
  converts a REAL gilde.gfx SHAPBANK through ShapeBankConvertNew and makes the
  depth-1 result the active HUD sprite bank — real artwork now flows to the
  REAL `ShapeShowFromBank` @0x5d861c with NO SessionHud edit.

## Tests (all passing)

* `tests/unit/shape_convert16_test.cpp` — **12 tests / 76 checks**:
  * `DarkMaskMatchesInitColorMasksFormula` — 565/555 masks + packer sanity
  * `RgbTo16RleGoldenBlob` — byte-exact 98-byte whole-blob compare
  * `RgbTo16ZeroPackBecomes555` — 0-pack → 0x0020
  * `RgbTo16HalfBrightAppliesAfterZeroReplacement` — (px>>1)&0x7BEF order
  * `RgbTo16SkipReencodeIsUnsignedTruncatingDiv` — 2*7/3 = 4
  * `RgbTo16EmptyRowKeepsRowTableSync` — empty-row offsets/counters
  * `RgbTo16RawFullBitmapGoldenBlob` — raw branch, black stays 0
  * `Convert8To16GoldenBlob` / `Convert8To16HalfBrightUsesDarkMask` /
    `Convert8To16RawIsUnsupportedAndReports` (exact message)
  * `ConvertToNewDispatchThroughLeaves9` — hook == direct, depth/target gates
  * `BankConvertNewConvertsADepth2Bank` (exact cursor/offsets/seq-data) +
    `BankConvertNewEdgeReturns`
* `tests/e2e/shape_convert16_e2e_test.cpp` — **2 tests / 43 checks**, GUARDED on
  `GUILD_GAME_DIR` (skip cleanly when absent):
  * `RealDepth2BankConvertsAndBlitsRealArtwork` — converts the real
    `_WIN_BORDER` bank, FLIPS the session-hud no-op proof (ShowFromBank now
    paints > 0 pixels), cross-checks opaque counts + first pixel against the
    independent depth-2 decode (@0x5fbb24) + PackColor, determinism (byte-equal
    re-convert, identical re-blit).
  * `HudBridgeFeedsRealConvertedBank` — `SetHudSpriteBankFromGfx` →
    `BlitHudSprite` paints real artwork; revert path.
* `tests/unit/wire_worldnet_test.cpp` updated: the leaves9 converter slots are
  asserted REAL (a minimal shape converts through the installed hook).
* Regression: `session_hud_test` (43 checks) and `session_hud_e2e_test`
  (35 checks) still pass unchanged.

## Deferred / out of scope

* `VIBE_State_Helper` @0x40e014 (d2_LoadObj) — the lazy-load/eviction record
  bookkeeping around the ConvertNew call (budget `dword_62D20C` +
  `VIBE_Resource_EvictOldestEntry` @0x40decc) remains with the resource-manager
  module; the conversion it invokes is now fully real.
* `VIBE_ShapeBank_GrabFromFile` @0x5d8a10 — unrelated driver, still a hook in
  shape_recon_cluster.
