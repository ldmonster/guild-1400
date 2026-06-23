# Wave-22 — NPC group-notify + building upgrade-tree window (W22-NPCBUILDING)

**Agent:** W22-NPCBUILDING · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Closes the two carried-over unwired/missing functions from wave-21:
the group join/leave/exec notifier (`0x4c9dec`) and the building upgrade-tree
window opener (`0x594100` — the live caller of the wave-21 road_network), 1:1 from
the Hex-Rays decompile + paged disasm, every float→int site verified.

---

## Reconstructed (with addresses)

| addr | name | where | status |
|------|------|-------|--------|
| 0x4c9dec | VIBE_NpcAction_NotifyJoinLeaveGroup | `src/sim/npcaction_notify.{h,cpp}` `NpcActionNotifyJoinLeaveGroup` | full 1:1 body |
| 0x594100 | VIBE_Building_OpenUpgradeTreeWindow | `src/sim/building_upgrade_window.{h,cpp}` `Building_OpenUpgradeTreeWindow` | full 1:1 body |
| 0x592d98 | VIBE_Map_ComputeRoadNetworkLayout | REUSED `world::RoadComputeNetworkLayout` (wave-21) | extern, no ODR — handoff closed |
| 0x5c6b08 | VIBE_Coord_ConvertX (truncate) | REUSED `util::ConvertX` | leaf, reused |

### 1 — NotifyJoinLeaveGroup (0x4c9dec, 2154 bytes)

A FourCC-dispatched group-membership notifier (`a1@eax` tag, `a2@edx` occupant
entity, `a3@ebx` seat/group node, `a4@ecx` person id). Tags (compared as
little-endian dwords; spell text big-endian): **"join"** 0x6A6F696E, **"cler"**
0x636C6572, **"leav"** 0x6C656176, **"new "** 0x6E657720, **"exec"** 0x65786563.
Unknown tag → no-op (the original's outer-range early `retn`).

Recovered data model (all routed through `NpcNotifyHooks` so the STATE logic is 1:1
while the genuine boundaries stay swappable):
- **Prologue (0x4c9e38):** resolve the seat node's 4 member slots (`*(u32*)(a3+28+4*slot)`)
  via `Person_FindRecordById` into a roster; `memberCount` = number resolved.
- **Group leader** `v52 = &word_12CE910[268 * *(u16*)(a2+39)]` — the 768-record
  city/person array (stride 536) indexed by the occupant's team word. Leader kind =
  `*(u8*)(v52+2)`, objId = `*(u32*)(v52+4)`.
- **City sweeps** (`byte_12CE912[i]` = person i's kind +2, `dword_12CE914[i]` = objId
  +4, stride 536, 768 records): "new " broadcasts 5287 to every kind 6/7 person ≠
  leader; "leav"'s count==4 path broadcasts 5288 to every kind 6/7 person that IS in
  the roster (the original's `v40<v56` = inner-loop-broke-on-match — verified, NOT
  inverted).
- **"join"/"leav"** resolve `a4` via FindRecordById (early-out if absent), broadcast
  5290/5291, message each player/host roster member ("leav" skips the leaver), then
  the leader gets 5283/5284 — concatenated with 5285 (join, memberCount==3) /
  5286 (leave, memberCount==1) via the `"%s%s"` format.
- **"cler"** broadcasts 5292 to every player/host roster member.
- **"exec"** the bar-tab settle: per-member tab = `ConvertX(price(377)+price(378))`
  **TRUNCATED toward zero** (verified vs the `fistp`-after-`ConvertX` at 0x4c9f71 —
  st0 already integral, store exact); `QueueRequest16` the tab from leader to each
  member, message host members, reciprocal `QueueRequestCoord27(a,b,3)` social links
  leader↔member and every ordered member↔member pair (i≠j), then 5282 to the leader.

Message ids (decimal) recovered at the call sites: 5282–5292; history/quickjump tag
1418 (0x58A); tab goods 377/378; player kinds 6/7. The He_SendQuickjumpMessage
constant -1/0 fields are the original's fixed args (only recipient/from/text vary).

Callees (genuine leaves, decompiled): `He_SendQuickjumpMessage` @0x4c5d98,
`Command_QueueRequest16` @0x494630, `Command_QueueRequestCoord27` @0x494878,
`Building_LookupCachedMarketPrice` @0x58f6b8, `Person_FindRecordById` @0x58bc6c,
`Text_RenderFormattedMessage` @0x59f99c — all routed through hooks (text render /
message send / command queue / person lookup / market price are the data + UI/sim
boundaries). Inert defaults make every effect a no-op.

### 2 — OpenUpgradeTreeWindow (0x594100, 430 bytes)

Opens & draws the building upgrade-tree window. Flow (1:1):
1. `dword_13CE288 = GameTick_Finalize(0,0,"techtree\\TechTree")` — the `.form` VFS
   loader @0x41beb8 (UI/file boundary, returns the form handle). Stored via the
   wave-shared `building_lifecycle::SetUpgradeWindowHandle` (dword_13CE288).
2. `Form_CenterChildWindows` / `dword_13CE280 = dword_62EB38` / `Form_SelectWindow(0,1)`
   / `Text_RenderRichString("$[%s$]",25)` / `Form_SelectWindow(0,3)` — UI boundary.
3. `RoadComputeNetworkLayout(a1, a2, rect.right>>16, rect.bottom>>16)` — **REUSED 1:1**
   from world/road_network (this is the wave-21 handoff, now closed). Non-zero
   (empty/target-absent) → `Form_Destroy(handle,-1)` and **return -1** (eax=edx=-1,
   verified at 0x594292).
4. `BuildUpgradeTree(a1,a2)` @0x59361c — pure UI presentation (Window_AddChildWindow /
   Object_AddToWindow / progress bars over the live object tables). A boundary hook;
   its per-node STATE classifier is already reconstructed as
   `building_lifecycle::Building_ClassifyUpgradeNode`.
5. **The edge-draw geometry loop** (the genuine logic, reconstructed byte-exact at
   0x5941e1..0x59425b): outer node N over [0,nodeCount), inner over [0,N); draw a
   `Paintbox_DrawLine` when `inner.nodeId == outer.parentFromId ||
   inner.nodeId == outer.parentToId`.

Recovered field byte-offsets (the dword_12CDD8E-aliased 44-byte record, confirmed by
disasm against the road_network mirror):
- X(N) = `dword_12CDD98[N]` = +0x30 = `RoadNode.cost` (the scaled X).
- Y(N) = `dword_12CDD9C[N]` = +0x34 = **node (N+1)'s coordY** (the documented
  cross-record field alias; +0x34 of N is +0x08 of N+1). The last node's +0x34 reads
  the flat store's zeroed +2-record tail → modelled as 0 (`next < nodeCount ? … : 0`).
  (A subtle gotcha caught in testing: `RoadLayoutState st{}` must be value-initialised
  and the alias bounded to `nodeCount`, since the public state only mirrors live nodes.)
- nodeId = +0x28 hi-word; parentFromId = +0x2A (= (dword_12CDD8E+2)>>16);
  parentToId = +0x2C (= dword_12CDD92>>16).
- Line args: `DrawLine(innerX+24, innerY+48, outerY+24, outerX+24, 88, 0x1F, 20)`.

---

## WIRED (rule 13)

### TavernSocializeState → NotifyJoinLeaveGroup (the 2 reachable call sites) ✅
- `src/sim/npcaction10.cpp` `NpcAction10_TavernSocializeState` (gilde.exe 0x4ca658)
  previously STUBBED its two NotifyJoinLeaveGroup sites (lines 489/498) as
  `sendMessage(tavern_objId, 0)` — wrong: the original passes a FourCC + the resolved
  occupant entity (v13) + the seat entity (v6) to the 2154-byte notifier.
- Added a `notifyJoinLeaveGroup(tag, occupant, seat)` hook to `NpcAction10Hooks`
  (`npcaction10.h`) and replaced both stubs:
  - 0x4ca8b1 (counter==1): `notifyJoinLeaveGroup(kNotifyTagNew,  tavern, seat)`.
  - 0x4ca903 (timed):      `notifyJoinLeaveGroup(kNotifyTagExec, tavern, seat)`.
  (a4/personId is unused for "new "/"exec" — verified: only "join"/"leav" read it.)
- Behaviour-neutral for the live spine: the hook is inert by default (the wave-19
  wiring in `wire_npcaction2.cpp` leaves the ambiguous record-readers/message-send
  hooks inert, the same boundary it documents for NpcAction3/4/10). The change is
  strictly more faithful than the old `sendMessage(...,0)` stub. No golden shift.

### OpenUpgradeTreeWindow → road_network handoff ✅
- `Building_OpenUpgradeTreeWindow` now calls `world::RoadComputeNetworkLayout`
  (extern reuse) — the wave-21 road-network handoff is **closed**: the faithful 1:1
  layout body now has a live in-tree caller.

---

## DEFERRED (reason + address — documented handoffs, NOT faked)

### ExUpdateBuildingLinks NotifyJoinLeaveGroup sites (3) — scene tree not modelled
- `0x49cac4 VIBE_Command_ExUpdateBuildingLinks` has 3 NotifyJoinLeaveGroup sites, all
  of signature `(a1[4]=tag, occupantRec, seatEntity, a1[5]=personId)`. They are only
  reached AFTER the type-301 group scene node is located via `GameObject_QueryFind`.
  The reconstruction in `command_apply5.cpp ExUpdateBuildingLinks` does **not** model
  the live scene tree, so the QueryFind misses and the handler returns 1 **before**
  reaching any NotifyJoinLeaveGroup call (exactly the original's node-absent path).
  Thus there is no reachable stub to fix there today — wiring those 3 sites requires
  the live scene-tree (a sim/scene-owned change). When the scene tree lands, each site
  becomes `NpcActionNotifyJoinLeaveGroup(tag, occupantRec, seatEntity, personId)`.

### OpenUpgradeTreeWindow real caller @0x50f7c0 — caller not reconstructed
- The single caller is `VIBE_Building_OpenUpgradeWindow @0x50f7c0` (0x50f851:
  `OpenUpgradeTreeWindow(building, ebx, "techtree\\TechTree")`; `result < 0` →
  error path). Only its charge-math block is reconstructed
  (`building_lifecycle::Building_ComputeUpgradeChargeAmount`); the full 0x50f7c0 body
  (a GUI-coupled modal) is not in src/. **Handoff:** when 0x50f7c0 lands, it calls
  `sim::Building_OpenUpgradeTreeWindow(buildingRec, targetIdPtr, "techtree\\TechTree")`
  and branches to its error path when the result is < 0. The building options menu
  (`gui/building_options_menu.cpp`, the `kOpenUpgradeWindow` action, text 5055) is the
  UI entry that drives 0x50f7c0.

### notifyJoinLeaveGroup live install — same inert-record boundary
- A faithful live install of the `notifyJoinLeaveGroup` hook (and the full
  `NpcNotifyHooks` set: person array, market price, message senders) is blocked on the
  same ambiguous polymorphic record readers + UI/sim message boundary that
  `wire_npcaction2.cpp` leaves inert. Documented there; the reconstructed body runs
  fully under its unit-test mock.

---

## Tests + docs

- `tests/unit/npcaction_notify_test.cpp` — 9 cases, **37 checks**: every FourCC branch
  (unknown no-op, cler/new/join-absent/join-3-welcome/join-2/leave-1-farewell/
  exec-tab+links), recipient gating (kind 6/7), rendered-id order, the truncated tab
  total, the reciprocal coord-link count, inert-default silence.
- `tests/unit/building_upgrade_window_test.cpp` — 4 cases, **37 checks**: the
  empty-tree → Form_Destroy → -1 path, the open/center/ctx/select/title/build call
  order, the **edge-geometry golden** (3 edges over the wave-21 4-node tree with the
  exact Paintbox_DrawLine coords incl. the +0x34 cross-record Y alias), inert default.
- Headers document the full data model + every recovered constant/offset.

Build: full `guild` lib + all tests compile clean; **ctest: 1549/1549 pass, 0
failures** (incl. npcaction10 37, world_road_network 33, building_lifecycle 118, and
the e2e goldens app_real_run/app_real_boot/playable_flow/app_full_wired_playthrough —
no shift). Owned files only: new `src/sim/npcaction_notify.{h,cpp}` +
`src/sim/building_upgrade_window.{h,cpp}`, the bind sites in `src/sim/npcaction10.{h,cpp}`,
the two test files, and this progress doc.
