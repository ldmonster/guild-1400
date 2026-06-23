# Harden report — src/sim/ai_recon5_decisions.cpp

Verified directly against gilde.exe via IDA MCP (decompile + disasm + get_bytes).

## VIBE_Math_RandomModulo @0x58b89c — VERIFIED-1:1
`a1 ? (int)RandNext() % a1 : 0`. RandNext (@0x5cb8bc) LCG: `state = 1103515245*state + 12345; return HIWORD(state) & 0x7FFF`. Reconstruction's `n ? r % n : 0` matches; signed `%` matches `(int)... % a1`.

## VIBE_Math_RandomFloatScaled @0x58b910 — VERIFIED-1:1
`(double)(int)RandNext() * flt_62675C`. Disasm: `fild` (signed int load) then `fmul flt_62675C`. get_bytes @0x62675C = `00 01 00 38` = 0x38000100. Reconstruction's `RandScale()` uses exactly 0x38000100. Match.

## VIBE_AiCardGame_PlaceBet @0x46670c — VERIFIED-1:1
- All float constants confirmed via get_bytes:
  dbl_61A188=10000.0 (40C3880000000000), dbl_61A190=0.1 (3FB999999999999A),
  flt_61A198=0.25 (3E800000), flt_61A19C=0.1 (3DCCCCCD), flt_61A1A0=-0.2 (BE4CCCCD),
  flt_61A1A4=0.11 (3DE147AE), flt_61A1A8=0.2 (3E4CCCCD). All match source literals.
- FLOAT->INT SITES (the critical bug class): both conversions are `fistp` *preceded by*
  `VIBE_Coord_ConvertX` @0x5c6b08 (disasm 0x4667ef->fistp 0x4667f4; 0x46681b->fistp 0x466820).
  ConvertX sets FPU round-toward-zero, so these fistp TRUNCATE. C++ `static_cast<i32>(double)`
  truncates toward zero → MATCHES. (int)v12 and (int)v16 both correct.
- RNG draw order/count: 2 draws (gate fcomp @0x466823, then 1 per branch). Reconstruction
  draws `pick` then 1 per branch. Match.
- Branch sense: `jbe loc_46688D` (RandomFloatScaled <= familyAgg) → humanSeat/B path uses
  flt_61A1A0 (-0.2). Reconstruction `pick <= familyAggIn` → humanSeat=1, uses -0.2. Match.
- NOTE: log term uses x87 `FYL2X`(v23, log10(2))+1.0 in binary; reconstruction uses
  std::log10(v23)+1.0. Mathematically identical; last-bit x87-80 vs double precision possible
  before truncation. Acceptable faithful translation (documented).

## VIBE_AiPlayer_ExecThreaten @0x47d568 — VERIFIED-1:1
Gate `*a1==7`, then FindRecordById, then `ComputeRatingCurveA(4,..) + RandomFloatScaled() >= 1.0`.
Binary evaluates RandomFloatScaled FIRST (v16 @0x47d5b1) then adds ratingCurveA; sum is
commutative and ratingCurveA consumes no RNG, so reconstruction's order (ratingCurveA then
RandomFloatScaled) is observationally identical. result=1. Match.

## VIBE_AiMethod_SelectConversationTarget @0x4675b8 — VERIFIED-1:1
5-slot ring (a1+12..a1+32 step 4). candidatesPresent(v19)++ on resolve; eligible(v5)++ when
alive(+8) AND relation(word[5]) > 0xB, collecting record. `v5 ? pool[RandomModulo(v5)] : 0`
(binary uses 1-based v17[idx+1]; reconstruction 0-based pool[idx] — same k-th selection).
type==5 leave path, default self-preference (type in (1,10), !(flag&4)), then random pick if
type in (1,10). One RandomModulo draw when eligible. Match. (Pool cap 8 vs binary's untracked
v17[6] is moot: max 5 eligible.)

## VIBE_AiObject_CountInventoryMatch @0x47a524 — VERIFIED-1:1 (hook-shaped)
Class mask 0x7FFF, cat gating (33 skip; 2/6 owned-then-forsale-then-need; else need-check),
threshold compares, missing-break emitting masked class + objId(-1 sentinel). Logic matches;
the table/QueryFind lookups are hook-abstracted data boundaries.

## Counts
VERIFIED-1:1: 6   FIXED: 0   BOUNDARY: 0
No source or golden changes required.
