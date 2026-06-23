# Hardening: sim — Farm/Estate Master AI

Owner file: `src/sim/ai_meister_calc_farming.cpp` (+ `tests/unit/ai_meister_farming_test.cpp`)

IDA module `gilde.exe`, imagebase 0x400000. Every function with `gilde.exe 0xADDR`
provenance was decompiled AND disassembled via MCP and diffed line-for-line against
the source.

## Functions audited

| Function | Addr | Verdict |
|----------|------|---------|
| `MeisterAssignWorkstations` | 0x4599f0 | VERIFIED-1:1 (no changes) |
| `CalcMeisterFarming` | 0x454f50 | FIXED (3 fix sites) + wiring restored |

## Cross-checked helper signatures (NOT edited — other owners)
GetEffectiveStock@0x5923fc, QueueRequest20@0x4946f4, SumWorkstationByCategory@0x5904fc,
ComputeWorkstationOutput@0x45b948, CollectProductionSlots@0x5922d4, FindObjectById@0x583a70,
QueueRequestMixed44@0x494cf0, ComputeFreeCapacity@0x5924a8, FindActiveWorkSlot@0x586904.
MeisterDispatchOrders@0x45d618 and AssignIdleWorkers@0x45379c decompiled to recover the
exact args overlay and call wiring (implemented in sibling `ai_meister_passes.cpp`).

## Bugs found and fixed

### FIX 1 — Loop-3 (revoke bit 0x08) double-increment  [definite bug]
- Site: work-order loop 3, `gilde.exe 0x455304..0x4553a0` (disasm 0x45531d).
- The original is a single clean loop: `test word_B564BC[esi],8 ; jz loc_455386`
  (fall through to the post-increment `add esi,0x58 / inc var_1C`). No second
  increment.
- The reimpl had `if (!(rdWoBits(wi) & 8)) { ++wi; continue; }` — the stray `++wi`
  double-advanced the loop counter, so every other bit-clear WO was skipped (its
  bit-0x08 validation/revoke never ran).
- Fix: removed `++wi`; now `if (!(rdWoBits(wi) & 8)) continue;`.

### FIX 2 — Wired the previously-DEFERRED dispatch pass  [behavioral gap / Rule 8+13]
- Site: `gilde.exe 0x455573..0x4556a0` (the bit-0x10 and bit-0x08 dispatch loop)
  and `0x455ca4` (AssignIdleWorkers).
- The reimpl had stubbed BOTH `VIBE_MeisterAi_DispatchOrders` (3 call sites: mode 8,
  mode 40/3, mode 4) and `VIBE_Ai_AssignIdleWorkers` to no-ops with TODO comments,
  although `MeisterDispatchOrders(MeisterDispatchArgs*)` / `MeisterAssignIdleWorkers()`
  ARE declared in `ai_meister.h` and implemented in `ai_meister_passes.cpp`. So real
  worker dispatch + idle-worker assignment never happened — a divergence.
- Recovered the exact stack-args overlay from disasm (0x4554e7..0x455511):
  a1[1]=meisterRec=mr, a1[2]=orderRow=`&dword_B56468[v32/4]` = `wo(v31)+4`,
  a1[3]=v93 (type-42 node), a1[4]=v94 (type-254 node), a1[5]=divisor=v84,
  a1[6]=total=v85, a1[7]=passIndex (0 init), a1[8]=already (0 init).
- CRITICAL: the original REUSES one stack array across all calls AND iterations, so
  `passIndex`/`already` ACCUMULATE (`MeisterDispatchOrders` does ++a1[7]/++a1[8]).
  The fix keeps ONE persistent `MeisterDispatchArgs` across the whole loop.
- AssignIdleWorkers (0x45379c) takes eax = the type-42 node (NOT the meister); wired
  via `g_aiIdleWorkstationNode = (u8*)v93;` then `MeisterAssignIdleWorkers();`
  (extern-declared the existing non-static global from `ai_meister_passes.cpp`).

### FIX 3 — Width of the qty / threshold comparisons  [faithfulness]
- Sites: loop-1 0x455706/0x455724/0x455752, loop-2 0x4558.. (node28/node29/threshold).
- Disasm: `xor eax,eax; mov al,[v93+0x1C/0x1D]; cmp eax, dword_B56484/B56488` and
  `cmp u16 threshold, dword_B564A8` — i.e. the WO qty/minQty fields are read as full
  signed `int` dwords, not u8/u16. The reimpl truncated them with `(u8)`/`(u16)` casts.
  Behaviorally identical given the live invariant (qty1/qty2/minQty are init to 0 and
  never written nonzero in this flow), but the casts were removed to be exactly 1:1.

## VERIFIED-1:1 (audited, correct, no churn)

- **Phase A bit-0x04 pass** (0x454ff0..0x4550dd): inner double-loop, stride math,
  `dword_B5646C` stock-back-index, `dword_B54454<<6` stride, `word_B5448C`/`word_B564BC`
  LOBYTE OR-4. The reimpl's restructured form computes the same `backWo` and stock
  matches. (Extra `backWo<count` guard is harmless.)
- **Harvest command (cmdType=9)** (0x455167..0x45523f): guard `(mr+456)&0x10` (=v89[228],
  456=228*2) and `bTypeDef0 != 11`; cmd field packing; LABEL_18 sets `mr+456 |= 0x10`.
- **Worker action-cancel sweep** (0x4553d6..0x455412): person stride 536 (0x218),
  768 persons / bound 411648 (0x64800), employer == `*(mr+0x16C)` (kM_bldgRec),
  `*(ao+44)==*(emp+1)`, `(!busy||*busy!=40) && (!v91||p!=mr)`. Confirmed `pr(pi)` =
  `word_12CE910 + 536*pi` == orig `v26`, so `p != mr` == `v26 != v89`.
- **min(v84,v85)** (0x4554a3): `if (v84 >= v85) v29=v85; v84=v29` ≡ `if (v84>v85) v84=v85`.
- **Odd-hour check** (0x455544): `WORD2(qword_13CE852) % 2` via zero-extended `dx`,
  positive value → signed `% 2` == `hour % 2`. Even-hour else clears `mr+0x1B4 & ~0x20`.
- **PFLANZBAR `mr+218` byte** (0x455a83): `test byte[mr+0x1B4],0x20` → kM_dayFlags (436).
- **Float→int Phase 8/9** (0x45a02c/0x45a0e6): `((double)v113*flt_6198FC + 1.0)*flt_619900
  + (double)reserved`, then `VIBE_Coord_ConvertX` (round-toward-zero) + fistp == `(i32)`
  truncation. flt_6198FC=0.01f, flt_619900=7.5f. Correct.
- **Trailing bit-0x08-clear loop** (0x4556c8): DEAD CODE — `v31` exits == count so the
  88*v31 < 88*count bound is immediately false; never executes in either binary or reimpl.

## Documented deferrals (unmodeled subsystem — Rule 8, NOT a translatable divergence)

- **PFLANZBAR office-placement interior** (0x455aad..0x455c61): gated on
  `dword_13ECF74[246*EnsureBuildingAvatar(...)]` (avatar/floor table). That table is
  not modeled (value 0 at the audited site), so the live binary takes `jz loc_455C5A`
  and skips the whole block — including the `RandomModulo` draw loop. The reimpl
  null-guards identically (no RNG consumed). Faithful AS LONG AS the floor table is
  empty; flagged for the orchestrator if/when the avatar/floor subsystem is added.
- **Debug order-snapshot copy** (`qmemcpy unk_B53AF0+88*v35, v81, 0x58` @0x455692): a
  selection-overlay debug buffer with NO sim side effect; the `g_aiSelOrderCount` bump
  is kept, the memcpy dropped per SPEC.

## Minor non-divergence notes

- Loop-3 non-type-23 stock-stock read (0x45536c): original indexes
  `dword_B54478[16*(-1)]` (UB out-of-bounds) when a sub-slot back-index is -1; the
  reimpl bounds-guards to 0 (`thr3 > 0` → clears bit 0x08). Kept the guard — the
  original's read is UB and unreproducible; the WO sub-slots reaching here are valid
  stock indices in practice.

## Tests
`tests/unit/ai_meister_farming_test.cpp` — 41 checks, 0 failures.
Added Test 25 `CalcFarming_DispatchPass_Wired_NoCrash` exercising the newly-wired
DispatchOrders + AssignIdleWorkers path through the full flow (qualifying worker +
one WO) and asserting no crash + workOrderCount.

## Build note (environment, pre-existing — NOT caused by this change)
The generated `ar qc libguild.a <1056 objects>` exceeds the shell arg limit and fails
with `ranlib: malformed archive`. My TU compiles clean (0 errors/0 warnings). Built
the archive once via an `@response-file` to link/run the test. The build-system arg
overflow is unrelated to this file and out of scope.
