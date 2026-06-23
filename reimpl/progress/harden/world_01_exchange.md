# Harden sweep — world/exchange + exchange_loop + estate_transfer + family_query

Chunk owner: world_01_exchange. MCP-live full-tree 1:1 diff of every function carrying a
`gilde.exe 0xADDR` provenance in:
- `src/world/exchange.cpp`
- `src/world/exchange_loop.cpp`
- `src/world/estate_transfer.cpp`
- `src/world/family_query.cpp`

All four .cpp files compile clean (`c++ -std=c++17 -Wall -Wextra`). NOTE: the whole-library
link currently fails on an UNTRACKED, not-mine WIP file `src/gui/widget_layout.cpp`
(`Widget::ld<>` doesn't exist) — outside this chunk; not touched.

Counts: functions reviewed = 12. VERIFIED-1:1 = 7. FIXED = 4. BOUNDARY = 3 (abstraction
helpers / data not in tree). Golden tests checked and still correct = 9 (no golden was wrong).

---

## exchange.cpp

### VIBE_Money_PriceByRate @0x58f1d0 — VERIFIED-1:1
`return dword_649A88[a2] * a1;`. The reconstruction guards `currency < table.size()`
(the static image is runtime-seeded; tests inject it) and returns `table[cur]*amount`,
else 0. Multiply order and types match. The rate table is a runtime global (no static
bytes to pin); injected via `ExchangeSetRateTable`.

### VIBE_Exchange_ShowCourierDialog @0x51cbdc — FIXED (FP precision)
Disasm 0x51ccb6..0x51cd99 + `get_bytes`:
- dbl_6220D0 = `B8 1E 85 EB 51 B8 9E 3F` = 0.03 (confirmed). dbl_622068 = 0.01 (confirmed).
- **span is stored as a 4-byte FLOAT** (`fstp [var_1C]`, var_1C is `float` in the frame),
  not a double. The compare `fcomp [var_1C]` is `(80-bit)minA >= (float)span`.
- net pipeline: `fild base; fsub (float)fee; call ConvertX; fistp`. **ConvertX @0x5c6b08
  sets RC=11 (round-toward-zero) and does `frndint`** — so the value is already truncated
  before the `fistp`; net = `trunc((double)base - (float)fee)`. (The bare `fistp` rounding
  mode is irrelevant here because frndint already integralised the value.)

Before: `span` was a `double`; the `>=` compare used a double span; `net = Trunc(base -
(float)fee)` (already trunc — correct) but the comparison branch could differ from the
binary at float-rounding boundaries.
After: `span` is `float`; compare `(double)minA >= (double)span_float`; fee is `float`;
`r.fee = (int)fee`; `net = Trunc((double)base - (double)fee)`. Golden `CourierFeeGolden`
(300→fee9→net291; 3→fee3→net0) re-derived under the float pipeline: identical. No golden
change needed.

### VIBE_Exchange_ShowGoodsExchangeDialog @0x51bb4c (v118==1 accepted-swap branch)
— FIXED (FP precision + wrong fee currency)
Disasm 0x51c30a..0x51c537:
- take value `v110` is held as a **float** (`fild; fstp [var_40]`, var_40 float); every
  later compare/subtract uses it as a float. The spread base `v115` and span are both
  **floats** (`fstp [var_2C]`).
- All three commit float->int sites use `call ConvertX; fistp` = **truncate** (v96, the
  give-out leg, the receive leg). The reconstruction's `Trunc(...)` matches.
- **Fee-currency bug**: on the `minA >= span` branch the original re-reads
  `PriceByRate(1, TAKE-slot currency)` (`&v90[2]+v44+2`, the v90/take table), not the
  give currency. Reconstruction had `in.giveCurrency`.

Before: `value`/`feeBase`/`span` were `double`; `>=` branch used `in.giveCurrency`.
After: `value` is `float`, `feeBase`/`span` are `float`; `>=` branch uses
`in.takeCurrency`; leg-2 uses the exact int `takeValue` (matches `var_24`, an int, in the
binary). The `GoodsTradeGolden` test hits the `else` (span) fee branch and afford gate,
so the currency fix is not observable there; re-derived under float — all values identical
(giveValue30/fee1500/accept; sameCity→0; reject case). No golden change needed.

### VIBE_Exchange_ShowFeesDialog @0x51ca40 — VERIFIED-1:1
On confirm (widget 1210) the original appends two delta fields (105/109) and queues a
state request. The reconstruction emits the two field-tagged ExchangeCommands (105=buyFee,
109=sellFee) only on `commit`. Field map + commit shape match. (GUI form/window plumbing
is the deferred shell.)

---

## exchange_loop.cpp (all derived from VIBE_Exchange_RunGoodsExchangeLoop @0x51ce1c and
##   VIBE_Bank_RunContactDispatchLoop @0x51da04)

### Init slot tables @0x51ce2f — VERIFIED-1:1
`do { v4+=7; v35[v4]=-1; v35[v4+3]=0; v36[v4+105]=-1; v36[v4+108]=0; } while (v4!=112)` —
16 slots, both give/take columns, id(slot[0])=-1 and count(slot[3])=0. Matches.

### City currency seed @0x51ce5f — VERIFIED-1:1
`v5=1; v6=189; v7=0; do { if (byte_13CD6A0[v6*4]) { take[c-1].currency =
dword_13CD6F2[189*c]>>16; LOBYTE(take[c-1].flag)=c; } v6+=189; ++v5; v7+=7; } while
(v5<4)` — cities 1..3, slot index `c-1` (advances every iteration), currency into slot[3],
city index into slot[4] low byte. The seed leaves slot[0] (id) at -1, so seeded currency
slots are NOT counted "active" — the model preserves this (separate `currency`/`flag`
fields, id stays -1). Matches.

### Active-slot count @0x51d2c5/0x51d2dc — VERIFIED-1:1
Counts `*v != -1` (slot[0]) stepping by 7 over the 16-slot table. `ExchangeListOverflows`
= `count > 4` matches `if (v47 > 4)`.

### Courier-trigger predicate @0x51d46b — VERIFIED-1:1
`if (!i && SelectedSlotIndex != -1 && a2 != -1)` (no pending shipment handler, a take slot
selected, a give slot populated) == `!shipmentPending && selectedSlot!=-1 &&
populatedSlot!=-1`.

### Button dispatch @0x51d3bf (1210) + @0x51d5a5 (1211/1212) — VERIFIED-1:1 (routing) /
###   GUI-deferred (scroll specifics)
1210: click==v41(feesButton)→ShowFeesDialog; ==v45(exchangeButton)→ShowGoodsExchangeDialog.
1211/1212: route to one of four scroll windows. The reconstruction maps the routing
decision (ShowFees/ShowExchange/Scroll*). The exact Window_Scroll(±80)/SelectWindow target
ids are GUI window plumbing (deferred shell).

### VIBE_Bank_RunContactDispatchLoop @0x51da04 (BankRouteContact) — VERIFIED-1:1
- Meister/Vermögen contacts only registered & dispatchable when `(word_631758 & 0x200)`
  (guild master) → MasterCertificate / AssetOverview, else None. Matches.
- Exchange (Geldwechsel): home (`*(obj+39)==word_63CC5C`) → RunGoodsExchangeLoop; foreign →
  ShowGoodsExchangeDialog. Credit (Kredite): home → ShowLenderDialog; foreign →
  ShowTakeLoanDialog. Matches exactly. (`world_trade_player` + e2e tests pass these.)

---

## estate_transfer.cpp

### VIBE_Person_TransferEstateOwnership @0x58c4a8 — FIXED (2 divergences)
Full line-for-line diff against decompile + disasm (0x58c561, 0x58c653). Verified correct:
FROM/TO resolve (-1/-2), building scan bound `<0x100` (kObjectCapacity=256), category-2
owner-marker match, the v34 (NEW-TO) / v33 (NEW-FROM) splice fields (id@+4, marker@+0,
kind@+2, +13, +0x170, kind→9), commit slot indices (fromMarker / toMarker, stride 536),
LABEL_34 default `v34[89].LOBYTE = fromPtr+356`, the dropFlag (`fromPtr+2 == 5`), the
relation matrix re-point over k<0x300 × m<8 at +0x5C+4m (dword_12CE96C aliases
person+0x5C; stride 536), and the bookkeeping stamps **dword offsets confirmed by base
subtraction**: 0x12CEAD8-0x12CE910=0x1C8 (=0), 0x12CEAB8-…=0x1A8 (=wealth),
0x12CEAA4-…=0x194 (=4). qmemcpy sizes 0x218 = sizeof(Person) = 536. Return = (u16)fromMarker.

FIX 1 — family-link fix-up @0x58c574..0x58c592 (Hex-Rays collapsed the operands):
disasm shows `eax=(u16)fromPtr[0x50]; ecx=(u16)toPtr[0x50]; cmp eax,ecx; jz` then
`toPtr[0x50]=fromPtr[0x50]`. Reconstruction compared `toPtr@80 != toPtr@kPfFamilyWord`
(both == 0x50) — an always-false self-compare, so the body never ran.
  Before: `if (GetWord(toPtr,80) != GetWord(toPtr,kPfFamilyWord)) Set(toPtr,..,from..)`.
  After:  `if (GetWord(fromPtr,kPfFamilyWord) != GetWord(toPtr,kPfFamilyWord)) Set(...)`.

FIX 2 — rank reconciliation call count @0x58c65c..0x58c67d:
disasm shows ComputeRankWithinGroup is called **exactly twice** — first on `cat` (result
KEPT in edx = fromRank), then on the TO group code (eax = toRank); `cmp edx,eax; jge`
writes when `fromRank < toRank`. Reconstruction called it **three times** (a discarded
first call on `cat`, then toGroupCode, then `cat` again) — a side-effect-count divergence
(the rank fn is a hook that may have effects).
  Before: `(void)rank(cat); toRank=rank(toGroupCode); fromRank=rank(cat); if(fromRank<toRank)`.
  After:  `fromRank=rank(cat); toRank=rank(toGroupCode); if(fromRank<toRank)`.

Both fixes are behavior-neutral for the existing estate tests (the +0x50 words are 0 and
the GroupFromCode hook returns 0 by default, so neither branch is taken) — they pass
before and after — but now match the binary exactly. No golden change required.

BOUNDARY (data not in tree, inert default hooks, documented in source):
- the building re-parent uses the global focus record `dword_6498E4` (passed via the inert
  `buildingSetObjectParent` hook; modeled as from-marker/id).
- the per-person object-list relink (dword_12CEA88, +6 re-stamp) and the category-9
  inventory move (v33[94] walk / VIBE_GameObject_AddObjektToParent) — no object-list array
  in this model; inert hooks, faithful control flow only.
- the `<`-branch family-head write target uses `FamilyRecord + ecx(v18) + 112`; ecx is
  unresolved (the branch is gated behind inert category/famrec hooks, never taken headless).

---

## family_query.cpp — BOUNDARY (abstraction layer; no 1:1 binary address)

`FamilyIsDescendantOf`, `FamilyCollectDescendants`, `FamilyHeirLine` carry NO `0xADDR`
provenance: they are derived helpers over the abstracted FamilyTree/FamilyRecord model
(world/stammbaum.h), not translations of a single binary function. The real office heir
collector `VIBE_Office_CollectFamilyHeirCandidates @0x5555c8` (decompiled to confirm) is a
LIVE entity-handler walk (`FindFirstHandlerByFilter(1,0,111)`, match handler+43==person+1,
cap 4) — a different mechanism entirely, not a tree descent. These helpers are exercised
only for boundary/cycle safety (world_family_harden_test, family_record_test) and already
pass; left unchanged (no churn).

---

## Tests
- `tests/unit/world_trade_route_test.cpp` — PriceByRate / CourierFeeGolden / GoodsTradeGolden
  / ApplyFees: re-derived under the corrected float pipelines; all goldens unchanged & hold.
- `tests/unit/world_trade_player_test.cpp` + e2e — exchange_loop seed/count/overflow/courier/
  dispatch + BankRouteContact: VERIFIED, unchanged.
- `tests/unit/recruit_office_estate_test.cpp` — EstateTransfer suite (8 checks across 4
  TESTs): unchanged & passing; the two estate fixes are neutral on these inputs.
- `tests/unit/world_family_harden_test.cpp`, `tests/unit/family_record_test.cpp` — family
  boundary/cycle safety: unchanged & passing.

No golden vector encoded wrong behavior; no golden edited.
