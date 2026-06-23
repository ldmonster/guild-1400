# Wave-22 — chat console + trade-panel column refreshers (reconcile to 1:1)

**Agent:** W22-CHATTRADE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Targets the wave-21 coverage audit flagged: wave-20 (`uipanels-wave20.md`) claimed these
were "already reconstructed", but it cited *sibling* addresses and only modelled the data
**sub-logic** — the actual function bodies at the flagged addresses were never translated.
This wave decompiles the real addresses, determines the gap, and reconstructs/reconciles
1:1.

| addr | name | bytes | wave-20 claim | wave-22 finding |
|------|------|------:|---------------|-----------------|
| 0x4bfc48 | VIBE_ChatConsole_BuildWindow | 838 | "already reconstructed (verified)" | **DIFFERENT** — wave-20 `chatconsole.cpp` only has the abstract `ChatConsole` line-buffer + `ChatConsole_AssembleLine` + a `DebugIdList::Append` (which is `0x53629c`, a *different* function). The real 0x4bfc48 driver (recipient build loop, channel restore, submit gather, persist-back) was missing. **Reconstructed.** |
| 0x50bf08 | VIBE_TradePanel_RefreshSellColumns | 565 | "already reconstructed (table golden-pinned)" | **PARTIAL** — wave-20 `TradePanel_RefreshColumns` is a single-pass abstraction; the real fn is a TWO-PASS scan with a distinct column-scratch fill + a typed final scan + the 475/476 slot guard. **Reconciled (faithful fn added).** |
| 0x50b1c4 | VIBE_TradePanel_RefreshItemColumns | 394 | (sibling, uncited body) | **Reconstructed** (faithful sibling). |

## What is genuinely new vs already-done (REUSE audit)

Grepped `src/**` for every target address + symbol before writing.

- **chatconsole.{h,cpp}** — REUSE `ChatConsole_AssembleLine` (the `"$%iFF " + text + "$A"`
  assembly, already faithful: `aIff` "$%iFF ", `aA` "$A", bias 1342) and the 8-channel
  template (`dword_4AD48C` = 8x 0xFFFFFFFF, get_bytes-pinned). ADDED the real driver
  `ChatConsole_BuildWindow` + `g_chatRecipientState` (byte_631E80) + `ChatConsoleHooks`.
- **trade_panel_windows.{h,cpp}** — REUSE the existing `g_buySlots` / `ItemSlot` grid
  model (`trade_panel.h`, `dword_122E064` base, 56-byte stride) and the byte-pinned column
  tables `kColTableItem` / `kColTableSell` (`dword_507C58` / `dword_507D68`, verified
  byte-identical: 17 rows x 4, staircase pattern). Kept the wave-20 abstract
  `TradePanel_RefreshColumns` (used by `dialog_market.cpp`) UNCHANGED — added the faithful
  `TradePanel_RefreshItemColumns` / `TradePanel_RefreshSellColumns` alongside it.
- No symbol redefined (no ODR): `g_buySlots`/`kBuySlotCount` extern-reused;
  `RequestBuildOp75Blob`/`word_63CC5C` left to their owning clusters (routed via hooks).

## Reconstructed 1:1

### `src/gui/chatconsole.{h,cpp}` — VIBE_ChatConsole_BuildWindow @0x4bfc48
The window-driver **state machine** (control flow line-for-line from the decompile):
- `qmemcpy(v29, dword_4AD48C)` -> 8 channels all -1; `dword_631E88 = 0`.
- recipient build loop over the scene-player table (`byte_12CE912`, stride 268, 768
  entries): each entry whose channel-tag byte `== 7` becomes a recipient toggle (capped at
  8 = the `v29[8]` array), its player id stored into `channels[next]`, toggle value
  restored from the persisted `byte_631E80[v31]`. `v31` counts toggles built (0..7).
- submit tick: gather the 8 channels (-1 if empty), assemble
  `"$<colour>FF " + edit-text + "$A"` where `colour = dword_12CE964[134*word_63CC5C] - 1342`
  (the active-speaker palette base), forward via `RequestBuildOp75`.
- close: persist each built toggle's value back to `byte_631E80[i]`, then destroy form.

**Boundary (rules 3-4, routed via `ChatConsoleHooks`):** the form load
(`GameTick_Finalize("misc\\chatconsole")`), `Object_AddToWindow` widget alloc, the scene
table walk, the modal `RunFrameLoop` (0x4c09a0), the colour-table read, and the
`RequestBuildOp75` command sink. The persisted recipient-toggle state (`byte_631E80[8]`,
all 1 at boot per get_bytes) is OWNED here as `g_chatRecipientState`.

### `src/gui/trade_panel_windows.{h,cpp}` — RefreshItemColumns @0x50b1c4 / RefreshSellColumns @0x50bf08
Faithful TWO-PASS reconstruction (the wave-20 single-pass abstraction is left in place for
its existing caller). Both reproduce:
- **pass-0 (column scratch):** `v3 = 0; do { v3 += 5; scratch[v3] = colTable[4*page + i]; }`
  — the `+= 5` BEFORE the write puts the four selectors at scratch[5],[10],[15] (+ the 4th
  at the past-the-end index 20, clamped into the 16-dword window the caller owns).
- **pass-1 (QueryFind type 5):** find each live object's prototype in the 16-slot grid; if
  found -> Item zeroes the slot stock, Sell sets the effective stock; if not found ->
  append to the first free slot (Sell, for buildings 475/476, first drops it unless
  `FindSlotByProt(prot)[1] != 0`).
- **pass-2 (grid re-query, k step 56):** re-query each occupied slot; present -> refresh
  effective stock + capacity (Sell applies the same 475/476 guard before refreshing —
  guard-failing slots fall through untouched, matching `if (v15 && v15[1])`); absent ->
  clear the slot id.
- **typed final scan (idx 0..15):** return 1 if any column object is type-65 ('A') with a
  live data ptr (modelled as: any occupied slot with positive stock).

Column tables `dword_507C58` / `dword_507D68` golden-pinned byte-identical via get_bytes
(272 bytes = 17 rows x 4 dwords; rows 0..16 = the staircase {2,0,0,0}…{4,4,4,4}).

## Wiring / handoffs (rule 13)

- **ChatConsole_BuildWindow** ← live caller is the frame loop `VIBE_GameLogic_RunFrameLoop`
  @0x4c09a0 (xref 0x4c11a9), dispatched via the `g_consoleBuilder` slot that
  `VIBE_QuickChat_UpdateWindow` @0x4bff90 already documents in `quickchat_window.cpp`.
  **Handoff (one line):** `quickchat_window` (not owned) should set its full-console
  builder slot to `guild::gui::ChatConsole_BuildWindow` with host hooks installed.
- **RefreshItemColumns** ← `VIBE_TradeTransport_PanelDispatcher` @0x54014c (xref 0x540829).
- **RefreshSellColumns** ← `VIBE_TradeTransport_PanelDispatcher` @0x54014c (0x5420f3,
  0x543595), `VIBE_Location_TradeTransport` @0x513568, `VIBE_StorageDialog_Options`
  @0x5461b0, `VIBE_Panel_RunThievesGuildEquipment` @0x550310.
  **Handoff:** these dispatcher/storage clusters (not owned) call
  `guild::gui::TradePanel_RefreshItemColumns` / `…RefreshSellColumns(page, ctx, objects)`
  after enumerating the building's objects; the wave-20 `TradePanel_RefreshColumns`
  abstraction remains for `dialog_market.cpp`.

## Callees -> leaves

- ChatConsole: `GameTick_Finalize` 0x41beb8, `Window_PositionAtCoord_Thunk` 0x41d964,
  `Form_SelectWindow` 0x41e4cc, `Object_AddToWindow` 0x41ae10, `Object_SetValueOrText`
  0x41dfec, `Widget_AddSpriteToWindow` 0x41217c, `Object_SetColor` 0x41e614,
  `Text_RenderRichString` 0x59d6e8, `Form_GetChildObjectId` 0x41dea8,
  `Object_SetButtonCallback` 0x41e49c, `Widget_SetFocus` 0x421370, `Object_GetDataPtr`
  0x41db9c, `Crt_Sprintf_0` 0x5cba00, `Command_RequestBuildOp75` 0x4955e0,
  `RunFrameLoop` 0x4c09a0, `Form_Destroy` 0x41da04 — all GUI/text/command/modal-loop
  boundary leaves (rules 3-4), routed via hooks.
- Trade refresh: `GameObject_QueryFind` 0x5857fc, `GameObject_IterNext` 0x58529c,
  `Inventory_GetEffectiveStock` 0x5923fc, `Inventory_GetSlotCapacity` 0x592474,
  `Building_FindSlotByProt` 0x5851fc, `Object_GetDataPtr` 0x41db9c — sim-query /
  object leaves; their RESULTS are the `RefreshScanObject` inputs (so the refresh STATE
  logic is reconstructed 1:1; the enumeration itself is the caller's job).

## Tests — `tests/unit/chattrade_wave22_test.cpp` (14 cases, 200 checks, all pass)

- ChatBuild: template/persist constants (get_bytes), tag-7 recipient selection,
  8-channel capacity cap, submit line assembly, cancel-no-forward, persist-back,
  assemble-line golden.
- TradeRefresh: column tables byte-identical, column-scratch fill order (5/10/15),
  item append+refresh, vanished-slot clear, sell non-guarded, sell 475/476 guard drop,
  sell pass-2 untouched.

Built standalone (chatconsole + trade_panel + trade_panel_windows + slider +
test_main): **200 checks, 0 failures.**

## Note on the normal build

The full `cmake --build build` currently fails on an UNRELATED untracked file from another
concurrent wave (`src/gui/gui_object_state.h`: `static_assert(sizeof(ObjectStateRecord)
== 84)` reduces to `88 == 84`, pulled in by `widget_layout.h`). It is not in this wave's
ownership and was broken before any wave-22 edit. My three files
(`chatconsole.cpp`, `trade_panel_windows.cpp`, the test) compile clean (`-fsyntax-only`)
and link+pass in isolation.
