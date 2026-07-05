# Harden — recovered-table verification sweep (get_bytes diff)

Method: for each `const` array carrying a `@0x…` provenance, fetch the raw bytes via
the IDA MCP `get_bytes` and diff programmatically against the source array. This has
been the single highest-yield hardening check — a real transcription error in **3 of
3** data-heavy areas swept so far, each missed by hand-review and by the existing tests
(the mis-set values were latent or untested goldens). Suite green 1558/1558 after each.

## Findings

| Table | Addr | Bug | Impact |
|---|---|---|---|
| resolution table (`ini.cpp`) | `0x63D70C` | had 6 rows; real table is **3** — rows 3-5 were the adjacent output globals mis-read | idx≥3 phantom resolutions (invalid input path) |
| `kStrtolCtype` (`ctype.cpp`) | `0x64A208` | **shifted right by 1** from idx 92; `a-f`=0x0C vs 0x98, `g`=0x98 vs 0x88 | latent — consumers read only whitespace bit 0x02/0x20 |
| `kStat582900` (`person_create.cpp`) | `0x582900` | **shifted left by 2** from idx 24; 12 alt-pairs before the 16/24 entry vs the binary's 13 | **behavioral** — swaps person stat slots 12 & 13 seed ranges (16..24 vs 2..3), zeroed real 2.0/2.7 at 38/39 |

The `person_create` one is a genuine game-balance divergence: the stat-seed loop reads
`fi = 2*t` for `t=0..13` (indices 0..27), so the 2-slot shift gave stat slot 12 the
16..24 range and slot 13 the 2..3 range — reversed from the engine. No golden test
covered those float values, so it was silently wrong. Fixed to byte-exact.

| `kCharClass` ×2 (`script_lexer.cpp`, `texlight_recon3_animset.cpp`) | `0x64A208` | **shifted LEFT by 1** from idx 91 (a 3rd independent transcription of the strtol table, wrong the OTHER way) | latent for the `&0x20` tokenizer test; but a spot-check test pinned byte 127 to the pre-fix 0x01 → corrected to 0x0C |

The `0x64A208` table now has THREE call-site copies (`kStrtolCtype`, two `kCharClass`)
all reconciled to the binary — each had been transcribed with a different off-by-one.

## Verified 1:1 (byte-exact, no change)

- `kPctype` (`__pctype`) `@0x14529ba` — 256×u16.
- `kByte649910` / `kByte649AD8` (talent) `@0x649910`/`@0x649ad8` — 48 B each.
- `kOfficeDefTable` (`world/office.cpp`) `@0x62EC8E` — 446 B (council/office game table).
- `kBuiltinFontBitmap` (`render/text_raster.cpp`) `@0x62D59C` — 637 B.
- `kVersionStringBytes` `@0x622990`, `kTemplate` (gametime) `@0x13CE85E`,
  `kDrinkTable` (`ai/aimethod2.cpp`) `@0x466445`, `kDefaultImage` (`world/event.cpp`) `@0x63CD48`.
- **DRM crypto** `kKeyStream9`/`kSigSeed9`/`kSectorTab9`/`kRotTab9`/`kKeyTableSeed9`
  `@0x145A790/0x142DD60/0x142DD50/0x145A77B/0x145CAF0` — all 9-byte seeds byte-exact.

## False positives (not bugs)
- `kDistCode` (`compress/trees.cpp`) — the standard zlib `_dist_code` table, not a
  game global; the scanner grabbed a nearby unrelated address.

## Tally so far
**4 real transcription fixes** (resolution table, strtol table, kCharClass ×2, stat
table — one of them, `kStat582900`, behavioral) across **~20 tables** swept. Many more
`recovered byte-for-byte` tables remain in `sim/`, `io/`, `world/`; the check is cheap
and keeps paying off — continue, prioritizing game-logic value tables.

## Sweep 2 — io/ai/gui/util/audio/net

Directories `src/io src/ai src/gui src/util src/audio src/net` (hardened before the
get_bytes method existed). Every `const` data array with a `@0x…`/`byte_/dword_/flt_/dbl_`
provenance was fetched via `get_bytes` and diffed programmatically (python) — no eyeballing.

### Fixes

| Table | Addr | Bug | Impact |
|---|---|---|---|
| `kDrinkTable` (`ai/aimethod2.cpp`) | `0x466445` | declared `[119]` with only 115 initializers → bytes 115-118 zero-padded, but the binary has `04 00 03 01` there (the adjacent `kChoiceTable @0x4664B8`) | **non-behavioral**: max read index is 114 (class-6 `randScale` at entry+15 = bytes 111..114); indices 115-118 are never read by any accessor and no `sizeof` uses the size. Corrected to `[115]` so the array owns exactly the bytes it reads (0..114), all byte-exact to the binary, and the false zero-pad that diverged from the ROM is gone. |

The 115 vs 119 was an internal inconsistency — the comment itself said "seven 16-byte
entries (112 B) plus the 3 trailing bytes" = 115. Tests after fix:
`aimethod2_test` **95 checks / 0 fail**, `aimethod2_e2e_test` **15 / 0**.

### Verified 1:1 (byte-exact, no change)

- `ai/cardgame.cpp`: `kCardThresholds[5][6]` `@0x466394` (120 B float dist table),
  `kTransitions` `@0x46637D` (24 B), scalars `kF_61A1B0/B4/B8` `@0x61A1B0..B8`,
  `kD_61A1C8` `@0x61A1C8` (0.44).
- `ai/meisterai3.cpp`: `kChoiceTable[3][4]` `@0x4664B8`.
- `ai/needs.cpp`: `kNeedRestockTable` `@0x647728` (10×12 B {needId,scale,cap}).
- `ai/types.h`: `kNeedGroupIdMap` `@0x5830F0` ({2,3,4,5}).
- `ai/aimethod2.cpp`: drink table raw 115 B + derived weight/base/randScale arrays
  ({10,13,16,19,23,27,32}/{20..140}/{40..160}).
- `ai/aiaction_finder.cpp`: search-radius floats `@0x61AC30` (6: 32,32,77,60,40,18).
- `ai/desire_table.cpp`: `kGoodsTable` `@0x6496A9` (**23 rows × 21 B = 483 B**; key@+3,
  8 good-id words@+4) — the big game table; byte-exact.
- `ai/building_needs.cpp`: `kBuildDiffScale` `@0x4C5140` ({4,3,2.5,1.5,1}) + owned<3
  ratio gate `flt_61E85C` = 0.5. (Table actually lives at `flt_4C5140`, confirmed via
  decompile of 0x4c7774.)
- `ai/meister_workstation.h`: `flt_6198FC`(0.01)/`619900`(7.5)/`619904`(0.01)/
  `dbl_619908`(-1e10)/`619910`(0.9)/`619918`(-1e10).
- `io/vfs_tree.cpp` `kBinExtTable` `@0x44E8F0` (7×6 B `.BIN5`..`.BIN`); `io/vfs.cpp`
  `kBinExtensions` = same table.
- `io/file_ops2.cpp`: `kErrnoTable` `@0x1455170` (45 os→errno pairs, 360 B).
- `util/matrix.cpp`: `kRefAxisA` `@0x5CA2D0` ({0,1,0}), `kRefAxisB` `@0x5CA2B0` ({0,0,1}).
- `util/locale_recon.cpp`: `kLanguageTable` `@0x5A3260` (5×64 B; GERMAN/ENGLISH/FRENCH/
  ITALIAN/SPANISH).
- `gui/hud_menu2.cpp`: `kBridgeAngleBuckets[9]` `@0x623FF0` (9 doubles pi/8+k·pi/4);
  the b[5]-repeat overlap in the source matches the decompile of 0x543da0 exactly.
- `gui/talent_dialog`: point→level ladder (thresholds 0x2A/0x54/0x7E/0xA8/0xD2 → levels
  1/2/3/5/7/10) — code-immediate compares, matched against decompile of 0x546ce8.
- `gui/text/text_strtok.cpp`: `kBitMask` = {1,2,4,…,128} (self-evident 2^k).

### Non-tables / false positives
- `gui/hud_grid.cpp` (person grid/column switch tables), `gui/mapview.cpp`
  (`kMapCornerPlacements`) — reconstructed from switch/call-sequence logic, not raw
  `.rdata`; already annotated against their decompiles, nothing to byte-diff.
- `src/audio/` and `src/net/`: **no** `const` data tables (only mutable state globals like
  `dword_62EA08` channel-table base) — nothing to sweep.

### Sweep-2 tally
**1 fix** (`kDrinkTable` size/pad, non-behavioral) across **~25 tables/constant-clusters**
swept in these six dirs. The only behavioral game tables here (`kGoodsTable` 483 B,
`kCardThresholds`, `kNeedRestockTable`, drink table, `kBuildDiffScale`, bridge buckets)
were all already byte-exact — a good sign these dirs were transcribed carefully.
