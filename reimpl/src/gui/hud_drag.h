#pragma once
// guild::gui — HUD drag-mode flag and the object-highlight toggle.
//
//   * VIBE_Hud_EnableDragMode  @0x595b8c / VIBE_Hud_DisableDragMode @0x595b80 —
//     set/clear the global drag-mode flag (dword_649CD4) the HUD click router reads
//     to decide whether a press starts a drag.
//   * VIBE_Hud_ToggleObjectHighlight @0x4bd008 — push/pop the "owned-object" render
//     highlight across every widget that belongs to an active form.  Turning the
//     highlight ON walks all loaded forms and, for each child widget that points at
//     that form's record (widget +60) and is not already lit (widget +52 == 0), sets
//     the lit flag (+52 = 1) and remembers the widget's local slot in a list
//     (word_11BC35C[count++]).  Turning it OFF replays the remembered list and clears
//     each widget's lit flag.  dword_631E5C holds the remembered count.
//
// The original stores a *pointer to the form record* in widget +60 and compares it to
// the form base address.  We model that link by the form index (0..kHudFormCount-1)
// stored in the widget's parentClip(+60) field, which is byte-identical storage; the
// highlight then matches `parentClip == formIndex+1` (the original's nonzero pointer).
// A form is "active" when both its valid flag (dword_676BF0 view) and its dword_676C00
// view are set.

#include "gui/types.h"

namespace guild::gui {

// ---- HUD drag-mode flag (dword_649CD4) ------------------------------------
extern i32 g_hudDragMode; // dword_649CD4 (1 while a drag is in progress)

// gilde.exe 0x595b8c — VIBE_Hud_EnableDragMode.
void Hud_EnableDragMode();
// gilde.exe 0x595b80 — VIBE_Hud_DisableDragMode.
void Hud_DisableDragMode();

// ---- Object-highlight list ------------------------------------------------
// The original loops forms with i = 0,171,342,..,8037 (i != 8208), i.e. 48 forms.
inline constexpr int kHudFormCount     = 48;   // 8208 / 171
inline constexpr int kHudHighlightMax  = 512;  // word_11BC35C capacity (widgets scanned per form)

extern i16 g_highlightList[kHudHighlightMax]; // word_11BC35C (remembered local widget slots)
extern i32 g_highlightCount;                   // dword_631E5C

void ResetHudHighlight();

// The two per-form "active" flag views the original tests (676BF0 / 676C00).
// Each is a 48-entry table addressed with the same 171-dword stride as the forms.
extern i32 g_formActiveA[kHudFormCount]; // dword_676BF0[171*formId]  (valid flag)
extern i32 g_formActiveB[kHudFormCount]; // dword_676C00[171*formId]

// gilde.exe 0x4bd008 — VIBE_Hud_ToggleObjectHighlight  (eax = result@eax, in: result).
// `on` != 0 lights the owned-object highlight; `on` == 0 clears the previously-lit
// set.  Returns the original `result` (the eax it was called with, then mutated as a
// loop counter); the meaningful side effects are on g_highlightList / g_highlightCount
// and the widgets' +52 lit flags.  See header note on the +60 form link model.
i32 Hud_ToggleObjectHighlight(i32 on);

} // namespace guild::gui
