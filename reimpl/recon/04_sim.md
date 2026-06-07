# Recon 04 — Core Game Simulation Cluster (`gilde.exe`, "Die Gilde / The Guild")

**Scope:** entities/records, characters, NPC AI, objects/buildings, combat, the
Command network/lockstep system, and the simulation tick. 32-bit x86, imagebase
`0x400000`. All addresses below are absolute (file = runtime, imagebase 0x400000).

> Naming caveat: the `VIBE_` prefixes are auto-generated guesses. They are mostly
> accurate, but **`VIBE_Command_Dispatcher` (0x40a4d4) is NOT the command router** —
> it is the per-tick *walk-on-path* action executor (`ch_WalkOnPath`). The real
> network command router is `VIBE_Command_ExecCommands` (0x494088) + the jump table
> at `funcs_4941F4` (0x631298). Treat the `Command_Ex*` / `Command_Queue*` families
> as the network command system.

---

## 1. Overview — Entity / Record Model

The simulation is built on a handful of fixed-capacity **global record arrays**,
each a flat array of fixed-stride POD records, scanned linearly for id→ptr lookups.
Bases are heap pointers stored in globals (all read 0 in the cold IDB — game not
loaded). The authoritative reference is `VIBE_GameObject_ResolveEntityById`
(0x583b44), which knows all three array geometries.

### Master arrays

| Array | Base global | Stride | Capacity | id field | "alive" field | Iterator |
|---|---|---|---|---|---|---|
| **Person / NPC** | `word_12CE910` (0x12CE910) | **536 (0x218)** | **768** | `+4` (dword) | `+0` word == -1 → free | `Person_QueryBegin`/`IterNext` |
| **Object & Building** (same type) | base in `dword_13CE298` (0x13CE298) | **169 (0xA9)** | **256** | `+1` (dword) | `+0` byte != 0 → alive | `Person_IterNext` reuses it; `Building_FindById` |
| **GameObject / scene-entity** (tree) | base in `dword_13CE290` (0x13CE290) | **67 (0x43)** | `dword_6498C0` | `+2` (dword) | `+0` word (type) != 0 | `GameObject_QueryFind`/`IterNext` (DFS) |
| **Character (live, in-scene)** | `dword_66F0D0` (0x66F0D0) | ptr array | **512** | — | slot ptr != 0 | `Character_Update` loop |
| **AiPlayer / Type-def parallel** | `dword_13CE294` (0x13CE294) | **589** | (per faction id) | — | — | indexed `589 * personType` |
| Person extra-fields | `dword_12CE914`(0x12CE914, id col, 134-dword stride), `dword_12CEAD8`, `dword_12CE8E0`, `dword_12CEB18`, `byte_12CE918`, `byte_12CE912` | — | 768 | — | — | parallel columns |

Notes:
- The Person record stride 536 is also addressed as **134 dwords** or **268 words**;
  AI code indexes `word_12CE910[268 * personIndex]` and `dword_12CE914[134 * idx]`.
- **Objects and Buildings share one array** (`dword_13CE298`, stride 169, 256 slots).
  `Building_FindById` (0x587b20) and the object scan in `ResolveEntityById` both walk
  `13CE298` with stride 169 over 43264 bytes. A type/role byte distinguishes them.
- The **GameObject array is a scene tree**: each 67-byte node has a child ptr at
  `+63` and the `IterNext` (0x58529c) pushes/pops a DFS stack `dword_12356CC`
  (0x12356CC). Node fields: type word `+0`, id `+2`, owner/parent id at word `+5`
  (`+10`), entity ptr at dword index 5 (`+20`).

### id→ptr lookup pattern (verbatim, `Person_FindRecordById` 0x58bc6c)
```c
v2 = 0;
while (word_12CE910[v2/2] == -1 || id != dword_12CE914[v2/4]) {
    v2 += 536;
    if (v2 >= 411648) return 0;   // 411648 = 536 * 768
}
return &word_12CE910[v2/2];
```
Linear scan, O(768). All three arrays use this idiom — a faithful reimpl can keep
the arrays but add a side id→index map without changing observable behavior.

### Person record — inferred field offsets (from AI/turn code)
Byte offsets into the 536-byte record (`Person*` = `char*` base):
- `+0` : record-type / personKind byte (-1 free); also `word` alive marker.
  Observed kind values: 4=production building owner-NPC, 5, 6=human player char,
  7, 11/12/13=guard/official class, 16, 19, 30=plant/farm.
- `+4` : person/entity id (dword).
- `+37` : faction/owner-A (word) — `Person_QueryBegin` filter slot 3.
- `+39` : owner/player id (word) — heavily used (`== currentPlayer`), filter slot 4.
- `+55` : security/heat counter (byte) — decremented by guard security level.
- `+61`, `+65` : relation/attitude scalars (used for mood deltas).
- `+91` : flag byte (bit 0x800 → needs a cmd25 request).
- `+93` : associated GameObject/scene node id (dword) — `QueryFind(person+93,…)`.
- `+105` : plant growth / misc dword.
- `+113` : associated object/plant id (dword).
- `+130`,`+131` : packed coordinate bytes (×scale → world via `Coord_ConvertX`).
- `+303` : packed AI-method class id (`>>24`), parallels `+151*2` word for stack.
- `+357` : profession/role byte (2/1/19/20/16/15 = craft tiers).
- `+532` : "visited this turn" guard flag (set by AI selector).
- `+544` : guard-target packed dword.
- `+583` : tax/income multiplier byte.

### Character record (live scene actor) — offsets (from `Character_Update`/`Dispatcher`)
`Character*` base; lives in `dword_66F0D0[0..511]`:
- `+12` : path/active flag (0 = idle/no path) — gate in walk dispatcher.
- `+20` : pointer to the **avatar/entity struct** (the `v110` "float*" object).
- `+48`,`+52`,`+56` : target tile / mesh handle / start tile.
- `+52` : object/mesh handle (→ `+492` low-poly mesh data, `+520` universe back-ptr).
- `+112` : current **action-queue node** ptr.
- `+128`,`+133` : action sequencing scratch.
- `+136` : universe/scene ptr (compared to `off_649D64`, the active scene).
- `+140`,`+141` : flag bytes (0x04 skip-update, 0x08 dirty-mesh, 0x20 sit/animation,
  0x10 idle-anim pending, 0x80 of `+141` = busy).
- `+240` : waypoint capacity (256), `+244` : waypoint buffer (`ch_t:waypoints`,
  2 bytes/tile, alloc 0x200), `+248` : current waypoint idx, `+252` : -1 sentinel.
- `+260` : "anim attached" state, `+264` : turn angle, `+268` : segment length.
- `+292/+296/+300` : current target world XYZ (float).
- `+296` : "has script action active" dword (gates `ActionQueue_DispatchCurrent`).
- `+340` : morph/transition id (-1 none), `+400` : abort flag.
The avatar object (`*(+20)`, indexed as `float*`): `[1]` flags byte (bit1 indoors,
bit3 mounted/cart), `[4]` move flags, `[11]` id, `[13]` 3D object ptr, `[28]` active
animation handle, `[31]` morph slot, `[34]` parent character ptr, `[73]` cart flag,
`[104]` base move speed, `[105]` speed ramp (0→1).

### The simulation tick

**Per-frame loop:** `VIBE_GameLogic_RunFrameLoop` (0x4c09a0, 0xe64 bytes) — the
master frame. A 22-bit feature-mask arg (`v54`) gates each subsystem. Order:
1. Window pump / input latch / widget+HUD mouse.
2. Cutscene processing, message boxes.
3. **Lockstep network step** (mask 0x20000, throttled to every 7 game-ticks via
   `dword_11AA488`): `Command_FlushSendQueue` → `Command_ReceiveAndQueue` →
   `Command_ExecCommands`.
4. `Script_StepAllActive`, `GameObject_DispatchInteractions`.
5. Render (mask 0x40): octree cull, mesh flush, `Render_RenderMainViewFrame`.
6. `Character_UpdateWorkScripts`, then `Character_RefreshFlaggedLocal` →
   **`Character_Update`** (0x405148) → `Character_ProcessFlaggedLocal`.
7. Weather/sound/ambient, drag-select, tooltips/HUD overlays, present frame.
8. Save/load handling, speed keys.

**`Character_Update` (0x405148):** iterates `dword_66F0D0[0..511]`. For each live
character: rebuilds collision grid if scene changed; runs `Character_CheckAniMorph`;
if `+296` (script active) → `ActionQueue_DispatchCurrent`; else does idle behavior
(find nearby chars within 20.0 → spawn "talk" action 45 via
`CharAction_InsertActionVararg`, or attach stand/sit idle anim). Sets
`dword_62D008 = dword_62EB38` (current game-tick clock) at entry and
`dword_62D004` at exit — these bracket the frame for movement interpolation.

**`ActionQueue_DispatchCurrent` (0x404768):** reads the action node at
`char+296`, calls its function ptr `node[0]` (`vtbl`-style: node+0 = step fn,
node+4 = chained/next fn, node+5 = ready flag, node+12 = call counter). This is the
per-character behavior coroutine driver. Walk steps land in `Command_Dispatcher`
(0x40a4d4, the misnamed walk executor) and `CharAction_WalkStep` (0x4093b0).

**`GameTime_Advance` (0x583150):** pure calendar arithmetic on a packed time record
(`dword_13CE852` = `qword_13CE852`). Layout: `+0` day-of-? (dword), `+4` hour (word),
`+6` minute (dword), `+10` second/tick (dword). Carries seconds→minutes(/60)→
hours(/60)→days(wrap 24), handles negative borrow. Used both for the real clock and
for scheduling future events (AI gives it +N to compute "appointment" times).

**Per-round (turn) tick:** `VIBE_GameTick_BeginPlayerRound` (0x533188). The
turn-based economic simulation. Sequence:
1. `City_ComputeWealthGrid`; dump/clear per-NPC turn flags across all 768 persons.
2. Cutscene/mission setup.
3. Plant/farm growth pass over player-6 persons.
4. `Building_RecalcAllProduction`, `City_SnapshotStats`.
5. **For each faction with a player** (`byte_12CE918`): **`MeisterAi_ProcessPlayerTurn`**
   — the master AI director runs that faction's whole economy/intrigue turn.
6. `Amt_*` passes: production, office prosperity, building tax, loan repayments,
   wages, offices. (Amt = town-hall/administration cluster, adjacent.)
7. `MeisterAi_RunBuildingTasks`, news, `AiMethod_BroadcastGroupState`,
   `City_TickStatsAndBroadcast`, `MeisterAi_ProcessBuildingNeeds`.
8. Broadcasts turn-end sync via `Command_QueueRequestCoord27` per faction;
   `Character_SyncAllTurnStates`.
`Sleep(100)` + `Amt_RefreshGuildState` between heavy passes (UI keep-alive while the
host computes — the host is authoritative and the others wait on net sync).

### AI architecture (three layers)

1. **MeisterAi** — the *Guild-Master / per-player economy director* (~70 funcs,
   prefix 0x459000–0x46b000 and 0x4c6f00–0x4c9400). `MeisterAi_ProcessPlayerTurn`
   (0x5321ec, 0xf9b) is the entry: walks the building array for the player's
   buildings, decides hiring/training/production/pricing/stocking, and emits
   **network commands** (`Command_Queue*`) rather than mutating state directly — so
   the host's AI decisions go through the same deterministic command channel. The
   `MeisterAi_RuleEval*` family (~40 small funcs) are individual decision rules
   (price policy, stock targets, wage level, quality tier, capacity toggles).
   `TradeManageStorage` (0x45f1e4, 0x22ea) and `TradeGeneral` (0x4614d0, 0x1dca) are
   the largest — trade/logistics planning.

2. **AiMethod** — the *per-person behavior method stack* (~50 funcs, 0x467000–0x46b000).
   A global stack `dword_B59658` (0xB59658, byte-packed method ids in the high byte of
   the dword and following bytes). `AiMethodStack_Push`(0x4691e4)/`Pop`(0x4691fc)/
   `Contains`(0x469248). Each "method" is an `Eval*` function (returns a desirability
   score into a 24-byte result frame) plus an apply/exec callback. The catalog lives
   in parallel tables: `byte_B57210` (148-byte stride, 61 entries → method table),
   `dword_B5723C`/`byte_B57240`/`flt_B57244` (per-method gate fn, enable byte, weight
   floats; two 32-byte score-vector blocks per method).
   - `AiMethod_SelectBestRecursive` (0x46965c): the **planner**. For each candidate
     method not already on the stack and of a different class than the current, calls
     its eval fn (`tbl[9]`) to fill score frames, tracks best-by-two-criteria
     (`v25`/`v24`), recurses, returns the chosen 24-byte action descriptor.
   - `AiMethod_ExecuteSelected` (0x469a18): runs the chosen method; serializes the
     resulting need-deltas into a network command (`Command_BeginAiMethodPacket` +
     `AppendAiMethodEntry` per stat + a delta packet) — again, AI acts via commands.
   - `Eval*` examples: `EvalGoToEat`(0x46a4d8), `EvalGoToDrink`, `EvalMoveToBuilding`,
     `EvalAttackTarget`, `EvalSocialInteraction`, `EvalPurchaseDesire`. Needs model:
     `AiNeeds`-style stat vectors (32 bytes = 8 stats) decayed/scored per method.

3. **AiAction** — *target-finding / spatial query helpers* (~40 funcs,
   0x475000–0x47d000). `FindNearbyPerson`, `FindTwoPeopleInRange`,
   `FindNearbyBuilding`, `FindAdjacentEntity*`, `EvalConversationTarget`,
   `DispatchTargetSearch`. These feed the AiMethod evals with candidate entities.

---

## 2. The Command System (deterministic lockstep core)

This is the spine of the game's networking and determinism: **all state mutations go
through fixed-size command packets** applied identically on every peer.

### Packet format
- Fixed record **0x99 = 153 bytes** (the staging slot copied by `EnqueuePacket`,
  `qmemcpy(...,0x99)`). Layout of the *queued* wrapper (the `+145…+152` link fields):
  - `+0` : **opcode** (byte, 0–95). Determines handler + size.
  - `+1` : computed wire size (word, from `ComputePacketSize`).
  - `+3` : status byte, `+4` : ring slot index (dword), `+8` : sequence Count (dword),
    `+12` : ack/seq scratch.
  - `+16…` : opcode-specific payload. (For sync opcode 0x20, byte `+16==14` flags a
    short 17-byte "sync" variant.)
  - `+145` : prev link, `+149` : next link (intrusive doubly-linked list).
- Send ring buffer: `unk_BAFB60` (0xBAFB60), **153-byte slots**, index masked
  `& 0x7FFF` (32768-entry sequence space). Write head `dword_11AA494` (0x11AA494).
- Per-slot ACK/status table: `byte_B5FB60` (0xB5FB60), **10-byte entries**, indexed
  `10 * (seq & 0x7FFF)`; `byte_B5FB56` base. Status 2 = applied/acked.
- `ComputePacketSize` (0x493034) is a big `switch(opcode)` giving each opcode's wire
  length (17–145 bytes; opcodes 0x16/0x17 are variable: a count byte at `+20` then
  per-field `stride*count+4`).

### Pipeline (driven from `RunFrameLoop` step 3, every 7 ticks)
1. **Build:** gameplay/AI code calls a `Command_Queue*` / `Command_Request*` /
   `Command_Build*Packet` / `Command_Enqueue*` builder → fills a 153-byte temp →
   `Command_EnqueuePacket` (0x49388c) copies it into the ring, assigns sequence
   `dword_11AA494`, links it onto the pending send list `dword_11AA46C` (0x11AA46C).
   `Command_GeneratePendingPackets` (0x493584) may coalesce.
2. **Delta encoding** (for entity field updates): `Command_BeginDeltaPacket`
   (0x493a94) resolves the target entity (sets `dword_11AA474` = entity base ptr,
   `dword_11AA460` = payload cursor, `byte_11AA3E4` = field count) →
   `Command_AppendDeltaField` (0x493aec) appends `(width,count,offset,Δvalues)` where
   width ∈ {1,2,4} and the value written is `new - old` (delta vs current entity
   memory) so packets are small; `AppendRawField`/`AppendCopiedField` write absolute
   values. Payload buffer `unk_11AA3E5` (0x11AA3E5), max 0x77 bytes/packet.
3. **Send/flush:** `Command_FlushSendQueue` (0x4934cc). If standalone
   (`dword_764CE0 == -1`) it applies locally via `Command_StoreReceivedPacket`;
   networked it pushes through `VIBE_Net_SendPacket` (0x43bc54) and tracks the
   in-flight cursor `dword_764CE8`.
4. **Receive:** `Command_ReceiveAndQueue` (0x493ebc) pulls from `VIBE_Net_ReceivePacket`
   (0x43b8d0) into `byte_11AA4A4` (0x11AA4A4), then `Command_StoreReceivedPacket`
   (0x493f80) links it into the received list `dword_11AA498` (0x11AA498).
   Fragmented packets reassembled by `Command_ReassembleReceived` (0x49377c) /
   `CheckReassemblyComplete` (0x4936e4).
5. **Execute:** `Command_ExecCommands` (0x494088). Walks the received list in
   **sequence (`Count`) order**, detects lost/duplicate/sync commands (logs
   "Lost a Command", "Received Sync", compares `cmd->Count` vs `dword_11AA468`
   last-requested / `dword_11AA470` last-sync), then dispatches:
   `funcs_4941F4[opcode](packet, ackEntry)` — a **96-entry jump table at 0x631298**.
   Group framing: opcode 5 = group-begin, 6 = group-end, 7 = skip
   (`ExecCommandGroup` 0x4942c0).
6. **Apply handlers:** the `Command_Ex*` family (~120 funcs, 0x496000–0x49d400) is the
   jump-table target set — each decodes its payload and mutates the relevant record.
   Examples: `ExApplyCharacterUpdate`, `ExApplyNeedDeltas`, `ExApplyCombatDamage`,
   `ExCreateGebaeude`, `ExSellObjekt`, `ExSetObjectField`, `ExAssignPersonToOffice`,
   `ExChrGotoBuilding`, `ExAddStraftat` (add crime), `ExAdjustCharacterReputation`.

### Opcode catalog
Opcodes are integers 3..95 (0x5F). The `Command_Queue*N` / `RequestBuildOpN`
builders are named with the opcode number (e.g. `QueueRequestQuad43` = opcode 43,
`RequestBuildOp85Unit`). `ComputePacketSize` is the canonical opcode→size map.
Special: 0x20(32) = entity sync (long 141B or short 17B if subtype 14), 0x4B(75)=144B,
0x1D(29)=95B, 0x16/0x17 = variable-length field batches.

### Key takeaway for reimpl
Determinism rests on: (a) fixed packet layout + opcode size table, (b) sequence
ordering with explicit lost/sync detection, (c) **all mutation routed through Ex***
handlers, (d) AI itself emits commands rather than touching state. A faithful C++
port must preserve opcode numbers, sizes, field offsets, and the delta-encoding
(new-old) semantics exactly.

---

## 3. Key functions (address · purpose)

Entity / lookup:
- `0x58bc6c VIBE_Person_FindRecordById` — id→Person* linear scan (stride 536, 768).
- `0x583b44 VIBE_GameObject_ResolveEntityById` — unified id→{building,object,scene-node}.
- `0x5857fc VIBE_GameObject_QueryFind` — varargs scene-entity query builder (583 xrefs).
- `0x58529c VIBE_GameObject_IterNext` — DFS scene-tree iterator (stack 0x12356CC).
- `0x586c20 VIBE_Person_QueryBegin` — varargs person query (kind/owner/faction filters).
- `0x586a6c VIBE_Person_IterNext` — person/object array iterator (stride 169).
- `0x587b20 VIBE_Building_FindById` — id→Building* (shares object array).
- `0x5917d4 VIBE_GameObject_ResolveTypeFieldB` — resolve node type for filters.

Tick / characters:
- `0x4c09a0 VIBE_GameLogic_RunFrameLoop` — master per-frame loop (only caller of Update).
- `0x405148 VIBE_Character_Update` — per-frame update over 512 live characters.
- `0x404768 VIBE_ActionQueue_DispatchCurrent` — runs current action node (coroutine).
- `0x583150 VIBE_GameTime_Advance` — calendar arithmetic / event scheduling.
- `0x533188 VIBE_GameTick_BeginPlayerRound` — turn-based economy/AI master tick.
- `0x533a54 VIBE_GameLogic_InitOrLoadSession` — session init/load entry.

Action queue / char actions:
- `0x40431c VIBE_ActionQueue_GetFreeEntry` — alloc action node from free list.
- `0x40c15c VIBE_CharAction_QueueInsertEntry` — link node into char's queue (+296).
- `0x40c1e4 VIBE_CharAction_InsertActionVararg` — build+enqueue action (varargs args).
- `0x404370 VIBE_ActionQueue_UnlinkEntry` — pop/free completed action.
- `0x40442c VIBE_ActionQueue_ValidateLinks` — debug link integrity.
- `0x40a4d4 VIBE_Command_Dispatcher` — **(misnamed)** walk-on-path step executor.
- `0x4093b0 VIBE_CharAction_WalkStep` — base walk step.
- Big behavior steps (each ~2–5KB, jump-table'd): `0x4e8bdc NpcAction_RunMarktSupervisorStep`,
  `0x4eb518 NpcAction_BurglaryStep`, `0x4cce04 NpcAction_RecruitmentState`,
  `0x4e7e88 NpcAction_DailyRoutineStep`, `0x4ed95c NpcAction_AttackTargetStep`,
  `0x4ea1e8 NpcAction_JailCellStep`, `0x4e2158 CharAction_RunSabotage`,
  `0x4cea84 CharAction_RaidStep`, `0x4cdf74 CharAction_PatrolStep`,
  `0x4d2900 CharAction_PlagueSpreadStep`, `0x4e0294 CharAction_RunEventMessagebox`.

AI:
- `0x5321ec VIBE_MeisterAi_ProcessPlayerTurn` — per-player AI economy/intrigue turn.
- `0x4c7774 VIBE_MeisterAi_ProcessBuildingNeeds` — building-need resolution pass.
- `0x45f1e4 VIBE_MeisterAi_TradeManageStorage` / `0x4614d0 TradeGeneral` — trade planners.
- `0x4599f0 VIBE_MeisterAi_AssignWorkstations` / `0x45aa78 DistributeWorkstationItems`.
- `0x46965c VIBE_AiMethod_SelectBestRecursive` — behavior-method planner.
- `0x469a18 VIBE_AiMethod_ExecuteSelected` — execute chosen method → emit commands.
- `0x4691e4/0x4691fc/0x469248 AiMethodStack_Push/Pop/Contains`.
- `0x46a4d8 EvalGoToEat`, `0x469e1c EvalGoToDrink`, `0x46ac24 EvalMoveToBuilding`,
  `0x4671c8 EvalAttackTarget`, `0x467768 EvalSocialInteraction`,
  `0x467df0 EvalPurchaseDesire` — scoring methods.

Command system:
- `0x494088 VIBE_Command_ExecCommands` — **dispatch loop** (jump table funcs_4941F4@0x631298).
- `0x4942c0 VIBE_Command_ExecCommandGroup` — grouped-command framing (op 5/6/7).
- `0x49388c VIBE_Command_EnqueuePacket` — copy into ring, assign seq, link send list.
- `0x4934cc VIBE_Command_FlushSendQueue` — send/apply pending.
- `0x493ebc VIBE_Command_ReceiveAndQueue` — recv from net into list.
- `0x493f80 VIBE_Command_StoreReceivedPacket` — link received / apply local.
- `0x49377c VIBE_Command_ReassembleReceived` / `0x4936e4 CheckReassemblyComplete`.
- `0x493034 VIBE_Command_ComputePacketSize` — opcode→wire size map.
- `0x493a94 BeginDeltaPacket` / `0x493aec AppendDeltaField` / `0x493c14 AppendRawField`
  / `0x493c90 AppendCopiedField` — delta serialization.
- `0x493d64 BeginAiMethodPacket` / `0x493d9c AppendAiMethodEntry` — AI need-delta packets.
- Apply handlers (sample): `0x49be74 ExApplyCharacterUpdate`, `0x497ed0 ExApplyNeedDeltas`,
  `0x49c8ec ExApplyCombatDamage`, `0x49c19c ExCreateGebaeude`, `0x496b90 ExSellObjekt`,
  `0x499638 ExAssignPersonToOffice`, `0x499f24 ExChrGotoBuilding`, `0x498f44 ExAddStraftat`,
  `0x49d0e4 ExAdjustCharacterReputation`, `0x49d3f0 ExSetObjectField`.

Combat:
- `0x491688 VIBE_Combat_UpdateUnitOrders` — combat-mode unit AI/order tick (0x159d).
- `0x490a80 VIBE_Combat_PerformAttackAction`, `0x490014 RunBattleSetup`,
  `0x489ba8 LoadScenarioAssets`, `0x48dab0 BuildDeploymentScreen`,
  `0x48e4e4 RunResultScreen`, `0x57e92c Combat_ResolveTargetObjekt`.

Buildings / production:
- `0x583c3c VIBE_Building_RecalcAllProduction`, `0x59064c Production_ComputeOutputOverTime`,
  `0x58f328 Building_ComputeItemBaseValue`, `0x59116c BuildingValue_ComputeRoomWorth`,
  `0x58fe68 BuildingValue_ComputeProductionWorth`, `0x5902ac Building_GetSecurityLevel`,
  `0x40e2b4 Building_Update`, `0x59361c Building_BuildUpgradeTree`.

Relations / persons:
- `0x5942fc VIBE_Relation_LookupMatrixEntry` — faction/person relation matrix.
- `0x594afc VIBE_Person_AdjustMoodAndNotify`, `0x59152c Person_SumCurrencyHeld`,
  `0x5920b0 Person_FindActiveByEntity`.

---

## 4. Data structures (inferred layouts)

### Person record (`struct Person`, **size 536 / 0x218**, array @0x12CE910, 768 slots)
Word-aliased; see §1 for the field list. Critical: `+0` kind/marker word,
`+4` id, `+37` factionA, `+39` ownerPlayer, `+93` sceneNodeId, `+303` aiMethodClass.
Parallel columns (per-index): `dword_12CE914[134*i]` (id), `byte_12CE912[536*i]`
(kind dup), `byte_12CE918[2*i]` (is-player-controlled), `dword_12CEAD8` (turn bitfield),
`dword_12CEB18[134*i]` (cutscene/slot id), `dword_12CE8E0[134*i]` (per-turn accumulator).

### Object / Building record (`struct GameObjectRec`, **size 169 / 0xA9**, @dword_13CE298, 256)
- `+0` : alive/type byte. `+1` : id (dword). Remaining 164 bytes: transform, fill
  level, parent links, occupant slots, room data. (Buildings vs movable objects
  distinguished by the type byte / role; both Ex-handlers operate on this record.)

### Scene entity node (`struct SceneNode`, **size 67 / 0x43**, @dword_13CE290, tree)
- `+0` : type (word). `+2` : id (dword). word index 5 (`+10`) : owner/parent id.
  dword index 5 (`+20`) : linked entity ptr. `+63` : first-child ptr (DFS).
- Per-type def table indexed `dword_13CE27C + 65*type` (65-byte type descriptors).

### Character action-queue node (`struct ActionNode`, ~64 bytes; alloc via GetFreeEntry)
- `+0` : step function ptr (called by `DispatchCurrent`). `+4` : chained/next-phase fn.
  `+5`/`offset 5` : "ready" flag (must be nonzero to run). `+8` : priority byte.
  `+9` : action-type id. `+12` : invocation counter. `+16` : state byte.
  `+20` : owning Character ptr. `+36` : prev node, `+40` : next node (intrusive list).
  `+44…` : variadic arg slots (count = `dword_66FD18[19*type]`). `+48/+52/+56` : type-51
  (talk) endpoint ids. Action-type descriptor tables: `dword_66FCD0[19*type]`
  (default step fn), `dword_66FD18[19*type]` (arg count). Character holds head at `+296`.

### AI method-stack frame
- Stack: `dword_B59658` (0xB59658) = count; method ids packed in high byte of each
  dword slot following (`*(int*)(&dword_B59658 + i + 1) >> 24`).
- Result frame: 24 bytes (two 12-byte halves) carrying chosen-action descriptor +
  two score values (`v25`/`v24` = best-by-criterion-A / criterion-B).
- Method table: `byte_B57210` (148-byte stride × 61). Per method: `[+0]` id,
  `[+9]` eval-fn ptr, `[+10]` apply-fn ptr, score vectors at `+48`/`+80` (32B each,
  MemMove'd forward each tick for decay). Gate fns `dword_B5723C[37*class]`,
  enable `byte_B57240`, weights `flt_B57244`/`flt_B5725C`/`flt_B57264`.

### Command packet (153 bytes) + delta payload — see §2.

### Game time record (`qword_13CE852` @0x13CE852)
`+0` dayCounter(dword), `+4` hour(word), `+6` minute(dword), `+10` tick/second(dword).
Game-tick clock: `dword_62EB38` (0x62EB38). Frame brackets: `dword_62D004`/`dword_62D008`.

---

## 5. Dependencies on other clusters

This cluster is the consumer/orchestrator; heavy outbound coupling:
- **Render / scene** (`VIBE_Render_*`, `VIBE_SceneGraph_*`, `VIBE_Anim_*`,
  `VIBE_Object_SetWorldTranslationXYZ` 0x5af5cc, `VIBE_Light_*`, `VIBE_Heightmap_*`):
  `Character_Update`/walk executor call animation attach, mesh resolve, world
  transform, tile↔world (`Heightmap_TileToWorld` 0x5c65d4). **Tight** — character
  movement is interleaved with rendering/animation.
- **GUI / HUD** (`VIBE_Hud_*`, `VIBE_Form_*`, `VIBE_Window_*`, `VIBE_Panel_*`,
  `VIBE_Widget_*`, `VIBE_Dialog_*`): combat screens, building windows, info panels,
  the turn-result scroll. The frame loop is *the* HUD driver. Several "sim" funcs
  (Building_Open*Window, Combat_*Screen, Inventory_OpenSlotWindow) are really UI.
- **IO / net** (`VIBE_Net_SendPacket`/`ReceivePacket` 0x43bc54/0x43b8d0,
  `VIBE_Net_LoadAndSyncSession`, `VIBE_Memory_AllocDebug`/`FreeDebug`,
  `VIBE_Crt_*`, `VIBE_Script_StepAllActive`): command transport + save/load + the
  scripting VM that drives missions/cutscenes.
- **Audio** (`VIBE_Sound*`, `VIBE_Voice_*`, `VIBE_Music_*`): footstep/ambient/voice
  triggered from movement and actions.
- **Amt / City / He / Straftat clusters** (town administration, city stats, "Heralds"
  news, crime): called from the turn tick — adjacent sim subsystems, likely a
  separate recon doc but tightly bound to MeisterAi.

Inbound: almost everything calls `Person_FindRecordById` (678 xrefs) and
`GameObject_QueryFind` (583 xrefs) — these are the chokepoints.

---

## 6. Effort & risk (per module)

| Module | Funcs (approx) | Effort | Risk / notes |
|---|---|---|---|
| Entity arrays + id lookups (Person/Object/GameObject) | ~30 | **S** | Mechanical; nail strides/capacities first. Foundation. |
| Command core (enqueue/flush/recv/exec/delta) | ~30 | **M** | Determinism-critical; must match opcode table + delta semantics bit-for-bit. |
| Command Ex* handlers | ~120 | **L** | Volume. Each is small but must match field offsets; the long tail. |
| Command Queue*/Request* builders | ~110 | **M** | Repetitive; auto-generatable from opcode table once one is understood. |
| ActionQueue + CharAction steps | ~? + big steps | **L** | `RunSabotage`/`RaidStep`/`PatrolStep`/`PlagueSpread` are 2–3KB each; heavy state machines. **High risk**. |
| NpcAction steps | big steps | **L** | `MarktSupervisor`(0x152d), `Burglary`(0x1198), `Recruitment`(0xebc): largest funcs in cluster. **Highest risk**. |
| Character_Update + movement/walk | ~? | **M-L** | Walk executor (0x40a4d4) is 0x11d1 with float-heavy interpolation tied to render; subtle. |
| MeisterAi | ~70 | **L** | `ProcessPlayerTurn`(0xf9b), `TradeManageStorage`(0x22ea), `TradeGeneral`(0x1dca). Economy heart. **High risk** (balance-sensitive). |
| AiMethod / AiAction | ~90 | **M-L** | Many small funcs but the planner (`SelectBestRecursive`) + score tables are intricate; needs the B57xxx tables dumped from a live IDB. |
| Combat | ~? | **M-L** | `UpdateUnitOrders`(0x159d), result/deploy screens are UI-heavy; battle logic moderate. |
| Building / Production / BuildingValue | ~? | **M** | Value formulas use float constants (0x6234xx) — extract exactly. |
| GameTime / Relation / small helpers | ~20 | **S** | Self-contained, easy wins. |

**Top risks:** (1) the giant NpcAction/CharAction step state machines — opaque
control flow, many magic constants; (2) MeisterAi economy balance (any deviation
changes the game feel); (3) movement/animation interpolation entangled with render;
(4) AI score/weight tables (`byte_B57210`, `B57240`, `B5723C`, weights) must be
dumped from a *loaded* IDB or the data files, since the cold IDB shows zero.

---

## 7. Proposed C++ layout & implementation order

```
sim/
  entity/        EntityArrays.{h,cpp}      // Person/Object/Building/SceneNode arrays + id maps
                 Person.h  GameObject.h  SceneNode.h  Character.h   // POD record structs w/ exact offsets
  time/          GameClock.{h,cpp}         // GameTime_Advance, time record
  command/       Packet.h                  // 153B packet + opcode enum + size table
                 CommandQueue.{h,cpp}      // enqueue/flush/recv/exec ring + lists
                 DeltaWriter.{h,cpp}       // Begin/Append*Field
                 handlers/Ex_*.cpp         // one TU per handler group (jump table)
                 builders/Queue_*.cpp      // opcode builders
  action/        ActionNode.h  ActionQueue.{h,cpp}
                 charactions/*.cpp         // WalkStep, Sabotage, Raid, Patrol, Plague…
                 npcactions/*.cpp          // MarktSupervisor, Burglary, Recruitment, DailyRoutine…
  ai/            AiNeeds.h  AiMethodTable.{h,cpp}  AiMethodStack.{h,cpp}
                 AiPlanner.cpp             // SelectBestRecursive/ExecuteSelected
                 methods/Eval_*.cpp
                 AiActionFind.cpp          // target-finding helpers
                 meister/MeisterAi.cpp     // ProcessPlayerTurn, Trade*, Workstation*
  combat/        Combat.{h,cpp}  CombatScreens.cpp
  building/      Building.cpp  Production.cpp  BuildingValue.cpp
  character/     CharacterUpdate.cpp  CharacterMove.cpp
  GameTick.cpp   // RunFrameLoop, BeginPlayerRound orchestration
namespace gilde::sim { ... }   // sub-namespaces per dir
```

**Implementation order (each step independently testable against the binary):**
1. **Entity arrays + record structs + id lookups** (`Person_FindRecordById`,
   `ResolveEntityById`, `Building_FindById`, query/iter). Everything depends on these.
2. **GameClock** (`GameTime_Advance`) — tiny, validates the test harness.
3. **Command packet + opcode size table + queue ring + Exec dispatch skeleton**
   (no handlers yet). Get the lockstep loop running with no-op handlers.
4. **DeltaWriter + a vertical slice**: one builder (e.g. SetObjectField/QueueRequest16)
   → one Ex handler, round-tripped. Prove determinism.
5. **ActionQueue + WalkStep + Character_Update** — get a character walking a path.
6. **Building/Production/BuildingValue** + the `Amt`/turn-tick scaffolding.
7. **AiMethod tables + planner** (needs the B57xxx data dumped from a live IDB first).
8. **MeisterAi turn** on top of (6)+(7).
9. **The big NpcAction/CharAction step machines** — port one at a time, lowest-traffic
   first; diff against IDA decompilation per basic block.
10. **Combat** last (largely self-contained mode switch).
11. Backfill the remaining ~120 Ex handlers and ~110 Queue builders (mechanical).

---

### Appendix — load-bearing global addresses
```
Person array            word_12CE910  0x12CE910  (stride 536, 768)
Person id column        dword_12CE914 0x12CE914  (stride 134 dwords)
Person kind column      byte_12CE912  0x12CE912
Person is-player        byte_12CE918  0x12CE918
Object/Building base    *(0x13CE298)             (stride 169, 256)
Scene-node base         *(0x13CE290)             (stride 67, tree)
Scene type-def base     *(0x13CE27C)             (stride 65)
AiPlayer/type base      *(0x13CE294)             (stride 589)
Live character slots    dword_66F0D0  0x66F0D0   (512 ptrs)
Action type: step fns   dword_66FCD0  0x66FCD0   (stride 19)
Action type: arg counts dword_66FD18  0x66FD18
DFS scan stack          dword_12356CC 0x12356CC
Query filter state      0x6498A8..0x6498E4       (Person/GameObject iterators)
Game-tick clock         dword_62EB38  0x62EB38
Frame brackets          dword_62D004 / dword_62D008
Game time record        qword_13CE852 0x13CE852
Active scene/universe   off_649D64 0x649D64, dword_649D60 0x649D60
--- command ---
Send ring (153B slots)  unk_BAFB60    0xBAFB60   (& 0x7FFF)
Send seq write head     dword_11AA494 0x11AA494
ACK/status table (10B)  byte_B5FB60   0xB5FB60
Pending send list head  dword_11AA46C 0x11AA46C
Received list head      dword_11AA498 0x11AA498
Recv staging packet     byte_11AA4A4  0x11AA4A4
Delta payload buffer    unk_11AA3E5   0x11AA3E5  (cursor dword_11AA460, target dword_11AA474)
Opcode jump table (96)  funcs_4941F4  0x631298
Net standalone flag     dword_764CE0  0x764CE0  (-1 = single-player/host)
last-requested Count    dword_11AA468 / last-sync dword_11AA470
--- ai ---
Method stack count      dword_B59658  0xB59658
Method table (148B×61)  byte_B57210   0xB57210
Method gate fns         dword_B5723C  0xB5723C
Method weights          flt_B57244 / flt_B5725C / flt_B57264
```
