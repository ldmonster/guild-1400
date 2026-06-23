# Harden pass — render_07 texlight

Scope: `src/render/texlight_recon.cpp` + `.h`, `src/render/texlight_recon3_animset.cpp` + `.h`,
tests `tests/unit/texlight_recon_test.cpp`, `tests/unit/texlight_recon3_animset_test.cpp`.

Method: per provenance-tagged function, `decompile` + `disasm` from IDA Pro MCP
(module gilde.exe, imagebase 0x400000) and diffed line-for-line against the
reconstruction; every constant/table read with `get_bytes`.

## texlight_recon.cpp

| Function | Addr | Verdict |
|---|---|---|
| SurfaceCache::Init | 0x5d93f4 | VERIFIED-1:1 |
| SurfaceCache::FreeAll | 0x5d9430 | VERIFIED-1:1 |
| SurfaceCache::StoreEntry | 0x5d94c8 | VERIFIED-1:1 |
| SurfaceCache::EvictAndStore | 0x5d9580 | VERIFIED-1:1 (degenerate off-end path, see note) |
| HiColTabBank::FindOrBuild | 0x5da04c | VERIFIED-1:1 (fast-path OOB note) |
| BuildLog10ByteTable | 0x5d988c | **FIXED** — float→int rounding mode |

### FIXED — BuildLog10ByteTable (0x5d988c)
The reconstruction used `std::lrint` (round-to-nearest-even) and the doc/golden
claimed "round-to-nearest". DISASM is the reference of record:

- Loop @0x5d98f4..0x5d991d: `fild(v6)` → `VIBE_Math_Log10` → `VIBE_Coord_ConvertX`
  → `fistp`.
- `VIBE_Coord_ConvertX` @0x5c6b08 sets the x87 control word `HIBYTE(cw)=0x1F`
  (RC field bits 10–11 = `11` = **round toward zero**) then `frndint`; the
  subsequent `fistp` inherits that control word. So the conversion **TRUNCATES
  toward zero**, not round-to-nearest.

Fix: `(int)std::trunc(l)` in the source; header + golden comments corrected.

Golden test `TexLightReconLog10Table.GoldenValues` encoded the WRONG (round)
values — fixed BOTH source and golden to the binary:
- t[10]  (log10(9)=0.954):   1 → **0**
- t[100] (log10(99)=1.995):  2 → **1**
- t[4096](log10(4095)=3.612):4 → **3**
- t[5]   (log10(4)=0.602):   1 → **0**
(t[1],t[2],t[11],t[101],t[1001],t[4] already correct under truncation.)
log10(0)=-inf path unchanged: x87 fistp yields int-indefinite 0x80000000,
LOBYTE=0 ⇒ t[1]=0. VERIFIED.

### Notes (binary-faithful, left as-is)
- EvictAndStore "not-full but no free slot" path: the original `return result`
  (a non-null off-the-end pointer) at 0x5d9642; the reimpl returns -1. This is an
  impossible state (liveCount != capacity yet every slot occupied) and is
  documented inline. No observable divergence on any reachable input.
- FindOrBuild fast path (0x5da097): requires `used == capacity`, at which point
  the original reads `rec[776*used+772]` — one record PAST the allocated array
  (OOB/uninitialised). The reimpl's `used < banks.size()` guard makes that
  condition unreachable. Not faithfully reproducible without an OOB read; left
  guarded (documented).
- Tables verified with get_bytes: none in this file (log10 table is computed).

## texlight_recon3_animset.cpp

| Function | Addr | Verdict |
|---|---|---|
| AnimSet_IsDigitClass / kCharClass | byte_64A208 | VERIFIED-1:1 (256 bytes confirmed) |
| StrToUpper (helper) | 0x5e9f50 | VERIFIED-1:1 |
| ParseInt (helper) | 0x5dc070 | VERIFIED-1:1 (for digit-run input it consumes) |
| CopyName (helper) | loc_5DA4D9 | VERIFIED-1:1 |
| StrChr semantics (last match) | 0x5d3ef0 | VERIFIED-1:1 (reimpl uses strrchr) |
| AnimSet_Analyze | 0x5da4b4 (analysis half) | VERIFIED-1:1 |
| VIBE_Texture_LoadAnimatedSet | 0x5da4b4 | VERIFIED-1:1 (header comment FIXED) |

### Verified line-for-line
- ctype table `kCharClass` == `byte_64A208` (all 256 bytes via get_bytes). ✓
- Trailing digit scan @0x5da564: digit-class test FIRST (`working[edx]`), then
  `edx<=0` (unsigned `jbe`), then set sawDigit + `--edx`. ✓
- Gate @0x5da596..0x5da5b5: `edx >= len-2` (jnb), `edx <= 1` (jbe),
  `upper[edx-1]=='A'`, `upper[edx-2]=='_'`, sawDigit — all unsigned compares,
  exact. Digit scan uses `working`, the A/_ check uses `upper`. ✓
- frame!=0 gate @0x5da5d9..0x5da5e5: abort iff `frame!=0 && (flags&0x40000)==0`. ✓
- flagsOut = `flags|0x40000` (BYTE2|4 @0x5da603). ✓
- rec.curAnimId = dword_1406A58 @0x5da606; rec.frameStamp = (u8)frame @0x5da609
  (`mov al,[var_18]` — LOBYTE of the parsed frame, NO +1). ✓
- member truncation @0x5da621: `member[edx+1]=0`. ✓
- exists path: `%s/%s%i.bmp`(dir,member,frame+1) then `%s%i`(member,palIdx),
  LoadByName(name, flags|0x40000, palIdx, loadFlag). ✓
- absent path bank walk @0x5da684: refCount(+0x40)>0 && curAnimId==animId(+0x50),
  matcher loc_5CB930, stamp frame(+0x70)=(u8)(frame+1) `inc al`, then
  `++dword_1406A58` once after the loop. ✓

### FIXED — header doc only (no code/golden change)
`texlight_recon3_animset.h` comments incorrectly said the caller record's +0x71
is stamped with `frame+1`. DISASM @0x5da609 (`mov al,[var_18]` with no `inc`)
shows it is `(u8)frame` (LOBYTE). The +1 applies only to the *bank* records'
+0x70 (`inc al` @0x5da6ae). The `.cpp` (`rec.frameStamp = (u8)p.frame`) and the
golden tests (frameStamp==12 for frame 12, ==78 for frame 78) were already
correct; only the two stale header comments were corrected.

## Build / test
- Built targets `texlight_recon_test`, `texlight_recon3_animset_test` (no full
  rebuild, no build-dir touch).
- `ctest -R texlight_recon_test|texlight_recon3_animset_test` → 2/2 PASS.

## Counts
- Functions audited: 12 (6 + 6 incl. helpers).
- VERIFIED-1:1: 11
- FIXED: 1 logic (BuildLog10ByteTable trunc-vs-round, source + golden) + 1
  doc-only (animset header +0x71 comment).
- BOUNDARY (inert GPU/VFS hooks, math verified, only the API call hooked):
  SurfaceCache release/probe (DDraw), BuildBmpPath/FileExists/LoadByName,
  loc_5CB930 matcher.
- Golden vectors corrected to binary: 4 (log10 table indices 10,100,4096,5).
