# Wave-20 — Command-queue + economy + turn-tick + misc-frame LEAVES (W20-CMDECON)

**Agent:** W20-CMDECON · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the wave-20 lockstep-command / economy-math / config / cutscene /
cross-fade leaves reachable from the frame loop (`0x4c09a0`) and display init
(`0x527fa4`).  Every function is a 1:1 translation of the Hex-Rays decompile,
cross-checked against the disasm where the decompile carried uninitialised-register
noise (FindOrAllocSlot, ApplyStatsFromAck, CheckMaster, Config).

## Owned modules

| file | functions |
|------|-----------|
| `src/sim/command_leaves.{h,cpp}` | 0x40c2f0, 0x485f7c, 0x49514c, 0x5799a8, 0x5792e0, 0x4ac0c8, 0x41e814 |
| `src/world/economy_quality.{h,cpp}` | 0x579a38 (verified pre-existing; see below) |
| `src/play/config_apply.{h,cpp}` | 0x56c0cc |
| `tests/unit/command_leaves_test.cpp` | 28 tests / 145 checks, 0 failures |
| `tests/unit/config_apply_test.cpp` | 3 tests / 12 checks, 0 failures |

## Reconstructed functions (1:1)

### 0x40c2f0 — VIBE_CharAction_InsertActionArgs → `CharActionInsertActionArgs`
Sibling of the pre-existing `InsertActionVararg` (0x40c1e4).  Type id = `BYTE4(a3)`
of the packed `__int64`; clamped to 0 if its registry step fn is null.  Fills the
node via `QueueInsertEntry` (reused from `charaction.cpp`): step from registry,
`+9`=type, `+20`=owner, args copied from `&a4` to `node[48+4*i]` (registry
argCount), `+4`=chained-fn (a2), `+8`=ready (low byte of a3).  RunActionOrFree
nodes latch the type into `args[1]` (the `node[48]=type` quirk).  Reuses the
charaction node pool / registry / `RunActionOrFree`; no ODR.

### 0x485f7c — VIBE_Command_FindOrAllocSlot → `CommandFindOrAllocSlot`
Verified against the **disasm** (the decompile's `v11` was uninitialised noise; the
alloc path consistently uses `ecx == edi+0x94+44*v7`).  16-slot × 44-byte order
roster at `ownerBase+0x94`; key at roster offset 0, lookup key `*(keyRec+4)`.
Reuse pass (key match) then alloc pass (key == -1 → zero 44 bytes, clear the +0x98
byte, write key); roster-full → -1.  Modeled over a flat `SlotRoster` buffer so the
freshly-zeroed-slot semantics (which the combat `Build*Packet` staging copies rely
on) are byte-exact.  **Golden-pinned** (alloc→reuse, full→-1, fresh-slot zeroing).

### 0x49514c — VIBE_Command_QueueRequestGuardTarget61 → `CommandQueueRequestGuardTarget61`
Opcode-61 guard-order packet assembly.  Null owner → -1.  Squad slot
(FindAvailableSquadSlot) + nearest-enemy (FindNearestEnemyTarget) ids packed at the
recovered packet offsets (owner `+0x10`, enemy `+0x14`, mode `+0x18`, squad
`+0x19`, a4 `+0x1D`, a3 `+0x1E`); the nearest-enemy scan is consulted only when the
squad slot is absent or not underfull-filled (`(slot && !*(slot+0x170)) || !slot`).
The two scans are already reconstructed in `sim/combat_slots*.cpp`; their *results*
are threaded through `GuardTargetInputs` so the assembly RULE is 1:1 and the **packet
bytes are golden-pinned** (verified after `EnqueuePacket` stamps the header).

### 0x5799a8 — VIBE_GameTick_HandleTurnControlCommand → `GameTickHandleTurnControlCommand`
Turn-control state machine keyed on the 4-char tag at packet `+36`
(`*((u32*)a1+9)`): `'enbl'`→latch=1, `'dsbl'`→latch=0, `'init'`/`'set '`→memcpy 36
bytes (overwriting the latch dword, which IS block[0..3]), unknown→0.  The
original's nested ordered compares are reproduced as a switch on the recovered magic
dwords (0x656E626C / 0x6473626C / 0x696E6974 / 0x73657420).  **Golden-pinned**
(enable/disable, init/set 36-byte copy, unknown-tag no-op).

### 0x5792e0 — VIBE_City_ApplyStatsFromAck → `CityApplyStatsFromAck`
Verified against the **disasm** of the averaging loop (0x5793d5).  Copies the ACK
head, re-seeds `flt_641DA8` (the cap divisor reused by economy_quality — **written
through to `world::g_capDivisor`**), then over the active-person set accumulates 5
satisfaction sums + `v4 = 5·Σneed` (the inner loop runs 5× per person, adding the
same need byte each time) and averages each by `1.0/count` via **ConvertX
(truncation toward zero)** — a bare `fistp` would round-to-nearest, so the
RC=truncate `frndint` is load-bearing.  `out[j] = trunc(sat[j]/count)`,
`overall = trunc(v4/count)`.  count==0 guarded to a defined 0 (documented
divergence from the original's divide-by-zero; not a live state).  **Golden-pinned**
including a truncation-vs-rounding discriminator (8/3 → 2, not 3).

### 0x4ac0c8 — VIBE_Cutscene_CheckMaster → `CutsceneCheckMaster`
Verified against the **disasm** loop (0x4ac11e).  Master-present → 1.  Else scan ≤16
participants (id at `a1[edx]+0x34`), tracking the LAST resolved record (seeded with
the current-master record); **stop at the first kind-6/7 participant** (the new
master).  No resolvable record → 1; record owner == current master → 1; otherwise
broadcast a master-change (RequestBuildOp95 + ack-spin) → 0.  The participant scan +
record lookups + the op95 round-trip are runtime sim / lockstep-channel side effects
(routed through `CutsceneMasterInputs` + a broadcast callback); the decision logic is
1:1.  **Golden-pinned** (present, new-master broadcast, stop-at-first-67, unchanged).

### 0x41e814 — VIBE_Gfx_CrossFadeStep → `GfxCrossFadeStep`
Cross-fade advance.  `+28` is BOTH the running alpha AND the teardown gate:
`alpha += 8`; re-arm (`SetFadeParams`) while `alpha <= 255`; blit captured rows while
`alpha ∈ (255,288]`; teardown (free surfaces + destroy widget + clear the active-fade
global) once `alpha > 288`.  The re-arm / row blit / heap+widget frees are the
Vulkan/SDL boundary (rules 3-4) routed through `CrossFadeOps`; the step sequencing +
alpha arithmetic + teardown gate are 1:1.  **Golden-pinned** (re-arm, blit, teardown,
inactive no-op).

### 0x56c0cc — VIBE_Config_ApplyCameraAndScrollSettings → `ConfigApplyCameraAndScrollSettings`
Verified against the **disasm** (the decompile's `v1` was `edx == &dword_1233558`,
the options block).  `dword_62D0E4=0`; scroll-speed = advanced branch
`((double)(unsigned)dword_631284·0.25 + 0.5)·0.8` when `word_63C740 & 4` else linear
`(double)(int)opt.field0·0.00625 + 0.25`, narrowed to f32 (`dword_62D07C`);
`SetWheelBase(wheel-64)` (reused from `gui/input_state.cpp`, folds in the +256);
`dword_6316C8 = 100 - sensitivityByte`; returns the sensitivity byte.  FP constants
recovered via get_bytes (flt_62520C=0.00625, dbl_625214=0.25, dbl_62521C=0.5,
dbl_625224=0.8).  **Golden-pinned** including the unsigned `dword_631284` read
(high-bit value stays positive).

### 0x579a38 — VIBE_Economy_ComputeAverageQuality (pre-existing, VERIFIED)
Already reconstructed in `src/world/economy_quality.cpp` (committed init).
Re-decompiled and confirmed **byte-exact**: loop 169..4732 step 169, accumulate the
`typeDef+583`/`typeDef+584` bytes for occupied objects, `(float)((double)num/(double)den)`,
0.0 when den==0.  No change required; the existing fixed-point rounding (the final
`float` narrow of the double division) is faithful.

## Genuine leaves reached / reused (not re-reconstructed)

| addr | symbol | status |
|------|--------|--------|
| 0x5c6b08 | VIBE_Coord_ConvertX (trunc) | reused `util::ConvertX` (`std::trunc`) |
| 0x40c870 | VIBE_Input_SetWheelBase | reused `gui::Input_SetWheelBase` |
| 0x49388c | VIBE_Command_EnqueuePacket | reused `sim::CommandQueue::EnqueuePacket` |
| 0x40c15c | VIBE_CharAction_QueueInsertEntry | reused `sim::QueueInsertEntry` |
| 0x406a00 | VIBE_Character_RunActionOrFree | reused (identity comparison) |
| 0x57e76c | VIBE_Combat_FindAvailableSquadSlot | reused (results threaded) |
| 0x57e50c | VIBE_Combat_FindNearestEnemyTarget | reused (results threaded) |
| 0x495c3c | VIBE_Command_RequestBuildOp95 | host round-trip (callback) |
| 0x4939d4 | VIBE_Command_GetPacketStatusById | reused (ack-spin in callback) |
| 0x4becdc | VIBE_Amt_RefreshGuildState | reused (ack-spin in callback) |
| 0x5d9078 | VIBE_Gfx_SetFadeParams | Vulkan/SDL boundary (callback) |
| 0x579ad0 | VIBE_Statistics_BuildEconomyReport | reused (host-wired report rebuild) |

`VIBE_Light_SetGrayColorThunk(0,44,…)` / `VIBE_Memory_FreeDebug` /
`VIBE_Widget_DestroyByType` are memset / heap / widget leaves reproduced inline
(memset) or routed through the ops boundary.

## Wiring / handoff (rule 13)

These are dispatch-table / command-apply leaves whose real callers live in bind-site
files this agent does not own (`sim/command_apply.h` notes them as handoffs,
`play/gamelogic_recon.{h,cpp}` carries the opcode-dispatch table at 0x45/0x46 for the
cross-fade and the per-opcode command handlers).  Documented handoffs:

- **FindOrAllocSlot** — the `combat_packets.{h}` `CombatOrderContext::findOrAllocSlot`
  callback (BuildAttack/MoveTo/Tile/SimplePacket, already reconstructed) is the live
  consumer; a host can now back it with a real `SlotRoster` + `CommandFindOrAllocSlot`
  (the `bool(handle,target)` adapter: `roster.CommandFindOrAllocSlot(key) != -1`).
- **QueueRequestGuardTarget61** — consumed by the guard-order issue path referenced
  in `sim/character_ai.h` (0x49514c note) and `play/gamelogic_recon.h`; host supplies
  the squad/enemy scan results via `GuardTargetInputs`.
- **GameTick_HandleTurnControlCommand** / **City_ApplyStatsFromAck** — both are
  opcode-apply handlers noted in `sim/command_apply.h` (0x5799a8 / 0x5792e0); they
  plug into the `CommandQueue` dispatch table (`set_handler`) — the handler thunk
  unpacks the 153-byte packet into `TurnControlState` / `CityStatsAck` and calls these.
- **Cutscene_CheckMaster** — the faithful replacement for the `BuiltinPrepareReady`
  skeleton in `sim/cutscene_process.cpp` (0x4ac1b0); that file is not owned here, so
  the handoff is: `CutsceneProcessActive` should call `CutsceneCheckMaster` with the
  slot's master + participant records instead of the `ActorHasParticipant`-only stub.
- **Gfx_CrossFadeStep** — the frame-loop fade step noted in
  `render/floorgfx_recon.cpp` (0x41e814) and dispatched at 0x45/0x46 in
  `play/gamelogic_recon.cpp`; host backs `CrossFadeOps` with the Vulkan blit + the
  SDL surface frees.
- **ConfigApplyCameraAndScrollSettings** — the options-accept + boot path
  (`gui/options_screens.cpp` `ApplyCameraAndScroll`, `app/engine_init_app_recon`
  `configApplyCameraAndScrollSettings`); host feeds the live options block into
  `CameraScrollOptions` and applies `CameraScrollGlobals` to the camera/input state.

No bind-site files were edited (ownership); the handoffs above are the one-line
integration points.

## Build / test status

- `libguild.a` builds clean with the new modules; no ODR clashes (every symbol
  defined once — grep-verified).
- `command_leaves_test`: 28 tests, **145 checks, 0 failures**.
- `config_apply_test`: 3 tests, **12 checks, 0 failures**.
- A dependent full-lib test (`actionqueue_boundary_test`) links + builds green.

## Notes

- A concurrent wave agent's `git clean` repeatedly removed the untracked
  `src/sim/command_leaves.{h,cpp}`; they were recovered from the git index and
  `git add`-staged (NOT committed) to survive further cleans.  No commit was made.
