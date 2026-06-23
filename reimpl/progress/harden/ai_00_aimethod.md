# Hardening report — AI method cluster (ai_00_aimethod)

Scope (edited only): `src/ai/aimethod2.cpp`, `src/ai/method.cpp`, `src/ai/aiplayer.cpp`
(+ owned headers `method.h`). Tests run: `aimethod2_test`,
`aimethod_index_boundary_test`, `aimethod_recon3_registry_test`,
`aimethod_score_ai_recon2_test`, `aimethod2_e2e_test`, plus `ai_meister_test`
(exercises ClassifyMeisterRoutine/EvaluateMeister).

Method: every `gilde.exe 0xADDR` provenance was `decompile`d AND `disasm`d, then
diffed line-for-line (control flow, branch conditions, switch arms, constants via
`get_bytes`, float->int sites against ConvertX/fistp/(int), x87 accumulation,
signed/unsigned shifts and modulo, RNG draw count+order, struct offsets/stride).
DISASM was treated as ground truth.

## Verification of helper semantics (cited)
- **ConvertX @0x5c6b08**: sets FPU control word high byte to 0x1F (RC=11 round-toward-
  zero, PC=11) then `frndint` -> **truncates toward zero**. Confirms `TruncToZero =
  std::trunc` at every `(int)v6`/`fistp` site (ClassifyWealthTier, WealthScoreA/B,
  ClassWeight, DistancePenalty). VERIFIED.
- **NormalizeAngle @0x5eef4c** + **StoreAndZero @0x602968**: StoreAndZero stores
  `trunc(x)` (ConvertX), then NormalizeAngle adds `dbl_62BFFC == -1.0` iff `x < 0`.
  Result = `trunc(x)` for x>=0, `trunc(x)-1.0` for x<0. **NOT std::floor** (differs for
  negative integers, e.g. -2.0 -> -3.0). -> FIX (below).
- **RandomModulo @0x58b89c**: `idiv` with sign-extended RandNext, divisor = zero-
  extended u16 -> `(int)RandNext() % n`. Source matches. VERIFIED.

## Per-function results

### aimethod2.cpp
| addr | function | result |
|------|----------|--------|
| 0x467d60 | ClassifyWealthTier | **FIXED** — used `std::floor`; binary uses NormalizeAngle (`trunc(x)-1` for x<0). Replaced `FloorVal`->`NormalizeAngle(v)`. Switch arms 0..16 -> tiers, default 7, `v5<0->0`: VERIFIED. |
| 0x4679a4 | CompareFavorability | VERIFIED-1:1 (record-null->1, d-e>20->1, e-d<=20->RandomModulo(2), else 0; one RNG draw only in last arm). |
| 0x467c24 | ShouldInitiateConflict | VERIFIED-1:1 (gate; favA/favB*0.01; ratingCurve(4)x2; field^2*0.0025+v10; flag68 -> *1.1/*0.9; `v12+v11 < v13+v9`, fcompp/jnb). |
| 0x467ae0 | ComputeDrinkConsumption | VERIFIED-1:1 (16-byte table scan; cap=budget*0.33; per-round priceMul 0.5/1.0; drink=rand*randScale*priceMul+base; `next>=budget` break w/ 3-in-4 *0.75; `drink>cap` break w/ spent+=cap; cap*=0.8; flag; `-spent` out. double `next` vs float `spent` accumulation matches `fst var_24`+80-bit `fcomp`). |
| 0x467ed4 | ComputeWealthScoreA | VERIFIED-1:1 (floor 10000, coef 0.1, gain 32; FYL2X log10; ConvertX trunc). |
| 0x467f88 | ComputeWealthScoreB | VERIFIED-1:1 (same shape, coef 0.05). |
| 0x46803c | ComputeClassWeight | VERIFIED-1:1 ((wA+wB)*0.0005 trunc; stride-16 scan; weight@+7; no-match -> dword_46644C[0]==10. Note: binary `sar` (signed) `>>24` vs source unsigned `>>24` is table-bounded equivalent — top bytes 0x00..0x06). |
| 0x4685fc | ShouldFollowTarget | VERIFIED-1:1 (equal->0 via xor; (rank(target)-rank(self))*2.5; fav(self,target)+delta; `RandFloatScaled()*75 <= v10`, fcomp/jbe). |
| 0x468910 | ComputeDistancePenalty | VERIFIED-1:1 ((3*(rA-rB)+30)*scale * 1/3; signed fild; ConvertX trunc). |
| 0x4680b0 | LookupClassEntry | VERIFIED-1:1 (dword scan stride 4, >>24 match, +3 result; v2>=28 fail). |
| 0x469d9c/0x469db8 | EvalReturnSix/Seven | VERIFIED-1:1. |
| stubs 0x469da8.. | StubReturn0..3/6/7/8 | VERIFIED-1:1. |

Float constants @0x61A220..0x61A2B8 and drink table @0x466445 (119 B): all byte-
verified via `get_bytes` against source (20.0/0.33/0.8/0.75/0.01/0.0025/1.1/0.9/-1.0/
0.5/10000/0.1/0.05/32/0.0005/2.5/75.0/0.33333334; seven 16-B entries + 160.0 overlap).

### method.cpp
| addr | function | result |
|------|----------|--------|
| 0x58b89c | RandomModulo | VERIFIED-1:1. |
| 0x46797c | RandomBoolCheck | **FIXED** — disasm `mov eax, 2; call RandomModulo`: modulus is **hardcoded to 2**; function takes no arg. Source had `RandomBoolCheck(u16 n)` passing n through. Changed signature to `RandomBoolCheck()` returning `(u16)RandomModulo(2)!=0`. (No callers in src/ or tests.) |
| 0x467994 | RandomValue | **FIXED** — disasm `mov eax, 7; call RandomModulo`: modulus **hardcoded to 7**. Source had `RandomValue(u16 n)`. Changed to `RandomValue()` returning `(u16)RandomModulo(7)`. |
| 0x47b9c8 | CheckSameFaction | **FIXED** — binary `% 4` on low dword of qword_13CE852 uses `sar 1Fh; idiv` (**signed**), source used unsigned `% 4u`. Now `(int)gameTimeLo % 4`. The `% 8` operand is a 16-bit zero-extended word (`xor edx; mov dx`), modeled as `(gameTimeHi & 0xFFFF) % 8u`. RNG roll order (last, only if both eq) preserved. |
| 0x47b9c0 | AlwaysAllow | VERIFIED-1:1 (return 1). |
| 0x47be3c | PrepareGroupMember | VERIFIED-1:1 (cap<2->0; active<=0->0; half=cap/2; clamp min(half,active)). |

### aiplayer.cpp
| addr | function | result |
|------|----------|--------|
| 0x4533a8 (core) | ClassifyMeisterRoutine / EvaluateMeister | VERIFIED-1:1 (classifier core). cat 1/4 -> class 8/14 Craft else Production; cat 2 -> Farming; else switch class 19/4/16/5/9 -> Wache/Diebe/Ambush/Bank/PlanProduction. AiClass consts 4/5/8/9/14/16/19 confirmed. Surrounding budget/logging + entity reads remain documented-deferred (Rule 8 note in file). |
| 0x47d0e8 (core) | TrySingleAttackAccept | VERIFIED-1:1. flt_61AE3C @0x61ae3c = `00 00 80 3e` = **0.25f** (exactly representable; double model equal). `depth>3 -> reject`; `RandFloatScaled() >= depth*0.25` (fcomp/jb). |
| 0x47d7f8 (core) | PickBestTarget | VERIFIED-1:1. Sentinel `0xC1200000` = **-10.0f** = kPickSentinel; invalid score `0xC47A0000` = -1000.0f (skipped via valid-flag). best-find: strict `>` resets tied, `==` sets tied; tie-break draws RandomModulo(4) ONCE then scans 4 slots for valid&&score==best with `(idx+1)%4`. Draw count/order preserved. |

## Fixes applied
1. `aimethod2.cpp` ClassifyWealthTier: `FloorVal`(std::floor) -> `NormalizeAngle`
   (`trunc(x)` / `trunc(x)-1` for x<0) to match VIBE_Math_NormalizeAngle @0x5eef4c.
   Source comment + helper updated with addr evidence. Existing goldens still green
   (only positive/non-integer inputs + -3.0f which returns 0 either way).
2. `method.cpp`/`method.h` RandomBoolCheck: parameterized -> hardcoded modulus 2
   (disasm `mov eax,2`).
3. `method.cpp`/`method.h` RandomValue: parameterized -> hardcoded modulus 7
   (disasm `mov eax,7`).
4. `method.cpp` CheckSameFaction: unsigned `%4u` -> signed `%4` on low dword
   (`idiv` w/ sar 1Fh); `%8` operand masked to 16 bits to match `mov dx`.

## Counts
- Functions audited: 23 (with 0xADDR provenance) across the three files.
- VERIFIED-1:1: 19
- FIXED: 4 (ClassifyWealthTier float-floor; RandomBoolCheck/RandomValue hardcoded
  moduli; CheckSameFaction signed modulo)
- BOUNDARY/deferred: 0 new (EvaluateMeister tail + aiplayer Find* finders remain the
  pre-existing documented hooks, unchanged).

## Test results (GUILD_GAME_DIR set)
- aimethod2_test: 95 checks, 0 failures
- aimethod_index_boundary_test: 21 checks, 0 failures
- aimethod_recon3_registry_test: 96 checks, 0 failures
- aimethod_score_ai_recon2_test: 30 checks, 0 failures
- aimethod2_e2e_test: 15 checks, 0 failures
- ai_meister_test: ClassifyMeisterRoutine/EvaluateMeister tests PASS. 2 failures are
  in `CardGameCanPlayAndPlay` (`CanPlayCard`, src/ai/cardgame.cpp — OUT OF SCOPE,
  unrelated to these edits).

Note: the shared `guild` library was transiently un-buildable mid-run due to
concurrent edits in `src/audio/voicequeue.h` and `src/ai/meister_storage.cpp`
(other agents' in-flight work, outside this scope); the build went green once those
converged. My three files compile cleanly standalone (`-fsyntax-only` OK).
