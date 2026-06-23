# Wave-22 — small reachable leaves (W22-SMALLLEAVES)

**Agent:** W22-SMALLLEAVES · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructed the remaining small reachable game-logic leaves listed in
[`coverage-audit-wave21.md`](coverage-audit-wave21.md), 1:1 from the decompile +
disasm, golden-pinned with `get_bytes`. Every byte/constant verified against IDA.

---

## Reconstructed (6 targets + the FlushAndFree wrapper)

| addr | bytes | name | landed in |
|------|------:|------|-----------|
| 0x5437d8 | 150 | VIBE_MapView_AddCornerObjects | `src/gui/mapview.cpp` (owned) |
| 0x4ad508 | 138 | VIBE_DragSlot_BeginDragText | `src/gui/dragtext.{h,cpp}` (new) |
| 0x40fb4c | 330 | VIBE_Input_SetIconTextById | `src/play/input_icon_text.{h,cpp}` (new) |
| 0x4ac9c0 | 39 | VIBE_Cutscene_GetRandSeed | `src/sim/cutscene_rand.{h,cpp}` (new) |
| 0x4ac9a0 | 31 | VIBE_Cutscene_SetRandSeed | `src/sim/cutscene_rand.{h,cpp}` (new) |
| 0x5d9104 | 21 | VIBE_Resource_FlushAndFree | `src/render/coord_transform_leaf.{h,cpp}` (new) |
| 0x5d8b00 | 16 | VIBE_Coord_Transform | `src/render/coord_transform_leaf.{h,cpp}` (new) |

Tests: `tests/unit/smallleaves_wave22_test.cpp` — **47 checks, 0 failures**. The
whole `guild` library + the existing `mapview_test` (58 checks) stay green.

---

## Per-function notes (1:1 grounding)

### VIBE_Coord_Transform @0x5d8b00 (16 bytes) — NOT a float projection
Golden bytes `85 c0 75 01 c3 81 e2 ff ff 00 00 03 44 90 45 c3`:
```
test eax,eax / jnz +1 / retn          ; base==0 -> return 0 unchanged
and edx, 0FFFFh                        ; index is u16
add eax, [eax+edx*4+45h]               ; result += *(i32*)(base + 0x45 + 4*index)
retn
```
Despite the "Coord" name it is a **pointer-relative i32 field add** (field base 0x45,
stride 4), used pervasively by the widget scroll/slider/object layout code
(xrefs: Widget_DrawScrollThumb/Bar, Object_Update, Slider_*, Entity_InteractionLogic,
GameLogic_Objects, Property_Get/Set, Window_LayoutScrollContent, Animation_Apply,
Object_RecomputeSize, Shape_LoadAndRegister). No `fistp`/SSE — no rounding/truncation
concern (ConvertX note in the brief does not apply: there is no float here). Two
overloads: a memory-backed (testable) and a raw-pointer (live wiring) form.

### VIBE_Resource_FlushAndFree @0x5d9104 (21 bytes)
Golden tail `89 d0 5b c3` = `mov eax,edx / pop ebx / retn`. The function calls
`Resource_FreeEntryData(a1,a2)` then `File_FreeStream(a1)` and **returns the FIRST
call's result** (captured in `edx` across the second call — pinned by the test). Its
two callees (`Resource_FreeEntryData @0x5d91d4`, `File_FreeStream @0x5fb574`) are
substantial deep-file/memory/Vfs functions (indirect `off_64A910/14/1C` dispatch,
`Memory_FreeBlock`, Vfs temp-file close, the stream free-list at `dword_1408760`) —
**not leaves**; reconstructing them is out of this wave's scope, so they are routed
through a hook (rule-8 honored: the wrapper's control flow + return-value selection
are 1:1, the deep callees are flagged for a future file/memory wave).

### VIBE_Cutscene_Set/GetRandSeed @0x4ac9a0 / 0x4ac9c0
Both operate on the **single shared global seed word `dword_11B4E38`** (the cutscene
LCG state that `CutsceneRng` (cutscene.h) and the combat-side `CutsceneRng` (combat.h)
snapshot/restore). Per the brief I added **exactly one** definition of this global
(`sim::g_cutsceneRandSeed`) — no second RNG state. Both functions also emit a debug
trace via `VIBE_Crt_Sprintf_0` (`"cut_randseed: %i"` / `"cut_getrandseed: %i"`, exact
strings from `get_bytes @0x61d8ec/0x61d900`); reproduced through a log hook whose
default uses the reconstructed `crt::Sprintf`, so Set's return value (the sprintf char
count) is byte-faithful and the side effect is observable+testable. Get reloads the
global AFTER the trace and returns it, exactly as the disasm shows.

### VIBE_Input_SetIconTextById @0x40fb4c (330 bytes) — LOGIC, not DInput
Splices a localized string (`dword_8C36B0[textId]`) into an object's editable text
buffer (`obj[+0x150]` base, `obj[+0x154]` current-inserted length), replacing the
previous insertion via two `MemMove`s (the gap-open/close arithmetic
`under.regionBase + strlen(under.text) + 1 - base` is reproduced exactly), then
re-measures the widget pixel width with `Property_Get(text, obj[+0x134] font)` and
writes the low word into the live widget record (`wrec+20`). Object record is the
stride-348 `unk_695080` array; widget record is stride-740 at `dword_69FFB4`. Modeled
with explicit structs + a measure hook (`VIBE_Property_Get @0x4152cc` is itself a
separate still-missing leaf, audit row). MemMove order, gap math, byte copy and width
writeback are 1:1. Callers: `Window_ParseMarkupAndBuild @0x416720` (call @0x417872),
`Widget_ProcessMouseDrag @0x420db4` (call @0x421294).

### VIBE_DragSlot_BeginDragText @0x4ad508 (138 bytes)
Arms a TEXT drag: copies the pending drag-text (`byte_61D9C8`) into the active buffer
(`byte_11B6B20`, strcpy via the 2-byte-stride loop), calls (in order)
`DragCursor_SetSprite(this,0)` @0x41fcbc, `DragSlot_ResetTable` @0x41f9dc,
`Input_ResetMouseButtonState` @0x40c87c, latches the drag-start tick
(`dword_631678 = g_gameTick`), sets the cursor tooltip (`Widget_SetTooltipText` @0x421a24),
and arms the drag box (`dword_62D0D4=1`, `dword_62D0C4 = cursorX-8`,
`dword_62D0CC/D0 = 0`, `dword_62D0C8 = cursorY`), returning `boxY`. The drag-box
globals are the same dead-band box modeled in `gui/dragselect.h`. Lives in a NEW file
`gui/dragtext.{h,cpp}` (NOT edited into `dragselect.cpp`, which this agent does not
own) reusing its sibling `DragCursor_SetSprite` semantics through the hook.

### VIBE_MapView_AddCornerObjects @0x5437d8 (150 bytes)
Disasm-grounded: four `Object_AddToWindow(win@ecx, y@dx, x@ax, gfx@ebx)` placements,
each followed by `ZOrder_RemoveObject`:
```
obj0: x=mx,        y=my,         gfx=gfxBase+0
obj1: x=mx,        y=0x1A2 (418),gfx=gfxBase+1
obj2: x=mx,        y=0x78  (120),gfx=gfxBase+2
obj3: x=0x243(579),y=0x78  (120),gfx=gfxBase+3
```
where (mx,my) is the live cursor offset (the original reads the packed mouse globals
`dword_69FFB8/BC >>16`; the constant bases `var_18/var_20` are 0). The existing
`kMapCornerPlacements` table is the (mx,my)==(0,0) snapshot and matches exactly
(`{0,0},{0,418},{0,120},{579,120}`). **Decompile caveat:** Hex-Rays collapsed the
4-arg `__usercall` to a 2-arg call (`Object_AddToWindow(a1, 0/418/120/120)`); the
disasm is authoritative and shows x and gfx vary too. Routes the REAL reconstructed
`gui::Object_AddToWindow` (gui/window.cpp) + `gui::ZOrder_RemoveObject` (gui/zorder.cpp),
preserving placement coords and the per-object remove ORDER. The original tail-returns
`ZOrder_RemoveObject`'s eax; our `ZOrder_RemoveObject` is void, so the function returns
the last placed widget index — observationally equivalent (the sole caller ignores the
return).

---

## Wiring (rule 13)

- **MapView_AddCornerObjects** — declared in the owned `gui/mapview.h`, in the live
  call tree. Sole caller `VIBE_MapView_PanelDispatcher @0x5441d0`, whose render half
  is reconstructed in `src/play/map_view.cpp` (not owned by this agent). **Handoff:**
  during the map-panel build, call
  `gui::MapView_AddCornerObjects(win, gfxBase, cursorX, cursorY)` (the four corner
  decorations placed-then-z-removed).
- **Coord_Transform** — pure leaf; wired into any of its 12+ widget-layout callers via
  the raw-pointer overload (`gui/window.cpp` object/slider code). Header in the live
  tree.
- **Resource_FlushAndFree** — callers `Vfs_CloseAndFreeEntry @0x5d90c0`,
  `File_FlushAllStreams @0x5fe464`; **handoff:** route both through the
  `ResourceFlushHooks` when the deep `Resource_FreeEntryData`/`File_FreeStream`
  file-subsystem lands.
- **Cutscene Set/GetRandSeed** — `sim::g_cutsceneRandSeed` is the shared snapshot word
  for the cutscene step (cutscene.h / combat.h); the cutscene-process step
  (`cutscene_process.cpp`) snapshots/restores through it. Free functions in the live
  tree.
- **Input_SetIconTextById** — **handoff:** call from the markup builder
  (`Window_ParseMarkupAndBuild @0x416720`, `gui/markup_build.cpp`) and the widget drag
  handler (`Widget_ProcessMouseDrag @0x420db4`, `gui/gui_dialogs4.cpp`) at the cited
  sites, with the object's edit buffer + underlying record + a Property_Get measure.
- **BeginDragText** — **handoff:** call from the drag-start path that previously set
  `byte_61D9C8` (the pending drag-text source), wiring the four leaves to their live
  targets via `DragTextHooks`.

---

## Files

New: `src/render/coord_transform_leaf.{h,cpp}` (Coord_Transform + Resource_FlushAndFree),
`src/sim/cutscene_rand.{h,cpp}`, `src/play/input_icon_text.{h,cpp}`,
`src/gui/dragtext.{h,cpp}`, `tests/unit/smallleaves_wave22_test.cpp`.
Edited (owned): `src/gui/mapview.{h,cpp}` (added MapView_AddCornerObjects + fixed the
corner-table call-arg comments to match the disasm `(win, y, x, gfx)`).

No symbols redefined (grepped first; reused `gui::Object_AddToWindow`,
`gui::ZOrder_RemoveObject`, `util::MemMove`, `crt::Sprintf` via extern/include). No
edits to `progress/INDEX.md`, `wiring.cpp`, or any non-owned source. No commits.
