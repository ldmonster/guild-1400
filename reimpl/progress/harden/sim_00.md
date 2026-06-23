# Wave-H1 1:1 Hardening — chunk sim_00 (SUMMARY)

Owner chunk: the 22 `.cpp` in `/tmp/guild_harden/chunks/sim_00.chunk` (sim AI/animal/building
cluster). Every provenance-tagged function was decompiled AND disassembled via IDA MCP
(module gilde.exe, imagebase 0x400000) and diffed line-for-line; constants/tables confirmed
with get_bytes; every float->int site checked for ConvertX-truncate vs fistp-round; RNG draw
count/order, byte strides, signed/unsigned compares, side-effect order and edx returns verified.

Per-file reports are the `sim_00_*.md` siblings of this file. NEVER committed; INDEX.md untouched.

## Build status
All 22 chunk `.cpp` compile clean (`g++ -std=c++17 -fsyntax-only`, rc=0). All chunk test
files compile. (Full `libguild.a` link is blocked by PRE-EXISTING breaks OUTSIDE this chunk:
src/sim/command_apply5.cpp `crt::` undeclared, src/gui/widget_layout.cpp `Widget::ld<>`,
src/sim/character_recon5_transport.cpp cast — all other waves' files, not touched.) Where a
test could be linked standalone it was RUN green (see per-file).

## Per-file verdicts

| File | Verdict | Reports |
|------|---------|---------|
| actionqueue.cpp | FIXED (2) +2 boundary | sim_00_actionqueue.md (orchestrator) |
| ai_recon5_decisions.cpp | VERIFIED-1:1 (6) | sim_00_decisions.md (orchestrator) |
| ai_meister_calc_angriff.cpp | FIXED (2) | sim_00_angriff.md |
| ai_meister_calc_diebe.cpp | FIXED | sim_00_diebe.md |
| ai_meister_calc_wache.cpp | FIXED (infinite-loop + RNG-route + __int16* offset) | sim_00_wache.md |
| ai_meister_calc_farming.cpp | FIXED (3) + dispatch wiring restored | sim_00_farming.md |
| ai_meister_subplanners.cpp | FIXED (16 across 6 fns, +3 golden) | sim_00_subplanners.md |
| ai_meister_passes.cpp | FIXED (2) | sim_00_passes.md |
| ai_meister_equip.cpp / _bank.cpp / _core.cpp | FIXED (5) / VERIFIED / VERIFIED | sim_00_equip_bank_core.md |
| aiaction_recon.cpp + dispatch + aimethod_recon3_registry + aimethod_score_ai_recon2 | FIXED (2: signed-level + prime-table truncation) | sim_00_aiaction.md |
| ai_needs.cpp | VERIFIED-1:1 (60-row table byte/VA/order-exact) | sim_00_needs_animals.md, sim_00_needs_queue_animals.md |
| animal.cpp / animal_wander.cpp / avatar.cpp | FIXED (sheep RNG burn, despawn u32->float, herdCount) | sim_00_animals_reconcile.md (orchestrator-final) |
| building2.cpp / building3.cpp / building4.cpp | FIXED (7) / 26 VERIFIED / 6 tables byte-exact | sim_00_buildings.md |

## Bug classes found & fixed (matching the brief's divergence taxonomy)
- **Hex-Rays pointer-typing byte-offset trap** (`__int16*`/`int*` so `*(p+N)` is byte 2N/4N):
  angriff (j+21->j+42), wache (FindStorableObject +1->+2 x3), subplanners (several),
  buildings (CollectByCityHandle filter +1, MatchTypeCode array+stride). 
- **RNG draw count/order desync**: animal_wander sheep d100 burn (unconditional vs cat-conditional;
  added empirically-passing golden, 64/64 checks), subplanners FlagIdleStaff draw, farming.
- **Float->int truncation**: confirmed ConvertX@0x5c6b08 + fistp = truncate at every site
  (PlaceBet, farming Phase8/9, animal despawn fild{u32,0}); all match `static_cast`/u32->float.
- **Table truncation / wrong literal**: aiaction prime array (4-entry wrong -> 12-entry via
  get_bytes), buildings 6 tables byte-confirmed, LookupTypeName NUL-copy quirk.
- **Signed/unsigned & sign-extension**: equip (multiple sar/jle/jg), buildings SyncProfessionState
  sign-extend, aiaction guildhall level byte, wache.
- **Control-flow routing**: wache merchant-branch infinite loop (DISPATCH_456367), angriff
  attack-vs-spy, diebe.
- **Rule-8 fakes/stubs reconstructed**: farming DispatchOrders+AssignIdleWorkers wiring restored;
  passes fabricated-emit replaced with real Amt free-slot gating.
- **DispatchCurrent owner-gate** (+0x14 not +0x08 ready), FinishSetVisible callCount-gate
  (cross-confirmed by two independent agents).

## Orchestration note
Work fanned out to 1 agent per file/cluster (Rule 9). One conflict resolved by the
orchestrator: two agents disagreed on the animal sheep-burn + despawn-conversion; the later
agent's reverts were WRONG per re-decompilation — restored to the binary and pinned with a
passing regression golden (sim_00_animals_reconcile.md).
