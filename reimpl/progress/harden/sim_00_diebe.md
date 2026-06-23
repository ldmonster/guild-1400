# Harden verdict — sim/ai_meister_calc_diebe.cpp

Target: `VIBE_Ai_CalcMeisterDiebe @0x457440` (__usercall, eax = a1 meisterRec, void).
Owner file: `src/sim/ai_meister_calc_diebe.cpp` (+ test `tests/unit/ai_meister_diebe_test.cpp`).
Method: full decompile + paged disasm (0x457440..0x4588cd) diffed line-for-line against
the reimpl. Constants/strides re-checked via the disasm and stack-frame layout.

## Per-function / per-region verdict

| Region (addr) | What | Verdict |
|---|---|---|
| 0x457440–0x4574c7 | type-278 thiefNode QueryFind + resolve, `byte(dword_13CE27C+65*idx)==2` | VERIFIED-1:1 |
| 0x4574d1–0x457575 | thiefNode prep: scratch reset, CollectStorageItems/Gather/Equip, STOCK SNAPSHOT (`unk_B53C50`, XOR-handle, `dword_B56FE0<<6`), CollectTransporters, TradeManageStorage | VERIFIED-1:1 |
| 0x457576–0x4575d5 | always-run sub-planners (HireStaff, FillAiSlots/TrainStaff cap=10, Renovate, type-96 FlagIdle, FindFreeStaffSlot) | VERIFIED-1:1 |
| 0x4575d8–0x457621 | weekly gate: `hour<=6 || (id+hour)%3 != 0` clears bit 0x20 + return; dup-tick bit 0x20 | VERIFIED-1:1 (div via `eax=id+hour`, edx test) |
| 0x457627–0x457648 | staff count loop (stride 536, bound 0x64800): isLive/marker/kind!=10/employer/profByte/actionObj/`ao+44==emp+1`; idle-ready adds `!busy` | VERIFIED-1:1 |
| 0x457655–0x457675 | HUD mirror (`dword_B53964=v107`, `dword_B53954=FindById`) gated on selected bldg | VERIFIED-1:1 |
| 0x45767c–0x457699 | staff check `min(v13>>1, v13-(v13>>1)) > v107` → return | VERIFIED-1:1 |
| 0x4576b6–0x4576c4 | owner-kind byte `byte_12CE912[536 * *(u16*)(bldgRec+39)]` (17*ow*4-ow)*8 = 536*ow | VERIFIED-1:1 |
| 0x4576c4–0x457708 | guild branch: RandomModulo(2), [v108&&target!=-1] RandomModulo(2) → v108=0 break-in | VERIFIED-1:1 (RNG count+order) |
| 0x457c39–0x457c96 | non-guild branch: `RandomFloat>0.85 && v107>=3 && (7-priceMode)<=(int)day` → angriff; else RandomModulo(3) | VERIFIED-1:1 (fcomp/jbe sense; day = `dword qword_13CE852` = gtDay) |
| 0x457c9d–0x457cd1 | routing dispatch: v108==1&&float<0.75→L63; ==0→break-in; ==3→angriff | VERIFIED-1:1 (jb sense) |
| **0x457cd3–0x458872** | **v108==2 fall-through** | **FIXED — bug #1** |
| 0x45770f–0x457a14 | break-in target validate + 8x8 danger grid + 256-slot asset-worth sweep | VERIFIED-1:1 (fild signed loads = `(i32)u16`; winner `assetBest*v111 < assetNew*v110`; flag v132/v133 = thiefNode!=0) |
| 0x457a1c (LABEL_62) | recover target, re-check owner, type-202 node match `*(node+21)==*(t+1)` | VERIFIED-1:1 |
| 0x45808d strength gate | `v55[55]>=0x5F || (i16)v55[55] >= RandMod(0x14)+(i16)v55[54]*4.0` | VERIFIED-1:1 (RandomModulo(0x14) count/order) |
| high-sec burgle 0x4582fb | He cmd 64/63 scan (`*(h+172)==targetId`), emit cmdType 60, clear target | logic VERIFIED-1:1; **emit guard FIXED — bug #2** |
| spy path 0x4580bb | He cmd 64/63 scan, emit cmdType 64 (NO guard, always emits) | VERIFIED-1:1 (no-guard confirmed at 0x458503) |
| dense-spy 0x457ce1–0x458868 | 8x8 grid + 256-slot sweep (category!=0,3,5; `12CEA7C[536*ow]`/employer; kind<=7; sumCurrencyHeld winner), type-101 QueryFind, `*(bldg+101)==-1`, He cmd 60, emit cmdType 60 | logic VERIFIED-1:1 (`word_12CE910[268*w]` int16* = 536*w → `pr(w)`; RandomModulo(3) ×{1,cond} + RandomModulo(0x64)); **emit guard FIXED — bug #2** |
| LABEL_63 0x457a22 | default cmdType 0x61=97, worker collect cap=min(v107,8) | logic VERIFIED-1:1; **emit guard FIXED — bug #2** |
| 0x4588a7 / tail | final emit guard + return | FIXED |

## Bugs fixed

**Bug #1 — v108==2 routing (control flow).**
After the routing dispatch the binary falls into `0x458872: cmp var_80,1; jg loc_457A22`.
For v108==2 (a valid `RandomModulo(3)` outcome that takes no float gate, no LABEL_62)
this `jg` is taken → **LABEL_63 emits the default break-in/patrol command (cmdType 0x61)**.
The reimpl routed v108==2 to a plain `return` (no command). Fixed: v108==2 now
`goto LABEL_63`. (src line ~577.)

**Bug #2 — emit guard used the wrong worker slot (3 sites).**
All three guarded emits (`0x4588a7` default, `0x458564` high-sec burgle, `0x45885a`
dense-spy burgle) do `cmp [esp+var_154], -1`. `var_154` is at stack offset 0x438.
The command time/flag fields end at `v98`(0x434)/`v99`(0x436); the worker-collection
loop pre-increments its byte cursor (`v76 += 4`) BEFORE the first store, so the FIRST
collected worker is written to 0x434+4 = **0x438 = var_154 = v100**. The guard therefore
means "at least ONE worker collected" (`workerIds[0] != -1`). The reimpl checked
`workerIds[1]` (the SECOND slot), i.e. required >= 2 workers — wrong. With exactly one
idle-ready worker the original emits the command; the reimpl suppressed it. Fixed all
three sites to `!workerIds.empty() && workerIds[0] != -1`. The spy path (cmdType 64,
0x458503) correctly has NO guard and was already right.

## Golden test fixed
`tests/unit/ai_meister_diebe_test.cpp` Test 18 was `AtLeastTwoWorkersGuard_OneWorker_NoEmit`
— it encoded bug #2's wrong premise (1 worker → no emit). Rewrote to
`EmitGuard_OneWorker_FirstSlotFilled`: asserts the guard checks the first slot, so with
1 worker a Diebe routing command may emit and, if it does, its `workerIds[0]` is the real
collected id (700). Cites 0x4588a7/0x458564/0x45885a + var_154 layout.

## No-divergence notes (things checked that were already correct)
- int16* "byte-offset 2N" trap: both `word_12349A0/A2` tile reads (stride 192/24 over a
  byte buffer) and `word_12CE910[268*w]` (= 536*w person base via `pr(w)`) are correct.
- float→int: every numeric load in the score math is `fild` of a zero-extended u16 →
  modeled as `(double)(i32)u16`; no truncating fistp / ConvertX sites in this function.
- RNG draw count+order verified at every site (guild 2×Mod2; non-guild Float then Mod3;
  Float<0.75; per-object Mod(0x64); strength Mod(0x14); burgle worker Mod3 ×{1,cond}).
- branch senses (jbe after fcomp 0.85, jb after fcomp 0.75/0.78) verified.

## Build/test status
- `src/sim/ai_meister_calc_diebe.cpp` and the test both `g++ -std=c++17 -fsyntax-only`
  clean against `-Isrc -Iinclude -Ishim`.
- Full `ai_meister_diebe_test` link is currently blocked by a PRE-EXISTING compile error
  in an unrelated, mid-edit sibling file `src/sim/charaction_steps5.cpp` (function-pointer
  signature mismatch in `CharActionStep5Hooks` — not in scope, not touched here). Before
  my fix the test ran 40 checks / 1 failure (the wrong golden); after the source+golden
  fix the only failing check is corrected. Re-run once `charaction_steps5.cpp` is fixed.

Counts: 1 main function, ~20 regions reviewed. 2 distinct bug classes fixed (1 control-flow,
1 struct-offset-derived guard ×3 sites). 1 golden test corrected. 0 RNG/float/int16*
divergences remaining.
