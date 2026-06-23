# input recon4 — hotkey / scancode / drag-grid cluster

Files: `src/play/input_recon4_hotkey.{h,cpp}`, `tests/unit/input_recon4_hotkey_test.cpp`
Tests: 18 cases, 567 golden checks, 0 failures (built against `tests/framework`).

## Reconstructed 1:1 (pure logic)

| addr | name | notes |
|------|------|-------|
| 0x40c790 | VIBE_Input_CharToScancode | 12 hard-coded arrow/keypad chars; else 16-bit key = ch \| (mods<<8) (shift assigns hi=1, ctrl \|=2, alt \|=4); linear scan of 256-entry word_671960, first match index, else 0. Table supplied via hook (built by Win32). |
| 0x4ff954 | VIBE_Hotkey_StoreDefaultEntry | slot write; type!=29 -> work-product else storable object; building id `*(bld+1)`, object id `*(obj+1)`; returns 12*slot. |
| 0x4fee18 | VIBE_Hotkey_ValidateAssignments | 11-slot loop; drops slot (ids->-1) on missing building / set-but-missing object / (selFlags&1)==0. |
| 0x4feec4 | VIBE_Hotkey_ActivateBuilding | guarded by byte_67225C && !byte_671D8A; scan slots for hotkeyChar match, revalidate, open dialog; obj id read `*(obj+2)`. |
| 0x4fef54 | VIBE_Hotkey_AssignFromSelection | precedence dword_631744 else 631748; writes bld `*(sel+1)` / obj `*(obj+2)`; fallback dword_6477A4; fires HUD banner hook; returns 0. |
| 0x54f884 | VIBE_DragSlot_ResetGridTable | PURE, no hooks. 32 rows x 64 bytes: header {0,-1,-1,-1}, 6 sub-slots {id=-1, qty=0}; returns 2032. |

Slot table `byte_122DC10`: 11 records, 12-byte stride { u8 hotkeyChar@+0, i32 buildingId@+4, i32 objectId@+8 }.

## Coupled-leaf hooks (rules 3/4 boundary; inert defaults)
buildingFindById (0x587b20), objectFindById (0x583a70),
buildingComputeSelectionFlags (0x588dec), dialogOpenBuilding (0x4adef4),
buildingFindWorkProduct (0x587674), buildingFindStorable (0x5877ac),
hudStatusBanner (0x59f99c text + 0x4bcdcc banner), scancode table (word_671960).

## Deferred / omitted (reported, NOT faked — rule 8)
- 0x40c710 VIBE_Input_BuildScancodeTable — Win32 VkKeyScanA/MapVirtualKeyA (rule 4).
  Table fed to CharToScancode via the scancode-table hook.
- 0x40ca38 VIBE_Input_DirectInputInit — DirectInput device creation /
  SetCooperativeLevel / SetDataFormat (rule 4). Already routed via the existing
  gamelogic.h `inputDirectInputInit` hook (app_init.cpp mode==3?1:6).
- 0x4ff070 VIBE_Hotkey_OpenAssignWindow — huge Form/Hud/RadioGroup/GameLogic
  window orchestrator over a dozen UI subsystems; deferred (too coupled).
- 0x41fa1c VIBE_DragCursor_Render — sprite/shape GPU draw (rule 3 Vulkan boundary).
- 0x4ad508 VIBE_DragSlot_BeginDragText — reads DInput-latched cursor globals +
  drives drag-cursor sprite; left to the existing gui_dialogs `dragSlotBeginDragText`
  hook (not re-reconstructed in this cluster).
- 0x40fb4c / 0x40fc98 VIBE_Input_SetIconTextById / SetIconText — widget text-buffer
  memmove splice over the 740-byte widget record; coupled to the live widget
  memory model (unk_695080 / dword_69FFB4 / VIBE_Property_Get). Left to the
  existing gui_dialogs4 `inputSetIconTextById` hook; not re-reconstructed here.

## Wiring
Live callers already present in the tree as hooks:
- CharToScancode -> VIBE_Widget_HandleKeyInput (0x420360); src/gui/gui_dialogs4
  routes via `inputCharToScancode`.
- ActivateBuilding / OpenAssignWindow / AssignFromSelection -> the cheat/hotkey
  dispatch already modelled in src/sim/cheat_recon.{h,cpp} (openAssignWindow /
  activateBuilding / assignFromSelection hooks; byte_67225C==88 path).
- The recon4 pure functions can back those hooks once the building/object/dialog
  real subsystems are wired (Building_FindById etc. already exist in src/sim).

## Rule flags
None. No non-pre-approved tech swaps. All boundary leaves are DirectInput/Win32
(rule 4) or GPU draw (rule 3), routed through hooks or deferred per rule 8.
