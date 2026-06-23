# Session input — the REAL input -> selection frame path

Module: `src/play/session_input.{h,cpp}` (namespace `guild::play`, class
`play::SessionInput`).
Tests: `tests/unit/session_input_test.cpp` — 16 tests / 119 checks, all passing.
Regression (consumed modules, unchanged behavior): `input_recon_select_test`
40, `gametick_entityscan_recon_test` 11, `session_select_test` 106,
`gui_input_state_test` 69, `gui_input_test` 112 — all passing.

## The recovered original frame sequence (THE key recon finding)

The original consumes input with a ONE-FRAME event latency, via the
`unk_670FE0` mouse event ring (32 rows x 76 bytes, row flag at +72 ==
`dword_671028[19*row]`):

| Step | Addr | What |
|---|---|---|
| frame head | 0x4c09bf -> **0x40dab8 `VIBE_Input_LatchMouseState`** | **reconstructed 1:1 here**: when `dword_62D0D4`, mirror cursor <- current packet (`dword_672210 = dword_672174`, `word_672214 = word_672178`); zero the mirror EDGE dwords (67221C/672228/672230/67223C/672240/67224C — HELD flags 672220/672234/672244 persist); pop the FIRST pending ring row into the mirror block field-by-field (0x40db10..0x40dbf0) and clear its flag; tail-call the keyboard poll (0x40dbff). **The mirror is what the selection gate reads.** |
| mid frame | 0x4215b0 (`VIBE_Widget_DispatchMouseClick`) | `VIBE_GameTick_MainLoop(ax=word_75BF4A, dx=word_75BF48)` — the dual 512-slot scan (sim/gametick_entityscan_recon, REUSED) -> `dword_75BF40` (= `gui::g_hoverPrev`), `dword_62D22C/62D240/62D290/62D294`; then 0x4215c9/0x4218e0: `dword_75BF08 = dword_62D290 == -1 ? -1 : dword_62D290` (the modal-widget gate input). |
| mid frame | 0x4147cc fallback | `VIBE_SelectEntity_ComputeResult` (input_recon_select, REUSED) as the MainLoop miss functor; its `dword_62D22C`/`dword_62D290` stores merged back into the scan-state view (one shared dword in the original — merge enabled by the additive `SelectEntityResult::secondaryWritten` flag). |
| mid frame | 0x4c0e19..0x4c0e96 | hover-latch resolve (0x4b8ba8 boundary) + `Selection_CommitContact @0x4b950c` (session_select, REUSED) — gate fed from the REAL mirror bytes: `dword_67221C` (left-up edge — **selection commits on the release-edge frame**), cursor `(*(i32*)0x67220E)>>16` / `(*(i32*)0x672210)>>16`, viewport `dword_63CC4C/50/54/58`, `dword_75BF08 == -1`, `dword_62D31C == -1`; empty latch -> the real `Selection_Reset @0x4b9444`; then the 0x4bc280 status-latch tail. |
| frame tail | 0x4c0f21 -> 0x40d388 | `VIBE_Input_PollMouseDevice` device step (rule-4 binding, every global write cited): per-event loop-head edge resets (0x40d3d3..0x40d40f), button transition flag writes (0x40d60e../0x40d65b../0x40d6ab..), wheel `(dword_62D0B8*data)>>8` (0x40d5c3), the OS-cursor absolute latch with the exact double-clamp idiom (0x40d74c..0x40d786), `gui::Input_SaveMouseButtonSnapshot @0x40d338` + `Input_ProcessMouseClicks @0x40cdd0` per event. Invoked through the SHARED `InputPollHooks` binding (`Input_GetPollHooks().pollMouseDevice()` — the additive getter), so the 0x40da88 orchestrator and the frame loop drive one binding. |
| frame tail | 0x4c0f26..0x4c0f41 | when `dword_62D0D4 != 0`: `word_75BF4A = *(u16*)0x672174`, `word_75BF48 = *(u16*)0x672176` (the next frame's scan cursor). |

## Reconstructed 1:1 in this module

| Addr | Symbol | Notes |
|---|---|---|
| 0x40dab8 | `VIBE_Input_LatchMouseState` | the per-frame ring consumer (above); keyboard tail bound to shim key events |
| 0x40cdd0 | `VIBE_Input_ProcessMouseClicks` | click/double-click/drag state machine (15/40-tick windows, +/-3 px), the 12-pair live-vs-current compare, the ring spill (append after the highest occupied row; row-31 overwrite; `dword_67195C` disable; the row-0-never-written producer quirk), the `qmemcpy(&dword_672174, &dword_6721C4, 0x4C)` packet latch. New state globals (ODR-grepped, none modelled elsewhere): `dword_62D0DC/62D0E0/62D0E8/62D0EC/62D0F0/62D0F4/62D0F8/62D0FC/67195C` (`g_inputClick`), `unk_670FE0` (`g_mouseEventRing`), `dword_62EB44` view (`g_inputClock`). |
| 0x40d920 | `VIBE_Input_PollKeyboardDevice` (pure tail) | key tables `byte_671D60`/`byte_671F60`, `byte_67225C` (REUSES `play::g_lastHotkeyChar`), repeat latch `byte_62D100` + `dword_672260` (+9 initial / +5 repeat / sentinel `1316134911` / al = -1 on release). Ring drain = boundary -> `SessionKeyEvent` shim feed. |

Plus the module-owned globals `dword_62D0D4` (`g_softCursorMode`),
`word_75BF4A`/`word_75BF48` (`g_widgetCursorX/Y`).

## Reused (extern, NOT redefined)

`Input_PollMouseAndKeyboard @0x40da88`, `SelectEntity_ComputeResult @0x4147cc`,
`Selection_Reset @0x4b9444`, `g_selectionAnchors`, `g_selectionOwnerB`
(input_recon_select); `GameTickMainLoop @0x414a38` + `GameTickScanState`
(gametick_entityscan_recon); `Selection_CommitContact @0x4b950c`,
`Selection_ClearAll @0x4b94d8`, `Selection_UpdateStatusTextLatch` (0x4bc280
tail), `g_selectGate`/`g_selectionContact`/`g_selectionAnchorRecords`/
`g_selectionCommit` (session_select); `gui::g_mouseInput` block +
`Input_SaveMouseButtonSnapshot @0x40d338` / `Input_SwapCursorClampState
@0x40dc2c` / `Input_SetWheelBase @0x40c870` (gui/input_state);
`gui::g_hoverObject/g_hoverWindow/g_mouseDown/g_mouseClick/g_lastClickedWindow`
(dword_62D22C/62D290/672220/672228/75BF08 dual views, gui/input);
`gui::g_hoverPrev` (dword_75BF40, gui/gui_dialogs4);
`ComputeSelectionVolumeSolve` (the @0x5b7134 kernel, picksel_recon);
`play::g_lastHotkeyChar` (byte_67225C, input_recon4_hotkey).

## Additive edits to owned modules (hook exposure only, tests still green)

- `input_recon_select.h/.cpp`: `SelectEntityResult::secondaryWritten` (records
  whether THIS call stored the shared `dword_62D290`) + `Input_GetPollHooks()`
  (the frame loop calls the mouse device step DIRECTLY at 0x4c0f21, not through
  the 0x40da88 orchestrator). No behavior change; 40 checks still pass.
- `gametick_entityscan_recon.*`: no edits needed (the fallback functor seam
  already exists).

## The provider boundary (renderer-owned data)

`SessionInputProvider { bindScanTables, bindSelectEntityHooks, resolveContact }`
— null members fall back to the REAL default record store:

- **740-byte records** (`dword_69FFB4` model; record dword +0 == own table
  index, proven by 0x421613 `dword_69FFB4 + 740*dword_62D22C`): every scanned
  field at its exact byte offset (+8 action, the +14..+22 / +26..+34 16.16 box
  word pairs, +24 type, +44/+60 native pointers in the unread gaps, +52/+56
  busy, +68/+72/+80 pick-enable, +116 group/secondary, +444 flag). Two REAL
  profiles: typeByte 64 = widget (hover scans -> `dword_75BF08` veto — GUI
  clicks never select in 3D), typeByte 0 = city entity (second-scan pick only,
  gate stays open).
- 512-slot table view (`dword_62D26C`; scans walk 511..0 — later registration
  is tested first) + the 238-dword group gate (`dword_67EDE4`).
- 48-actor store (0x2AC records: live +400/+408/+412, anim names +428/+492,
  list count +388 / ids +4*i+4) + decoded hotspot lists (`dword_67EB80`) +
  rect records in the same 740 table; selection volume routed to the REAL
  `ComputeSelectionVolumeSolve` kernel over registered corner extents.
- `resolveContact` default: the 0x4b8ba8 hover-walk boundary — shadow records
  carrying exactly the bytes 0x4b950c dereferences (+0 handle / person id,
  +8 person-active, **+392 person-alive** (the per-frame stale-worker drop
  0x4b954d), +48 name, +529 highlight bit), the session_select precedent.

## Wave-2 per-frame call contract (swapping the session's renderer Pick)

```cpp
play::SessionInput si;                    // ctor: clamp wide-open (0x40dc2c),
si.SetViewport(vpL, vpT, vpR, vpB);       //   wheel base 256 (0x40c870),
                                          //   dword_62D0D4 = 1 (city scene)
// per frame, BEFORE rendering reads the selection:
si.ClearEntities();
for (each visible entity) {
    play::SessionInputEntity e;           // screen box from the renderer's
    e.id = entityId;                      //   projection (ProjectWorldToScreen)
    e.left/top/width/height = outer box;  // 16.16 box words @16/@18/@20/@22
    e.innerLeft/innerRight/yMin/yMax = the second-scan range;
    e.typeByte = 0;                       // city profile (NOT 64)
    e.kind/typeCode/handle/name/selectable/highlightable/selectionFlags = roster;
    si.UpdateEntity(e);
}
shim::MouseState ms; plat.getMouse(ms);
play::SessionInputFrame f{ms.x, ms.y, ms.left, ms.right, ms.middle,
                          /*wheel*/0, /*clock*/tick, keyEvents, nKeys};
play::SessionInput::Result r = si.Frame(f);
// r.pickedId   == dword_62D22C  (what RealCityRenderer::Pick used to supply)
// r.selected / r.selectedId / r.selectedKind / si.selectedName()
//              == what SessionSelect::current() produced (same live stores:
//                 g_selectionAnchorRecords / g_selectionCommit / worker marks)
// r.actionCode == dword_75BF40 (widget action; 1155/1210 OK/Cancel)
// ESC etc.: r.repeatScancode / play::g_lastHotkeyChar (byte_67225C)
```

Timing (the original's, do NOT "fix"): a physical click in frame N surfaces as
the mirror click edge in frame N+1 and the selection commits on the
release-edge frame; the scan cursor is the previous frame's `word_75BF4A/48`.
`SessionInput` installs `SelectCommitHooks`/`SelectEntityHooks`/`InputPollHooks`
— one input driver at runtime (drop the separate `SessionSelect::OnPick` call
when swapping; `Selection_*` state remains shared and readable).

## Named gaps (rule 8 — omitted, not faked)

| Addr | Symbol | Why |
|---|---|---|
| 0x4b8ba8 | `VIBE_Object_UpdateGateContact` | hover walk over the live scene graph — the `resolveContact` provider boundary (default = descriptor shadow records) |
| 0x421793..0x4217f9 | the `unk_75BA38` 32-slot hotspot-strip scan | drag-cursor payload strip (`dword_62D31C/62D318`); no strip in the session -> -1 (gate-neutral) |
| 0x5b7134 corner fill | `VIBE_Object_ComputeBoneScreenExtents` | renderer-owned bone screen extents; the kernel is REAL (picksel_recon), the corners are provider-supplied; absent -> the original actor-miss path |
| 0x40d388 ring decode | DirectInput relative-mickey scaling (`dword_62D0B8 * flt_610C5C`, 0x40d588) | rule-4 boundary; the binding uses the original's own OS-cursor absolute branch (0x40d74c..0x40d786) |
| 0x40d920 ring drain | DirectInput keyboard GetDeviceData | rule-4 boundary -> `SessionKeyEvent` feed; the pure repeat tail is 1:1 |

## Tests (16 / 119 checks, suite `SessionInput`)

PollPacketMirrorAndEdgeLatency, DoubleClickSwallowsSecondRelease,
KeyboardRepeatWindows, ScanPicksCityEntity, Type9Returns1155Or1210,
WidgetProfileBlocksCommitGate, CommitOnLeftRelease,
ReleaseOutsideViewportNoCommit, EmptyClickDeselects, RightClickClearsSelection,
WorkerSelectMarksAndClears, Type29ContactVoidsSelection,
FallbackActorHitMergesSharedGlobals (REAL volume kernel + the modal veto),
FallbackActorHitWithoutSecondarySelects, FallbackVolumeAbsentIsOriginalMissPath,
FrameOrderingGolden.
