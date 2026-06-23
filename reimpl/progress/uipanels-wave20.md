# Wave-20 — UI Panels (chat / trade-sell / item-use / hotkey-assign / save-name)

**Agent:** W20-UIPANELS · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Targets (all reached from the frame loop 0x4c09a0, rule 7):

| addr | name | bytes | status |
|------|------|------:|--------|
| 0x4bfc48 | VIBE_ChatConsole_BuildWindow | 838 | **already reconstructed** (verified) |
| 0x50bf08 | VIBE_TradePanel_RefreshSellColumns | 565 | **already reconstructed** (verified, table golden-pinned) |
| 0x5671f4 | VIBE_Item_UseObjectAction | 811 | **reconstructed** (new `src/sim/item_use.{h,cpp}`) |
| 0x4ff070 | VIBE_Hotkey_OpenAssignWindow | 1312 | **reconstructed** (new `src/gui/hotkey_assign.{h,cpp}`; reuses play-layer table) |
| 0x56a700 | VIBE_Menu_RunSaveNameInput | 259 | **reconstructed** (new `src/gui/save_name_input.{h,cpp}`) |

## What was NEW vs already-done (REUSE audit)

Grepped `src/**` for every target address and symbol before writing anything:

- **ChatConsole_BuildWindow** — the line-buffer + 8-channel recipient model + the
  `"$%iFF …$A"` submit assembly were already reconstructed in
  `src/gui/chatconsole.{h,cpp}` (+ `quickchat_window`). I verified the data-logic
  against the decompile (template `dword_4AD48C` = 8x 0xFFFFFFFF confirmed via get_bytes;
  colour bias 1342; suffix "$A") — faithful. No new code; the form-load / widget / modal
  loop is the SDL/Vulkan boundary (routed through the existing command hook). **Not
  re-implemented (would be ODR/duplication).**

- **TradePanel_RefreshSellColumns** — already reconstructed as the shared
  `TradePanel_RefreshColumns(RefreshMode::kSell, …)` + `TradePanel_SelectColumns` +
  `kColTableSell` in `src/gui/trade_panel_windows.{h,cpp}`. I **golden-pinned the column
  table** `dword_507D68` against `get_bytes` (rows 0–3 = `[2,0,0,0],[2,0,0,0],[2,0,0,0],
  [3,0,0,0]`, matches the source) and added a test exercising the sell-eligibility guard
  (`FindSlotByProt[1]==0 -> drop`). **Verified, not re-implemented.**

- **Hotkey table logic** (Validate / Assign / Activate / StoreDefault @0x4fee18 /
  0x4fef54 / 0x4feec4 / 0x4ff954) was already reconstructed in
  `src/play/input_recon4_hotkey.{h,cpp}` (`guild::play`). The **window builder
  0x4ff070 itself was deferred there** (noted as the "huge Form/Hud/RadioGroup" leaf).
  My `src/gui/hotkey_assign` reconstructs **0x4ff070** — the 11-row build loop, the
  per-frame assign/clear/enable dispatch and the rebuild-on-change (LABEL_2) — and
  **REUSES** `guild::play::g_hotkeySlots` / `Hotkey_ValidateAssignments` /
  `Hotkey_AssignFromSelection`. No table logic is duplicated.

## Reconstructed 1:1 (new code)

### `src/sim/item_use.{h,cpp}` — VIBE_Item_UseObjectAction @0x5671f4
The item-use **state effect**. Control flow + constants translated line-by-line:
- result codes -1/-2/-3/-4/-5 (null actor/args, no item id, no slot, perm-blocked);
- perm gate `(actor.permMask & slot.permMask) == 0 -> -5` (slot+0x10 = required mask);
- the success/spin roll: item **373** fails iff `RandomFloatScaled() < 0.66`
  (`dbl_624E20` = 0.66, byte-verified `1f 85 eb 51 b8 1e e5 3f`); every other item
  succeeds. Default RNG reuses `guild::util::RandomFloatScaled` (VIBE 0x58b910) so the
  CRT-LCG draw order matches the rest of the sim;
- effect callback (slot+0x14 low-24b) abort-on-0;
- DispatchPanelEvent(0x2D); QueueRequestArgs25 perm-grant when slot+0x10 set;
- **object-trigger path** (slot+0x04 != 0 -> delta packet + QueueRequestState22)
  vs **plain-use path** (QueueRequest17) — mutually exclusive, exactly as the original;
- law violation when slot+0x08 != -1 (kind = HIBYTE(slot+0x14); city from actor+0x170,
  else -1);
- player panel show when `actor.typeByte == 6 && !args.suppressed`.

The slot record (`FindSlotByItemId` @0x54f04c, 24-byte stride at `word_63D1D8`) field
offsets +0/+4/+8/+10/+14 are mapped in `ItemSlotRecord`. All cross-module / GUI leaves
(FindSlotByItemId, DispatchPanelEvent, the command builders, Gesetz_EvaluateViolation,
Panel_ShowUseObject) are routed through `ItemUseHooks` (inert defaults) — they are owned
by other clusters and are GUI/law boundaries.

### `src/gui/hotkey_assign.{h,cpp}` — VIBE_Hotkey_OpenAssignWindow @0x4ff070
The window orchestration: `Hotkey_BuildRows()` (11 rows; building present -> "%2N8~ %1G"
colour 67, absent -> placeholder colour 66; object present -> "%1s" colour 67),
`Hotkey_WindowAssignEnabled` (0x4ff498: row selected), `Hotkey_WindowClearEnabled`
(0x4ff465: row selected AND holds a building), and `Hotkey_OpenAssignWindow` (entry
ValidateAssignments -> modal loop -> assign/clear -> rebuild). Delegates all table
mutation to the existing `guild::play` hotkey layer.

### `src/gui/save_name_input.{h,cpp}` — VIBE_Menu_RunSaveNameInput @0x56a700
The save-name text-entry state machine: modal loop; on ENTER (key 28) copy the field
text into `buf` and return confirmed(1); any other exit returns 0 with `buf` unchanged.
Constants pinned: field colour 66, width 272 (dword_6951D8), x-region 128 (dword_695090).
Field-widget creation / focus / sizing sprites / RunFrameLoop are the SDL/Vulkan boundary
(via `SaveNameInputHooks`).

## Wiring (rule 13) / handoffs

- **item_use** — the live caller is `Interaction_DispatchPanelEvent`/the orient handlers
  in `src/sim/interaction3.{h,cpp}`, which already route Item_UseObjectAction through
  `Interaction3Hooks::useObjectAction` (currently the inert default). **Handoff (one
  line):** `interaction3` (a bind-site file I do not own) should install a thunk that
  calls `guild::sim::Item_UseObjectAction`. Also referenced by `inventory2.cpp` /
  `npcaction8.h` (documented there). Exposed + tested; no edit to the bind-site file.
- **hotkey_assign** — `Hotkey_OpenAssignWindow` is the building-hotkey ('X'/key 88)
  handler. **Handoff:** the input dispatch in `src/play/session_input` /
  `src/sim/cheat_recon.cpp` (which already documents `if (byte_67225C==88)
  VIBE_Hotkey_OpenAssignWindow()`) should call `guild::gui::Hotkey_OpenAssignWindow`
  with the host modal hooks installed.
- **save_name_input** — the live caller is the save-slot driver
  `src/app/save_drivers.cpp` (already documents the `0x56aa36` handoff:
  `if (!Menu_RunSaveNameInput(...)) reshow slot`). **Handoff:** save_drivers (not owned)
  should call `guild::gui::Menu_RunSaveNameInput(name)` and treat `false` as cancel.

## Tests — `tests/unit/uipanels_wave20_test.cpp` (12 cases, 62 checks, all pass)

- ItemUse: validation codes, perm-block, the 373 RNG gate (both sides + non-373 always
  succeeds), effect-callback abort, object-trigger-vs-plain-use path + law violation +
  player panel + suppression.
- Hotkey: BuildRows + enable logic; modal clear + rebuild; ValidateOnOpen (delegates to
  play-layer, drops a stale building).
- SaveName: confirm-copies-text; cancel-leaves-buffer.
- TradeSell: column-table bytes (golden-pinned `dword_507D68`); sell-guard drops the
  ineligible object.

## Build status

`guild` library (includes the 3 new source files) builds **100% clean**; the
`uipanels_wave20_test` target builds, links and passes in the real CMake build.

> Transient parallel-agent note: during this wave another wave-20 agent was mid-edit on
> `src/sim/command_leaves.{h,cpp}` (+ its test). That caused intermittent full-tree build
> failures unrelated to my files (missing `sim/command_leaves.h` / `RunActionOrFree`
> decl). My modules and tests compile and pass independently; nothing I own touches those
> files.
