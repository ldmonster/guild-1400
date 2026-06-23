# Input poll orchestration + entity selection (VIBE_Input / VIBE_SelectEntity / VIBE_Selection)

Module: `src/play/input_recon_select.{h,cpp}` (namespace `guild::play`).
Tests: `tests/unit/input_recon_select_test.cpp` — 12 tests / 40 checks, all passing
(verified clean under `-fsanitize=address`).

## Reconstructed 1:1

| Addr | Symbol | Notes |
|---|---|---|
| 0x40da88 | `VIBE_Input_PollMouseAndKeyboard` | Poll orchestrator: PollMouseDevice → copy 0x4C-byte cursor packet (current→prev mirror dword_672210←dword_672174) → PollKeyboardDevice(0); returns the keyboard auto-repeat scancode byte. Device-poll steps are platform-boundary hooks (`InputPollHooks`). |
| 0x4147cc | `VIBE_SelectEntity_ComputeResult` | `__usercall` (eax=screenX, edx=screenY). Walks the 48-entry actor array (dword_676A60, stride 171 dwords), per live actor resolves its animation (slot 0/1), projects the selection volume (Y biased by flt_610EBC = 5.0f), scales by anim+116, rounds via Coord_ConvertX, then scans each child-hotspot list's entry rects (16.16 box at +14/+16/+18/+20 read via misaligned dword>>16) for the point. Returns the hit action code (rect+8) / -1. Stores rect+0..name, picked label-id (dword_62D22C), secondary id (dword_62D290). Type-byte 64 stores only the secondary and keeps scanning. Goto graph (LABEL_6/LABEL_29/LABEL_27) translated literally incl. the "goto-into-loop" return to the while-condition test. |
| 0x4b9444 | `VIBE_Selection_Reset` | `__usercall` (esi=a1). Clears 4 anchor globals (11BC274/11BC278/11BC260/631740), resolves the selection owner (prefers latched dword_631748, else Person_QueryBegin), reads owner+93, clears byte_6317B4, then walks the owner's child game-objects clearing flag-bit 2 (record+32) on each object whose type byte (dword_13CE27C+65*id) == 29. |
| 0x5c6b08 | `VIBE_Coord_ConvertX` | FPU `frndint` under a round-to-nearest control word → `std::nearbyint` (round-half-even). Local namespaced helper; consistent with `gui/statchart.cpp`'s interpretation. (Most other modules model it as trunc-toward-zero; the disasm at 0x5c6b08 is round-to-nearest, used here for the select-coord rounding.) |

## Coupled leaves → hooks (rule 8: named, not faked)

- Actor record array (dword_676A60), animation resolver `VIBE_Animation_GetPtr @0x5d9774`,
  projection `VIBE_Pick_ComputeSelectionVolume @0x5b7134`, hotspot list-record table
  (dword_67EB80) and rect table (dword_69FFB4) → `SelectEntityHooks`.
- Person/game-object query iterators `VIBE_Person_QueryBegin @0x586c20`,
  `VIBE_GameObject_QueryFind @0x5857fc` / `IterNext @0x58529c`, object-type table
  (dword_13CE27C) → `SelectionResetHooks`.
- Device-poll bodies `VIBE_Input_PollMouseDevice @0x40d388` /
  `VIBE_Input_PollKeyboardDevice @0x40d920` → `InputPollHooks`.

### 64-bit fidelity note
The hotspot LIST record packs the entry-id pointer (record+24, `v19[6]`) and the
entry count (`*(i32*)(rec+26)>>16`) in OVERLAPPING bytes — fine for a 4-byte 32-bit
pointer, impossible for a 64-bit pointer. The `hotspotListRecord` hook therefore
decodes `(count, idArray)` at the boundary; the downstream walk math is unchanged.
Same reasoning makes `g_selectionOwnerB` (dword_631748, dereferenced as a record)
a native `u8*` rather than an `i32`.

## SKIPPED — platform boundary (rule 4: Win32/DirectInput → SDL), routed via hooks

| Addr | Symbol | Reason |
|---|---|---|
| 0x40c710 | `VIBE_Input_BuildScancodeTable` | VkKeyScanA / MapVirtualKeyA scancode-table build |
| 0x40c950 | `VIBE_Input_FormatErrorMessage` | wsprintfA DirectInput error string |
| 0x40c9b8 | `VIBE_Input_AcquireMouseDevice` | DirectInput vtable +28/+32 |
| 0x40c9f8 | `VIBE_Input_AcquireKeyboardDevice` | DirectInput vtable +28/+32 |
| 0x40d7c8 | `VIBE_Input_ReadMouseAxes` | DirectInput vtable +36 + GetCursorPos/SetCursorPos/ClientToScreen |

`VIBE_Input_SaveMouseButtonSnapshot @0x40d338` and the ProcessMouseClicks/snapshot
helpers are already reconstructed in `src/gui/input_state.{h,cpp}`.

## Wiring

- `xrefs_to 0x4147cc` → `VIBE_GameTick_MainLoop @0x414a38`: **WIRED** —
  `play::SessionInput::Frame` (src/play/session_input.cpp) passes
  `SelectEntity_ComputeResult` as the real MainLoop fallback functor and merges
  its dword_62D22C / dword_62D290 stores back into the shared-global view
  (see progress/session-input.md).
- `xrefs_to 0x4b9444` → **WIRED for the live frame path**: the per-frame
  commit `Selection_CommitContact @0x4b950c` (session_select) reaches
  `Selection_Reset` on the real empty-click branch, driven every frame by
  `SessionInput::Frame` (the 0x4c0e19..0x4c0e96 sequence); the right-click
  deselect pair (`SessionInput::ClearSelection`) calls it too. The remaining
  callers (Command_ExRemoveBuilding, Building_Enter*, …) stay PENDING until
  those modules stand up.
- `xrefs_to 0x40da88` → no direct xref (the input-init path). **Device-step
  hooks BOUND**: `SessionInput::installPollHooks` installs the shim-fed
  bindings (mouse device step = the 0x40d388 global writes incl. the
  OS-cursor branch; packet mirror copy; keyboard latch), and the frame loop
  drives the same binding directly (0x4c0f21) via the additive
  `Input_GetPollHooks()`.

Additive edits for the session wiring (hook exposure only, behavior unchanged,
40 checks still green): `SelectEntityResult::secondaryWritten` (whether THIS
call stored the shared dword_62D290 — enables the exact shared-global merge)
and `Input_GetPollHooks()`.
