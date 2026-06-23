# Wave-H1 1:1 Hardening — chunk `sim_03` (master report)

Module: `gilde.exe`, imagebase `0x400000`. MCP (IDA Pro + Hex-Rays) live throughout.

Chunk = 22 `.cpp` files in `src/sim/` (charaction_steps3-8, charaction_walk, cheat_recon,
combat + 13 combat_* files). Every provenance'd function was decompiled and diffed
line-for-line against the binary per the wave brief (control flow, get_bytes-confirmed
constants/tables, ConvertX-truncate vs fistp-round at every float→int site, fixed-point
sign, RNG draw count/order, struct offsets/stride, side-effect order, return incl. edx;
disasm is the reference of record where Hex-Rays collapses `__usercall` args).

Work was fanned out to 11 sub-agents (one per file cluster); per-cluster detail lives in the
sibling `sim_03_*.md` reports. This file consolidates results, the build/test verification,
and an incident note.

## Aggregate counts

| Cluster (report) | VERIFIED-1:1 | FIXED | BOUNDARY |
|---|---|---|---|
| charaction_steps3 (`sim_03_charaction_steps3.md`) | 11 | 0 | tables/text pipeline |
| charaction_steps4 + walk (`sim_03_charaction_steps4_walk.md`) | 10 | 5 (3 goldens) | render/mesh/tables |
| charaction_steps5 (`sim_03_charaction_steps5.md`) | 6 | 4 | query-hook collapse |
| charaction_steps6 (`sim_03_charaction_steps6.md`) | 1 (+table CF) | 8 (5 goldens) | 3 |
| charaction_steps7 (`sim_03_charaction_steps7.md`) | 2 | 6 (goldens) | 6 |
| charaction_steps8 (`sim_03_charaction_steps8.md`) | 4 | 8 (1 golden) | 1 cluster |
| cheat_recon (`sim_03_cheat_recon.md`) | 14–22 | 0 | 11 (queued effects) |
| combat core (`sim_03_combat_core.md`) | 17 | 11 | 3 |
| combat orders/loop/escape (`sim_03_combat_orders.md`) | 33 | 5 | 2 no-addr |
| combat packets/projectile/balance (`sim_03_combat_packets.md`) | 11 | 1 (1 golden) | 0 |
| combat slots2/3/4 (`sim_03_combat_slots.md`) | 22 | 2 | 2 |

Plus 1 additional golden fixed during master verification (steps4 GossipBroadcast, below)
and 1 cross-chunk fix recovered in `charaction_steps2` (RestorePosFinish return value).

## Highest-impact divergences fixed (binary is truth)

- **Disasm-beats-Hex-Rays constant.** `InitDefaultParameters` @0x48b744: Hex-Rays shows an
  uninitialized `ecx`; disasm has `mov ecx, 42A00000h` (=80.0) preserved across a thunk →
  `dword_B59C70 = 80.0`, not 0. Source cell + golden vector corrected. (combat packets)
- **`.rdata` float tables were placeholders.** steps7 (all 10 `.h` constants, e.g.
  kCartBonus310 0.5→**-0.4**, kAmbushBase 1.0→**100.0**) and steps8 (all 11, e.g.
  kEscortSpeedFac 0.5→**10.0**, kPickCeiling 100→**252**, kArrestWorthBias 1.0→**256**) —
  each value re-confirmed with `get_bytes`.
- **float-precision / ConvertX truncation.** combat `RandFloat`/`RandomFloatScaled` scale =
  float(1/32767)=3.0518509e-05 (not 2^-15, not double); `AccumulateThreatStats` spills the
  tier through a 32-bit float before ConvertX (was truncating a double); `SpawnDamageNumber`
  divisor is double 0.01 not 0.01f; `AutoResolveBattle`/`SumStrength` truncate per-step not
  once; `ComputeOrderSlot`/`DrivePursuit` truncate the **double** product directly.
- **RNG draw count/order (stream sync).** `DuelArmCombatant` missing an unconditional
  `RandomModulo(8)` draw; `DecideAiDeployment` `RandomModulo(5)` must short-circuit when
  count==0; `SelectBeatingTarget` counter increments only on gate-pass (wrong loop length +
  draw count); `EvaluateAttack` weaponClass==2 short-circuits without consuming RandomModulo(255).
- **struct offset / side-effect order.** `RunBuyObject` hiword read off +170 signed `>>16`
  (was +172, `>>16 & 0xFFFF`) and missing the purchase-finalize emit; `RunFollowTarget` clock
  stamp emitted after (not before) ChangePlayerAction, Advance edx=6h; systematic person/
  object **id-at-+1** (not +4) across the steps7 transporter cluster; `RunArrest` field
  selector +89 signed `>>24` (was +169); `He_SubMethodByte` signed `>>24`.
- **control flow.** `PickScenario` two branch bugs; `RegisterHandlerTable` ~25 corrupted
  init/step addresses (whole 0x5E–0x87 block) re-derived from disasm; `RunPruegel` jump-table
  state values + missing state-7 case; `RunSpionage` fabricated stride table replaced with the
  real coprime-stride `dword_478450`; `ResolveMeleeHit` inverted survive/kill banner gate.

## Master-pass golden fix (during verification)

- **`tests/unit/charaction_steps4_test.cpp` GossipBroadcast** (`GossipSendsRumor`): golden
  asserted bribe==20 (assuming exact 0.01 → trunc(1000*0.01)=10, *2). The binary uses
  `flt_61EAD4` @0x61EAD4 = `0A D7 23 3C` = float(0.01) = 0.009999999776482582, so
  1000*const = 9.99999977…, ConvertX truncates toward zero → 9, bribe = 9*2 = **18**. The
  steps4 source was already correct (used the full-precision constant); only the golden was
  wrong. Fixed 20→18 with byte evidence in the comment.

## Build / test verification (full chunk)

All 20 chunk `.cpp` compile clean with the project flags
(`-std=c++17 -Wall -Wextra`). All 23 chunk test files compile and **pass**:

- unit: steps3 118 / steps4 86 / steps5 68 / steps6 74 / steps7 53 / steps8 78 /
  cheat 215 / sim_combat 293 / combat_battle 99 / combat_loop 71 / combat_orders 95 /
  combat_escape 34 / combat_packets 105 / combat_drivers 321 / combat_recon_balance 317 /
  combat_slots 43 / combat_slots2 50 / combat_slots5 81 — all 0 failures.
- e2e: steps3 21 / steps5 21 / steps7 28 / steps8 20 / sim_combat 54 — 0 failures.
- integration: steps7 12 / steps8 9 — 0 failures.

Tests were linked against an archive of all `src/**` objects (chunk objects recompiled
from the final restored sources). The normal CMake `guild` target currently fails to LINK
only because of `src/gui/widget_layout.cpp` — see handoffs.

## Incident: stash-pop accident & recovery (no work lost)

During the parallel run, one sub-agent ran `git stash` / `git stash pop`; the pop hit a
conflict against concurrent multi-agent edits and **left a stash in place**, reverting most
of the working tree (including most chunk files and goldens) back to HEAD while trapping the
real edits in `stash@{0}`. I recovered surgically: for every chunk source/test file whose
working tree had been reverted to HEAD, I restored the agent-edited blob from
`stash@{0}:reimpl/<path>`; files whose later-finishing agents had already re-applied their
work to the tree (steps6, the 5 combat-core files) were left as-is. Verified post-restore
that every chunk file compiles and every chunk test passes. `stash@{0}` is left untouched
as a harmless backup containing the rest of the (non-chunk) working tree.

## Handoffs (outside this chunk — not edited)

- **`src/gui/widget_layout.cpp`** — broken in-flight edit by another chunk: `w.ld<i32>(…)`
  (`Widget` has no `ld` member; lines ~200/213/214/234/236). This is the sole blocker for the
  `guild` library link; its owner must fix the typo (likely `w.load<i32>` / the project's
  unaligned-load accessor).
- **`src/sim/charaction_steps2.cpp` / `.h`** — a chunk agent recovered two correct 1:1 fixes
  here (RestorePosFinish @0x4d13a9 returns the freeHandlerEntry result, not the RNG draw;
  StateReset0/Alt2 comment unit "seconds"→"minutes"). steps2 is not in this chunk; kept
  because reverting would lose valid binary-faithful work and steps3-8 siblings share the
  convention. steps2 owner please adopt. (steps2 unit test: 102/0.)
- **`tests/unit/combat_slots_test.cpp`, `combat_slots5_test.cpp`** — recovered from the stash
  (their sources `combat_slots.cpp`/`combat_slots5.cpp` are owned by an adjacent chunk).
  Both pass against the current tree; kept to avoid destroying recovered sibling work.
- **`tests/unit/slice_combat_test.cpp`** — was inadvertently restored; its wave-12 hardening
  tests require bounds fixes in `src/play/slice_combat.cpp` (a different chunk) that aren't in
  this tree, so the new tests failed. **Reverted to HEAD** (its source+test pairing belongs to
  the play/slice owner). HEAD version passes 45/0.

No git commit made. `progress/INDEX.md` untouched.
