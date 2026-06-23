# Harden report — src/sim/objectsearch.cpp

Line-for-line 1:1 audit of every provenance'd function in
`src/sim/objectsearch.cpp` against the gilde.exe decompile + disasm via IDA MCP
(module `gilde.exe`, imagebase 0x400000). DISASM is authoritative over Hex-Rays.

| addr | function | verdict |
|------|----------|---------|
| 0x559d98 | MatchEntityFilter | VERIFIED-1:1 |
| 0x559ff8 | MatchEntityFilterWithStatus | VERIFIED-1:1 |
| 0x47b1d0 | FindNearestEntity | VERIFIED-1:1 (probe geometry); favourability gate DEFERRED (documented) |
| 0x47b308 | FindEntitiesByCount | VERIFIED-1:1 (probe geometry); favourability gate DEFERRED (documented) |
| 0x4784cc | ObjectRing_AdvanceIterator | FIXED -> VERIFIED-1:1 (2 deviations corrected) |

## Constants verified with get_bytes

* `dword_478450` (probe stride table, 16 dwords @ 0x478450):
  `01 03 05 07 0B 0D 11 13 ED EF F3 F5 F9 FB FD FF` — matches
  `kObjectProbeStrides` exactly. All odd / co-prime with 256 (single-pass).
* `dbl_61AC18` @ 0x61AC18 = `00 00 00 00 00 00 59 40` = **100.0** (FindNearest
  favourability-range arming threshold `a5 < 100.0`).
* `dbl_61AC20` @ 0x61AC20 = `00 00 00 00 00 00 59 40` = **100.0** (FindByCount).

## Key 1:1 findings

### Faction-mask / require-status byte OVERLAP (both Match* funcs)
The original reads the faction mask as `*(_DWORD*)(a2+8)` and the "require
status" sign byte as `*(char*)(a2+11)` — **byte 11 is the high byte of the same
dword**. They are not independent fields. The reimpl's `fb[]` reconstruction
reproduces this exactly: `fb[11] = requireStatus` is written into the top byte of
the dword later read by `RdD(fb,8)`. So a negative `requireStatus` forces the
mask's top byte to 0xFF (faction membership then needs a bit in 24..31).
Confirmed correct in source; this overlap drove two initially-wrong golden
vectors (see below).

### MatchEntityFilter (0x559d98)
* Empty-filter early-out `!mask && !a2[12] -> return 1` (0x559ea9): matches.
* Sign gate `(i8)a2[11] < 0 && ((a3[90]&1) || !*(int*)(a3+97))` (0x559e10):
  matches — the +97 dword test is present (this variant only).
* Faction byte = `*(u8*)(dword_13CE294 + 589 * *a3)` where `*a3` = type byte at
  +0: `a3[kObjAlive]`. Matches.
* Owner-mode switch arms 1..7 (jump table @ 0x559ed9), incl. mode 7's extra
  `dword_6498E4` compare and the `0xFFFF` "no owner" sentinel handling: matches.
* Owner-list XOR refinement `v17 = listOwner ^ owner` (==0 -> filtered) over the
  4-word list at a2+14, guarded by `a2[13]&1 && owner!=0xFFFF`: matches.
* Final `return v16 && v5 && v17`: matches.

### MatchEntityFilterWithStatus (0x559ff8)
* Faction byte computed unconditionally right after the empty-filter check, then
  the status-bitmask reject `((1 << bit) & 0xF82806F) && (a2[11] & 0x40) ->
  return 0` (0x55a064): matches (mask constant verified by value).
* Require-status sign gate here tests **only** `a3[90]&1` (no +97 dword), unlike
  the non-status variant (0x55a086): matches.
* Membership / owner-mode switch / owner-list refinement (sets `v16=0` on a list
  hit, not the XOR): all match.

### FindNearestEntity (0x47b1d0) / FindEntitiesByCount (0x47b308)
* Probe: `stride = dword_478450[RandomModulo(16)]`, `start = RandomModulo(256)`,
  256-slot budget, `next = (slot + stride) % 256` (signed idiv; operands keep the
  result == unsigned for the in-range values, verified), record stride 0xA9=169,
  alive test at +0, id read at +1. All match. The RNG draws are surfaced as the
  `strideIndex`/`probeStart` test seam (deterministic), per the module header.
* The favourability gate `VIBE_Ai_ComputePersonFavorability` (0x594330) and its
  range-arming (`(a4 > 0 || a5 < 100.0) && a4 < a5`) plus the float compare
  (disasm 0x47b2f2: `fcomp`; `jb`/`ja` both skip => accept only when fav EXACTLY
  equals the threshold, i.e. `fav >= lo && fav <= lo`) are **DEFERRED**: the gate
  lives in AI/relation code outside this module and callers pass it inactive.
  Documented in the file header; not faked (rule 8). FindNearest returns
  `found != 0`; FindByCount returns the collected count (`a6` is `unsigned __int8`
  in the original — noted; the test seam passes small counts).

### ObjectRing_AdvanceIterator (0x4784cc) — FIXED
Two genuine deviations from the disasm, corrected in source:

1. **count==0 must NOT store the cursor.** Disasm 0x4784e1 loads `dword_B596A0`
   into eax and returns with *no* `mov [ecx], ...` — the cursor is left intact.
   The old reimpl wrote `*ring.cursor = 0`. Fixed: the count==0 branch now returns
   0 without touching `*cursor`.
2. **Modulo by count is UNSIGNED.** `(v0-base)/12` is a signed `idiv` (0x4784fb
   `sar edx,1Fh`), but `(bias + index) % count` is an **unsigned** `div edi`
   (0x47850d), with `bias` read as unsigned (`off_4784C0[1]`). The old reimpl used
   signed `%`. Fixed to compute the modulo in `u32`. (For valid non-negative
   state the results coincide; the fix makes the wraparound bit-exact.)

The address arithmetic `lea eax,[edx*4]; sub eax,edx; shl eax,2` = `12*index`
(3 dwords) was already correct.

## Tests

New: `tests/unit/sim_objectsearch_harden_test.cpp` (target
`sim_objectsearch_harden_test`, suite `ObjSearchHarden`) — 15 tests / 67 checks:
null cases, empty-filter early-out, require-status sign gate (incl. the +97 test
and the mask/sign byte overlap), all owner-mode arms 1..7, faction membership,
owner-list XOR refinement, the WithStatus bitmask reject + status-only-+90 gate,
the verified stride table, FindNearest/FindByCount probe geometry, and the
ObjectRing count==0 no-store + rotation/wrap behavior.

Two golden vectors were initially WRONG (set faction mask = 0 together with a
negative requireStatus, which is impossible given the +8/+11 byte overlap) and
were corrected to the binary's behavior — source was right.

## Build & run (real targets discovered via `ctest -N`)

```
cd build && GUILD_GAME_DIR=$PWD/../europe_guild_1400_original \
  ctest -R "objectsearch|sim_object_test|sim_path_test|sim_path_query|aiaction_finder|pathfind_map" \
  --output-on-failure
```
Result: 12/12 pass (includes `aiaction_finder_*`, which drive FindNearestEntity
through the live call tree, and `sim_path_test::ObjectRingAdvance`).
