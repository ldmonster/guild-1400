#pragma once
// guild::gui — window render-update bookkeeping (the DATA half of the per-frame
//              flush + the scroll/sprite reflow leaves).
//
//   VIBE_Window_RenderUpdates        @0x4146ac  per-frame dirty-flush dispatch.
//   VIBE_Window_ApplyScrollOffset    @0x4163bc  step the scroll position toward its
//                                               target + reflow children to it.
//   VIBE_Window_NormalizeSpriteWidths@0x416658  equalise all sprite children to the
//                                               widest (so a column of icons aligns).
//
// RenderUpdates is a thin renderer dispatcher (DispatchInteractions / scene-blit /
// PresentFrame) with no GUI data-model state of its own; it is reconstructed here as
// the ordered sequence of edges so the call graph stays faithful, with each renderer
// leaf forward-declared (and stubbed in tests). ApplyScrollOffset and
// NormalizeSpriteWidths ARE pure data-model functions over the Window/Widget records
// and are translated byte-for-byte.
//
// Scroll-state Window fields (dword indices off the 952-byte record; the originals
// address them as v1[145..151] and a step byte at +608):
//   +580 dword[145]  scrollMaxY   (vertical content extent / clamp)
//   +584 dword[146]  scrollY      (current vertical scroll)
//   +588 dword[147]  scrollPrevY  (last applied vertical scroll)
//   +592 dword[148]  scrollVelY   (>0 scroll down, <0 scroll up, decays to 0)
//   +596 dword[149]  scrollVelX   (horizontal velocity)
//   +600 dword[150]  scrollX      (current horizontal scroll)
//   +604 dword[151]  scrollPrevX  (last applied horizontal scroll)
//   +608 byte        scrollStep   (per-tick pixel step)

#include "gui/types.h"

namespace guild::gui {

// ===== Forward-declared renderer / layout edges (stubbed in tests) =========
// gilde.exe 0x413220 — VIBE_Widget_LayoutBounds: recompute a widget's clip bounds
// from a new (x,y) origin. Renderer-side geometry; the model reflow only needs to
// invoke it once per child after a scroll step.
void Widget_LayoutBounds(int x, int y, int widgetIdx);
// gilde.exe 0x412480 — VIBE_Widget_RefreshText: re-measure/redraw a widget's text
// (used after NormalizeSpriteWidths forces a new width). Renderer edge.
void Widget_RefreshText(int widgetIdx);
// VIBE_Window_RenderUpdates renderer edges (renderer cluster):
void GameObject_DispatchInteractions(); // 0x40e6c0
int  DecompressState_Blob();            // 0x423500  (returns non-zero if a frame is pending)
void GameLogic_Interactions();          // 0x4139a8
void Decompression_Finalize();          // 0x4235dc
int  Render_PresentFrame();             // 0x4349e4

// gilde.exe 0x4146ac — VIBE_Window_RenderUpdates ()
// Per-frame flush: dispatch pending widget interactions, and if a frame is pending
// run the interaction logic, finalise, and present. Returns the present result (0
// when nothing was pending). The body is renderer edges; reconstructed for call-graph
// fidelity.
int Window_RenderUpdates();

// gilde.exe 0x4163bc — VIBE_Window_ApplyScrollOffset (winSlot@eax)
// Steps the window's vertical and horizontal scroll positions one `scrollStep`
// toward their velocity targets (clamping at 0 and scrollMaxY), and — when the
// applied scroll changed since last frame — reflows every non-sprite child's clip
// bounds (Widget_LayoutBounds) to the new offset. Returns the new scrollX.
int Window_ApplyScrollOffset(int winSlot);

// gilde.exe 0x416658 — VIBE_Window_NormalizeSpriteWidths (winSlot@eax)
// Scans the window's children for type-9 sprites, finds the widest (+8 pad, min
// 128), then sets every sprite child to that width (+88 "width overridden" flag and
// Widget_RefreshText). Used by the markup builder so a column of inline icons lines
// up. No return (void in the original; the trailing JUMPOUT rejoins the caller).
void Window_NormalizeSpriteWidths(int winSlot);

} // namespace guild::gui
