# Harden sweep — src/world/event5.cpp (MCP live, gilde.exe @0x400000)

Scope: the four giant deferred world-event "He" coroutines. Every `gilde.exe 0xADDR`
function decompiled and diffed line-for-line against the binary; floats/tables verified
by bytes; offsets/strides verified by disasm.

## Function inventory
| Addr | Symbol | reimpl | Verdict |
|---|---|---|---|
| 0x4f5b5c | VIBE_Event_RunGebaeudeBauen | RunGebaeudeBauen | FIXED (3) + BOUNDARY |
| 0x4f2bd0 | VIBE_Event_RunProduktion | RunProduktion | FIXED (1) + BOUNDARY |
| 0x4f0708 | VIBE_Event_DiscoveryRaidRun | DiscoveryRaidRun | FIXED (comment) + VERIFIED |
| 0x4f4348 | VIBE_Event_SlotProcessRun | SlotProcessRun | FIXED (6) + BOUNDARY |
| helper | ReArmEntity29 | — | FIXED (extra side-effect) |

## Constants — ALL VERIFIED 1:1 (get_bytes @ .rdata)
- dbl_6202F8=0.01, dbl_620300=1/252 (0.003968253968253968), dbl_620308=0.25,
  dbl_620310=0.5, dbl_620318=-0.5, dbl_620320=0.1, flt_620328=1.2999999523162842f
- dbl_6203E8=0.9, dbl_6203F0=0.8, dbl_6203E0=1.1
- dbl_61FFE0=0.35, dbl_61FFE8=0.1, dbl_6204C0=1.8
- byte_6477A1 (currency global) = 0  (reimpl passes literal 0 — matches static init)

## float->int — VERIFIED
VIBE_Coord_ConvertX @0x5c6b08 disasm: sets FPU RC=11 (round-toward-zero, byte 0x1F to
ctrlword), `frndint`, restores. So every `ConvertX(); (int)x` site = truncate toward
zero. reimpl `ConvertX(double)=static_cast<i32>(v)` matches all sites (raid loot value /
stock qty / loot accumulate; slot in/rest/raw unit prices). VERIFIED 1:1.

## FIXED — RunGebaeudeBauen (0x4f5b5c)
1. **case 6 per-step divisor offset** — was `RecI32(obj, 579*4)` (=obj+2316). Disasm
   @0x4f5f84: `mov eax,[edx+243h]` (edx==obj) then `*4`, then `*256 - *4` == `*1020`,
   then `xor edx,edx; div ecx` (unsigned). The dword is at **byte 0x243 (579)**, not
   2316. Fixed to `RecI32(obj, 0x243)`; unsigned div preserved.
2. **model-handle offset 388 -> 97** — disasm shows the build object's model handle is
   at byte **+97 (0x61)** (`mov ebx,[eax+61h]`, `*(_DWORD*)(v99+97)`), NOT +388. Fixed
   the 4 RunGebaeudeBauen sites (BuildModelName, FindByHandle door, ApplyTransform,
   HideScaffold/Inflate/RebuildOctree). NOTE: DiscoveryRaid case 3 keeps **388** — there
   the decompile is `*((_DWORD*)v12 + 97)` = dword-index 97 = byte 388 (different type).
3. **ReArmEntity29 extra increment** (helper) — the old helper did `++Dword(h,112)` on the
   non-negative path. The binary tails (LABEL_17/23, cases 2/3/4/5) are *only*
   `(+132)=QueueRequestEntity29(state, a1)`; they pass `+112+1` as the packet's desired
   next-state arg and do NOT mutate +112. Removed the increment. Golden test added
   (Phase2ReArmDoesNotIncrementState).

case 9: changed the per-step math to read `*(a1+192)` directly for div/mod/advance (no
/0 guard) matching disasm (`v50/v52 = *(a1+192)`). Comments corrected.

## FIXED — RunProduktion (0x4f2bd0)
4. **favorability member loop 4 -> 8** — disasm: `a3=a1; v74=a1+32; do{...*(a3+140)...; a3+=4}
   while(a3!=v74)` = 8 iterations over +140..+168, and AverageObjectFavorability is
   called with count **8** over &+140. reimpl looped only 4. Fixed to 8.

Verified 1:1: rate = wsCount*0.01+1; favTerm = (i16)(u8 mood)*（1/252)*0.25 for active
class 6/7; slot-match *0.5; cmdState=(*(b+65)-2)*0.1; final *Flt(212); tired byte>=0x3C;
`*(a1+208)+=v31` byte-wraps; hi = `*(a1+170) sar 16`.

## FIXED — DiscoveryRaidRun (0x4f0708)  (comments only; code already 1:1)
- case 3 "done" advance `GameTime_Advance(+82,0,1,0)` = arg ecx adds to SECONDS => +1
  **second** (comment said "+1 hour"; the call & faithful gametime impl already do +1s).
- RNG draws verified: case 4 `RandomModulo(3)` per member (draw+discard, correct order
  ChangeAction/Single49/Named53/Rand(3)/Flag55); case 6 `RandomModulo(0xC8)` payout then
  per-member `RandomModulo(0x64)`; case 7 per-good `RandomModulo(0x64) > 0x32`. All match
  disasm count+order. Loot float->int (ConvertX truncate) all 1:1.

## FIXED — SlotProcessRun (0x4f4348)
5. **unsigned >>2** — `cap = (item[7]*slotCap) >> 2` is logical SHR in the binary
   (`(unsigned int)(...) >> 2`); reimpl used signed i32 `>>` (sar). Cast to u32 first.
6. **QueueRequest17 record offsets (×2 error)** — the decompile uses `__int16*`
   arithmetic; `take/proc/slotRec/it +1/+1/+7` are BYTES 2/2/14, not 4/4/28. Disasm
   confirms (`mov edx,[edi+2]`, `mov eax,[ecx+2]`, `mov ebx,[edi+14h]`). Fixed all three
   QueueRequest17 sites (no-work take/iter, input scan take+rest, output scan sell).
   source-id stays **+1** (disasm `cmp edx,[eax+1]`); proc QueryFind stays **+20**.
7. **final compare source vs dest** — binary `v68=*(source+1); if (v68==*(a1+176)) +5
   else reset+10`. reimpl compared `dest+1`. Fixed to `source+1`.
8. **owner-chain gate** — binary `if(!*(a1+241) && !FindOwnerChain(src,dst)){FindOwnerChain;
   free}`. reimpl ignored the probe return (hook was `void`). Changed the hook to return
   i32, replicated the double-call + `&& !chain` branch. Inert default returns 0.
   GameTime carry advance comment corrected (`+82,1,0,30` = +1 hour +30 min).

Golden test added (NoWorkTakeUsesByteOffsets) pinning the +2/+2/+14 offsets and +5 idle.

## BOUNDARY (Rule 8 — data/subsystem not in the live tree; routed through hooks)
- **Building/object TYPE table** `dword_13CE294 + 589*type` (and `dword_13CE27C` recipe
  rows, `byte_B5FB61` packet table, `word_12CE910/dword_12CE914/dword_12CEA80` action
  tables): not in tree. RunProduktion abstracts the Rohstoff message-prefix selection
  via RohstoffVariant + render/send hooks; SlotProcessRun's `srcCls` read of
  `589*type` (missing the dword_13CE294 base) and the wage-owner index into word_12CE910
  are abstracted/partial. The recipe drain loop (`*(v53+34)`, table-driven) and the
  case-1 packet-resolved v81 message classification (`byte_B5FB61[10*pkt]`) cannot be
  faithfully run without those tables — kept as the inert-routed structure; only
  observable side effects guaranteed correct (counter/packet/advance writes).
- **RunGebaeudeBauen case 6 SpawnBuildEffectByName @0x4f58d4 + case 7/9 nearest-door scan
  (dword_13CE298 entity table) + case 9 finish tail** (RestoreObjectStates, season
  global byte_634484, RefreshFlagAnimation, neighbour-notify loop, BeginDelta/AppendDelta
  /State22, Audio voice, dword_12CEA80 leader-table update): scene-graph / global-table
  dependent. Collapsed to hook calls; the documented observable phase/field writes
  (+192/+196/+200/+82, free) are 1:1. SpawnBuildEffect's failure->LABEL_37 branch is the
  one dropped control edge (no hook); under inert hooks the binary's effect is "proceed".

## Tests
tests/unit/event5_test.cpp — 16 TESTs / 48 CHECKs, all pass (built standalone:
event5.cpp+gametime+npcaction+math_random+rand). Added 2 golden tests for the
ReArmEntity29 non-increment and the SlotProcessRun byte offsets. (Full CMake build
blocked by an unrelated untracked WIP file src/gui/widget_layout.cpp — not in scope;
event5.cpp + event5.h + the test compile clean with -fsyntax-only and link/run standalone.)
