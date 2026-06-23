# Script load/run wrappers + network/history/round-end menu drivers

Cluster: VIBE_Script (load+run leaves, purchase-location script), VIBE_Menu
(network IP / choose-history / round-end / network save+load), VIBE_Hud labels.

Modules:
- `src/sim/script_recon_purchase.{h,cpp}` (namespace `guild::sim`)
- `src/play/menu_recon_network_screens.{h,cpp}` (namespace `guild::play`)

Tests: `tests/unit/script_recon_purchase_menu_test.cpp` —
24 tests / 69 checks, all passing (compiled + linked + run standalone).

## Reconstructed 1:1

| Addr | Symbol | Module | Notes |
|---|---|---|---|
| 0x43c690 | `VIBE_Script_LoadAndRunMain` | sim | LoadFromScriptDir(name); null→0; else RunMain. |
| 0x43c6ac | `VIBE_Script_LoadAndRunWithArg` | sim | LoadFromScriptDir; null→0; else RunWithArgs(s,1,arg). |
| 0x43c6d4 | `VIBE_Script_LoadAndRunWithArgAlt` | sim | Byte-identical body to 0x43c6ac (binary duplicate); shares `Script_LoadAndRunWithArg`. |
| 0x50588c | `VIBE_Script_RunPurchaseLocationScript` | sim | Full control flow + key-index math: person-table scan (dword_11BB6A0, 32 dword slots), state-byte>1 gate, optional location-vs-building key match, production key = 589*type+base / purchase key = 65*type+base, +1 offset, "%slocations\\%s\\Einkauf_%s.esc" path, finish-previous-then-run, dword_634494 tick (-1 on load fail). |
| 0x529074 | `VIBE_Menu_EnterNetworkIp` | play | Host-IP edit screen: INI Host read (default 127.0.0.1) + fat-string commit + INI write-back; event-loop shell. |
| 0x52df18 | `VIBE_Menu_ChooseHistoryVariant` | play | History radio screen: historySel clamp to -1, RadioSelectionIndex seed/update, HistoryVariantForRow (7 rows: row0→-1, rowk→k-1). |
| 0x52dabc | `VIBE_Menu_RunChooseHistoryNetwork` | play | Network new-game launch: same selection conversion + NetworkHistoryVariantForRow (row0→-1,1→1,2→2,3→3,4→5). Sub-screen/cutscene flow is orchestration (hooks). |
| 0x530bcc | `VIBE_Menu_ShowPlayerRoundEndReport` | play | Building record stride 589, sale-line id predicate (type 4/16/19→0x1C1E else 0x1C1F), profit/loss line (>0→0x1C21, <0→0x1C22, 0→none). |
| 0x56a4d4 | `VIBE_Menu_RunLoadNetworkGame` | play | Network load browser: slot stride 544, scan cap 8704 (16 slots), player stride 238, 952*windowId object index, "gamedata/network"; fat-string lift into byte_122F530. |
| 0x56abcc | `VIBE_Menu_RunSaveNetworkGame` | play | Network save browser: same strides, 16-record cap, "Gamedata\\network\\%s.SAV". |

### Shared pure helpers (golden-vector tested)
`RadioSelectionIndex` (-1→0 else +1), `HistoryVariantFromByte`, `CopyFatString`
(2-byte-stride NUL/attr-terminated copy — low-byte break + `while(attr)` exit, the
exact do/while at 0x529181 / 0x56a63e / 0x56af09), `HistoryVariantForRow`,
`NetworkHistoryVariantForRow`, `RoundEndSaleLineId`, `RoundEndProfitLineId`, and the
stride constants (544 / 8704 / 16 / 238 / 952 / 589).

## Coupled leaves → inert-default hooks (rule 8: named, not faked)
- sim: LoadFromScriptDir/RunMain/RunWithArgs/LoadScript/FindByHandle/Finish,
  Person_FindActiveByEntity, Building_IsProductionType, Vfs_ResolvePath, and the
  partner/location record field reads → `ScriptLoadRunHooks` / `PurchaseScriptHooks`.
- play: GameTick_Finalize (form load), Form_Destroy, GameLogic_RunFrameLoop,
  Selection_Update, INI read/write → `MenuScreenHooks`. The widget/radio-group
  creation, text render, fades, cutscene launches and dialog/message-box edges are
  driven through the host's frame-loop hook (inert by default → zero iterations).

## SKIPPED / already present
- 0x4416c4 `VIBE_Script_SkipBraceBlock` — **already present**: reconstructed as
  `ScriptExecutor::SkipBraceBlock` in `src/sim/script_compiler.cpp`. Not redefined.
- 0x442d88 `VIBE_Script_ParseDeclaration_42d88` — **already present**: the declaration
  pass is reconstructed in `src/sim/script_compiler.cpp` (the type-keyword branch of
  the compile loop, with the sibling 0x4413d0 ParseDeclaration in script_import4.cpp).
  Not redefined.
- 0x552858 `VIBE_Hud_AddRightAlignedLabel` / 0x5528b0 `VIBE_Hud_AddLeftAlignedLabel`
  — **already present**: the right/left-align layout math (x = anchor - width / anchor,
  +0x5C = 1/0, +0x14 = clip width) is reconstructed in `src/gui/hud.cpp`. Not redefined.
- 0x5596ce / 0x5596db / 0x5596e8 HUD `ClearStatusFlagBit{2,4,8}_Thunk` — **OMITTED**:
  these are mid-function JUMPOUT fragments (each clears a bit of STACK[0x308] then
  `JUMPOUT 0x559533` back into another function). They are not standalone callable
  functions; reconstructing them in isolation would not be faithful (rule 8).
- 0x43c6fc / 0x43c788 script thunks (size 10 / 8) — SKIPPED (thunks, size < 12).

## Wiring
- Within-file: all drivers/orchestrators are wired to their leaves through the hooks
  vtables (`Set*Hooks`), so a host can supply the real engine edges and drive them.
- Cross-TU: real callers are present in the reimpl but in EXISTING files —
  Scene_SetupBuildingAmbience (0x5069c0, `src/gui/hud_actionsn`), Script_RegisterCommands
  (0x43c850, `src/sim/script_import3`), Menu_SearchNetworkGames (0x529248,
  `src/gui/netfile_run`), Net_LoadNetworkSaveProfile (0x528f24, `src/net/lobby`),
  Menu_RunOptionsMain (0x56dccc, `src/gui/menu`). **PENDING**: wiring into those TUs
  requires editing existing files (disallowed for this task); the hooks API is the
  integration seam left for the host.
