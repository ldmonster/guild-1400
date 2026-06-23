# Harden pass — sim/ai_meister_equip + ai_meister_bank + ai_meister_core

Scope (3 owned TUs + headers + tests):
- `src/sim/ai_meister_equip.cpp` (5 funcs)
- `src/sim/ai_meister_bank.cpp` (1 func)
- `src/sim/ai_meister_core.cpp` (dispatch shim / globals)

Method: every `gilde.exe 0xADDR` function decompiled AND disassembled via IDA MCP,
diffed line-for-line. Bug classes audited: Hex-Rays pointer-typing strides, worker
emit guards, cmp/jg routing, float→int (ConvertX frndint truncation), RNG draw
count+order, signed/unsigned compares, constants/tables, side-effect order.

Build: each TU passes `g++ -std=c++17 -fsyntax-only`. Owned unit tests built and run
green (equip 51 checks / bank 70 checks — see below).

---

## ai_meister_bank.cpp — VIBE_Ai_CalcBankmeister @0x459264 — VERIFIED-1:1

Full disasm diff (308 insns). All record offsets confirmed against symbol
arithmetic: bldgRec +0x16C, bldgId *(bldg+1), cash +0x65, sceneRoot +0x5D,
owner +0x27, budget +0x1B8, flags2 +0x1C8.

- Reserve-rate state machine (BankmeisterNewRate, ai_recon_brain.h — REUSED, not
  owned): branch routing at 0x4594c2/0x45954d and the RNG **pre-decision** match the
  binary — only the single live branch draws exactly one `RandomModulo(3)`, in the
  matching arg. |diff|>3 draws none. Verified order/count 1:1.
- FP constants get_bytes-verified: 1.03 (6198C8), 1.5 (6198D0), 2.25 (6198D8),
  0.8 (6198E0). Match kBankReserve* in ai_recon_brain.h.
- ConvertX @0x5c6b08 confirmed: `fldcw` round-toward-zero + `frndint` ⇒ subsequent
  `fistp` TRUNCATES. Reimpl `(int)(double*k)` is exact.
- Final-balance FP compare `count < budget*1.5` (fcompp/jnb at 0x459446) — correct
  sense. Oversupply sell `count - budget*2.25` (fsubrp) — correct.
- netMoved (ebp): `+rawDelta` on buy (0x4593a4), `-rawDelta` on sell (0x459620). ✓
- 6/7 gate reads `byte_12CE912[536*owner]` = g_persons[owner]+2 (kP_kind=0x02). ✓
- Owner actor id `dword_12CE914[536*owner]` == g_personIds[owner] (flattened col). ✓

Boundary (documented, rule 8): lockstep command queue captured into BankCmdSink
(ReserveSet delta-packet + CoinOp mint/melt); He handler-count and coin-count
surfaced as leaves. No source change needed.

Tests: 70 checks, 0 failures.

---

## ai_meister_core.cpp — dispatch shim @0x4533a8 + scratch globals — VERIFIED-1:1

DispatchMeisterCalc routes kFarming/kWache/kDiebe/kAmbush (other module owns
kProduction/kCraftProduction/kBank/kPlanProduction). Globals mirror the live bases.
No divergence; no churn.

---

## ai_meister_equip.cpp — 5 functions

### EquipStaffWeapon @0x45e350 — FIXED (2 bugs) → now 1:1

**BUG 1 (pointer-array indexing) — the matched slot `v4`.** disasm 0x45e47b /
0x45e48c / 0x45e50c: the original captures `v4` from `v24 = unk_B54490`
(= dword_B5444E + 0x42 = g_stockTable+66), advanced **0x40 bytes PER FA/FC LIST
ITERATION** — NOT per matching stock row. On a hit it stores that list-position ptr
(`v26 = v24`). So `v4 = g_stockTable + 0x42 + 64*k` (k = list iteration index).
The reimpl had `v4 = st(si)` (the matching stock row) — wrong base (missing +0x42)
AND wrong index (si vs k). Field reads `*v4` (i16) and `*((_DWORD*)v4+10)` (count at
v4+40) therefore landed in the wrong place. Rewrote the FA/FC passes to track `v4`
as a byte offset `kUnkB54490Off(0x42) + 64*k`, and read/decrement via that pointer.

**BUG 2 (signedness of match key, latent).** original compares both sides with
signed `sar ...,10h`; reimpl mixed logical `>>16` (stock) with `(u16)` (target).
Aligned both to signed `>>16` (non-observable for valid positive type ids, but now
faithful).

Golden updated (cited): EquipStaffWeapon_CompatibleWeapon_CommandEmitted now seeds
the type word at `g_stockTable+0x42` and the count at `g_stockTable+106`
(= 0x42+40), matching `unk_B54490 + 64*0` for the k=0 match (gilde.exe 0x45e712).

### CollectStorageItems @0x45a62c — FIXED (1 bug) → now 1:1

**BUG (uninitialized-register artifact).** disasm 0x45a6d4 `xor ecx,ecx` precedes
the LookupCachedMarketPrice call; the new-FA-row stores then use ecx:
0x45a6ea `B54474←ecx`, 0x45a6f2 `B54478←ecx`, 0x45a6f0/0x45a70d `word_B5448C←(esi^ecx)`.
So FA reserved=0, stock=0, bits=0 — **identical to the FC block**, NOT `(int)price`.
The decompile's `v9` is simply the lost ecx==0. Reimpl had interpreted v9 as
`(int)price` for reserved/stock and `(u16)intPrice` for bits — wrong. Fixed to 0
(stock is then overwritten by FindItemStock, same as before). No golden depended on
the old FA price values (tests assert stock=FindItemStock, price, flags50=129,
freeCap; FC reserved=0 already correct), so no golden change needed.

Pass-3 (He handler) offsets verified 1:1: slot types at &outBuf[18]+v27, item-key
hi at &outBuf[17]+v27+2, handler item words at +206/+208/+210; second loop
+214(word)/+212(dword)/+220(count). Accumulate into B54474 (kST_reserved). ✓

### GatherRequiredItems @0x45c10c — VERIFIED-1:1

Full diff. Phase-1 (FA/FC mark required + bit2), null-QueryFind else-branch
(first-match increment), Phase-2 net shortfall `req-(reserved+stock)` clamp ≥0,
seller-select (last hasObj && deficit≥1 → srcBldg; clear required if no seller),
Phase-3/4 budget clamp `while(req>0 && budget<req*price+v39) --req; v39=(int)(…)`.
All stock-row strides (v29+=16 ⇒ 64-byte rows), list indices, and ConvertX
truncation match. `a2`=budget (ebx) confirmed. No `unk_B54490` use here. No change.

### ReserveWorkstationItems @0x45bd68 — VERIFIED-1:1 (1 signedness fix)

Full diff. v5 = st(v4)+2; flags50 at v5+48 (kST_flags50), bits at v5+60 (kST_bits),
type id at *v5. Food-slot set (452/453/454/449/450/451). WO columns via
B564A0/A4/A8/B0 = kWO_incoming/stock/freeCap/planned (88-byte rows). Same-owner
`required=freeCap`; cross-owner affordable-qty (halfMax, freeCap clamps, budget/price,
ConvertX, deficit min, ≥need). Recurse on produced input `wo(v9)+kWO_subKey`.
WO-bit flags (|2 at planned set, |8 at the def-word gate).
**FIX (signed compare):** seller filter `need <= deficit` — disasm 0x45bfcc
`cmp eax,deficit; jg` is SIGNED; reimpl used `(u32)>...`. Changed to
`(i32)(u32)need > deficit`. (Need-offset here is the slot index `i`, confirmed —
distinct from CheckWorkstationCapacity below.)

### CheckWorkstationCapacity @0x45ba84 — FIXED (2 bugs) → now 1:1

Full diff. v6 = st(v5)+2; need vs stock, freeCap, reserved gates; food check;
back-index −1 ⇒ seller scan, else recurse `wo(v10)+kWO_subKey`.

**BUG 1 (need-offset uses v12 not v20).** disasm 0x45bbb7 `add eax,[esp+var_38]`:
inside the seller loop the type-def need word is read at offset **v12** (the
per-seller index, 0,2,4,… = 2*k), NOT the outer slot index v20. (Hex-Rays renders
this explicitly as `v12` vs `v20` — they are separate stack slots.) Reimpl used
`38+v20` for all three seller-loop need reads. Added `const int v12 = 2*k;` and use
`38+v12`. This genuinely differs from ReserveWorkstationItems (which uses `i`).

**BUG 2 (signed compares).** disasm 0x45bace `cmp eax,[ebx+28h]; jle` (need>stock),
0x45bbc4 `jg` (need>deficit filter), 0x45bd3f `jg` (need>v25 final) are all SIGNED
(u16 need is zero-extended then compared signed against signed stock/deficit/qty).
Reimpl used `(u32)` unsigned compares — flips when the rhs is negative. Changed the
three to signed: `(i32)(u32)need > stock`, `(i32)(u32)need > deficit`,
`(i32)(u32)need <= v25`.

---

## Documented structural gap (rule 8 — NOT faked, called out)

The B56FA8/B56FAC and B56FC4/B56FC8 "scratch + list" globals **overlap** in the
original BSS (B56FAC = B56FA8+4; B56FC8 = B56FC4+4) and are indexed as WORD arrays
by a running byte offset; the engine writes the current key into `&dword_B56FA8 +
off + 2`, which aliases into the list itself, and CollectStorageItems' FC price
lookup even reads the *FA* scratch (`&dword_B56FA8 + v15 + 2`). The reimpl models
these as two clean `g_sceneTypeList{A,B}[]` arrays plus single scalars
`s_searchKey{A,B}`, which is byte-exact for the normal match/compare paths but does
NOT reproduce (a) the cross-list FC-price-reads-FA-scratch quirk, nor (b) the stale-
scratch value used in the null-QueryFind else-branch of EquipStaffWeapon/Gather.
Faithfully reproducing this needs restructuring the global layout (touches
ai_meister_core.cpp + ai_meister_equip.h + the bridge/tests). Left as-is and flagged
rather than half-implemented. Latent `(u32)>>16` vs signed `sar` on type-id hi-words
is also non-observable for the valid id range (≤ 32767) and left documented.

## Cross-file handoffs (not edited; for downstream agents)
- `ai_recon_brain.h`: BankmeisterNewRate/ReserveTier/FinalBalance — REUSED by bank;
  verified consistent with the binary, untouched.
- `entity.h`/`entity.cpp`: g_persons/g_personIds/g_objects — handle model
  (resolveHandle/makeObjHandle) is the documented 64-bit-safe substitute for the
  raw 32-bit record pointers the binary stores in the pointer columns.
- Calc{Farming,Wache,Diebe,Ambush}: live in sibling TUs; referenced by core's
  DispatchMeisterCalc.

## Counts
- Functions audited: 6 (5 equip + 1 bank) + core shim.
- VERIFIED-1:1 (no source change): bank, core, GatherRequiredItems.
- FIXED: EquipStaffWeapon (v4 array + signed key), CollectStorageItems (ecx==0
  reserved/stock/bits), ReserveWorkstationItems (1 signed cmp),
  CheckWorkstationCapacity (need-offset v12 + 3 signed cmps).
- Goldens changed: 1 (EquipStaffWeapon command test — v4 addressing).
- Tests: equip 51 checks / bank 70 checks — all green.
