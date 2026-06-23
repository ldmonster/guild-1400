# Wave-22 — GUI widget/object/state/property mini-cluster (W22-GUICORE)

**Agent:** W22-GUICORE · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the GUI-object layout + object-state state-machine + property/text-width
accessors used by the per-frame entity render (the wave-21 entity core / render_submit
reaches all of them). All addresses are `gilde.exe` (imagebase 0x400000).

## Modules I own (new)

- `src/gui/widget_layout.{h,cpp}` — `Widget_LayoutBounds`, `Property_Get`,
  `State_GetCurrent`, `Coord_Transform`.
- `src/gui/gui_object_state.{h,cpp}` — `State_Update`, `ResolveObjectState`,
  `MarkObjectUsed`.
- `tests/unit/gui_widget_layout_test.cpp` — 75 checks, 0 failures.

## Reconstructed (body in src, 1:1 from the Hex-Rays decompile)

| addr | bytes | name | module | notes |
|------|------:|------|--------|-------|
| 0x413220 | 719 | VIBE_Widget_LayoutBounds | widget_layout.cpp | recursive widget bounds; clamps against parent (+44 link) + screen extents (dword_69FFB8/BC); per-type dispatch ('@' window child-recursion, 'A'/4 anchor tables) |
| 0x4152cc | 159 | VIBE_Property_Get | widget_layout.cpp | text pixel-width: State_Update(font) + per-glyph Coord_Transform; '~' skip; kern (+22) + advance (+26); spacing dword_62D274(=2)/dword_62D270(=8) |
| 0x40e728 | 237 | VIBE_State_GetCurrent | widget_layout.cpp | dirty-rect clamp+append; odd-x even-align; clip dword_64A1B4/B8/BC/C0; scroll window dword_69FF80/84/88/8C; 16-byte rects [x,y,w,h]; 512 cap |
| 0x5d8b00 | (leaf) | VIBE_Coord_Transform | widget_layout.cpp | pure leaf: `if(rec) rec += *(u32*)(rec+4*idx+69)`; byte-exact unaligned +69 read |
| 0x40e9e8 | 262 | VIBE_State_Update | gui_object_state.cpp | realise record state, 5/8-pair redirect to +76 partner, grid offset (flag68 bit2 + byte_62D220), publishes redirectDelta (dword_62D2A4) |
| 0x40eaf0 | 277 | VIBE_Gui_ResolveObjectState | gui_object_state.cpp | same redirect logic; out = (record[v6].stateHandle, v3); preserves the v6=a1-vs-v3-adjusted quirk in the non-redirect branch |
| 0x412ea4 | 113 | VIBE_Gui_MarkObjectUsed | gui_object_state.cpp | resolve 5/8 pair (only when loadOffset==0; NO grid-offset test), ++useCount(+64), frameStamp(+72)=dword_62EB38 |

## Genuine leaf / boundary kept as injected hook (rule 6/8 — NOT faked)

- **VIBE_State_Helper @0x40e014** (the realise step `d2_LoadObj`): on `record[+52]==0` it
  evicts LRU (`Resource_EvictOldestEntry`), allocs `record[+56]` bytes
  (`Memory_AllocDebug`), opens the VFS stream (`File_OpenStream`), seeks to `record[+48]`,
  reads, (`kind<16` =>) converts via `ShapeBank_ConvertNew`, accounts size, closes
  (`Vfs_CloseAndFreeEntry`). This is a genuine **VFS/resource boundary** — modeled as
  `StateContext::realiseHook`. A bound hook MUST set `record[idx].stateHandle` (+52)
  nonzero on success (the three callers gate on it). It already has a body cited
  elsewhere in `src/play/session_*` / `src/render/shape_recon_cluster.cpp` as the
  d2_LoadObj path; this cluster intentionally does not re-reconstruct it (ODR).

## Fixed-point / ConvertX audit

Every clip comparison in LayoutBounds / State_GetCurrent is an integer `>> 16` on a
signed 16.16-fixed dword followed by an integer compare — **no float, no ConvertX, no
fistp** is involved, so there is no rounding/truncation subtlety. The shifts are
reproduced exactly (arithmetic `>>` on `int`). The widget bound fields (+28..+34) are
read by the original at OVERLAPPING / unaligned dword offsets (e.g. `*(int*)(v4+30)>>16`);
these are reproduced byte-exactly via `Widget::ld<i32>(off)` (memcpy-backed unaligned
load), matching the x86 unaligned `mov`. Verified field offsets, table strides and
default globals with `disasm`/`get_global_value`:

- widget array dword_69FFB4 (740 stride) = `guild::gui::g_widgets` (REUSED).
- owning Window array dword_67EB80 = 952-byte (238-dword) stride (`shl3;×8;−;<<4;+` = ×952).
- anchor tables: dword_695084/695088 = 87-dword (348-byte) stride; word_140642D/F = 17-word
  (34-byte) stride; indexed by widget +116 (ownerWindow).
- defaults: dword_64A1BC=0x280(640), dword_64A1C0=0x1E0(480), dword_62D274=2,
  dword_62D270=8; the rest are BSS-zero (set at display init).

## Wiring (rule 13)

These seven are the exact bodies of the sibling-leaf hooks `src/app/render_submit.cpp`
already calls by name+address (`RenderSubmitHooks::resolveObjectState` @0x40eaf0,
`markObjectUsed` @0x412ea4, `stateUpdate` @0x40e9e8, `stateGetCurrent` @0x40e728), and
the text-width hook the GUI window/markup builders call (`Property_Get` @0x4152cc).

`render_submit.h`/`.cpp` are owned by the app/render-submit agent and use a flat-hook
signature (`int(*)(int idx,int* outState,int* outIndex)` etc.) rather than the
`StateContext&` body signature here. **Handoff (one line):** the render-submit / entity
core binds its hooks to a thin adapter that owns a single `guild::gui::StateContext`
(records=dword_62D204, gridOffset=byte_62D220, frameStamp=dword_62EB38, redirectDelta→
dword_62D2A4, realiseHook=the d2_LoadObj VFS load) and forwards
`resolveObjectState/markObjectUsed/stateUpdate` to `guild::gui::{ResolveObjectState,
MarkObjectUsed,StateUpdate}`, and a `guild::gui::DirtyRectState` (the dword_62D2D0/2E0
bucket the active dword_62D580 selects) forwarding `stateGetCurrent` to
`guild::gui::StateGetCurrent`. No file I own needs editing for that bind; the bodies are
live in `libguild` and the adapter is a ~10-line shim in the render-submit owner's TU.

## Tests (golden-pinned, no assets)

`tests/unit/gui_widget_layout_test.cpp` (75 checks):
- Coord_Transform: null/inert/+69 offset add.
- Property_Get: monospace width, single char, empty (=letterSpacing), '~' skip, space extra.
- State_GetCurrent: even append, odd-x align (w+=2, x--, w&=~1), bottom/right clamps,
  scroll-window skip vs poke-outside, 512 hard cap (early return).
- State_Update: simple realise, already-realised no-op, 5/8 redirect (delta=v−partner),
  loadOffset!=0 non-redirect, grid offset (flag68 bit2 + gridOffset).
- ResolveObjectState: non-redirect outputs (v6=a1, v3), redirect outputs + delta.
- MarkObjectUsed: bump+stamp, pair-redirect bump partner, loadOffset!=0 bump self.
- Widget_LayoutBounds: '@' bound writes (no parent, 640x480 extents), else-branch
  screenExt defaults, type-4 anchor, '@' window child-recursion (origin stamp + child
  relayout = newOrigin + child.local − oldOrigin).

## Build

`cmake --build build --target guild` green; `gui_widget_layout_test` 75/0;
`app_render_submit_test` 30/0 (sibling, unaffected).
