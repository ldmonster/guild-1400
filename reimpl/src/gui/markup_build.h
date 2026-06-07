#pragma once
// guild::gui — the BUILD-INTO-OBJECTS half of VIBE_Window_ParseMarkupAndBuild
//              @0x416720 (the half deferred by gui/text/markup.cpp's tokenizer).
//
// The original is one 8 KB goto-laden function that does THREE things at once:
//   (1) tokenize the markup string        -> done in gui/text/markup.cpp (TokenizeMarkup)
//   (2) lay out / glyph-blit the text      -> renderer half (Coord_Transform /
//                                              Animation_Basic / LayoutScrollContent),
//                                              forward-declared edges, DEFERRED.
//   (3) spawn the inline CHILD WIDGETS the markup requests, by calling the
//       widget-create leaves -> THIS MODULE.
//
// Recovered token -> widget-create mapping (the cases in the big v15 switch that
// actually create objects on the window; all leaves are REUSED from widget_create.h):
//
//   "$ia[label]" / "$in[label]"   red inline button sprite
//        -> Widget_CreateSprite(_BUTTON_RED gfx) + GameObject_AttachToWindow
//        -> if "[label]" present: Object_RecomputeSize(sprite, label)
//        -> "$in" additionally sets the sprite's radio flag (+444 |= 0x10)
//
//   "$i<sel>"  (sel = pending embedded object id in the window's slot table)
//        -> Object_AddToWindow(win, ..., objId)
//        -> selector 'c' => clickable button (+68=1,+72=0)
//                    'i' => plain         (+68=0,+72=0)
//                    'b' => toggle button (+72=1,+68=0)
//                    't'/'a'/'n' + "[label]" => Object_RecomputeSize(label)
//
//   "$t"   numeric input field   -> Input_AddFieldToWindow(..., 16, 0x82)
//   "$tt"  icon/text input field -> Input_AddFieldToWindow(...,  8, 0x10)
//   "$mt"-style trailing field   -> Input_AddFieldToWindow(..., 0, 1)   (no-arg field)
//
//   "$F[F]" font / colour, "$L/$R/$B/$Y" column align, "$A"/"$<"/"$="/"$M"/"$T"/"$C"
//        affect the text cursor + columns only (no child widget) and are tracked as
//        layout state; the glyph emission is the deferred renderer half.
//
// What this module produces is the SET OF CHILD OBJECTS a marked-up string adds to a
// window — which is exactly what a screen builder relies on and what the e2e test
// verifies. The pixel layout of the surrounding text is not reproduced here.

#include "gui/types.h"

#include <vector>

namespace guild::gui {

// Result record: one child object the markup created on the window, in creation
// order. Mirrors what the original leaves return (a widget slot index) plus enough
// classification to assert against a reference tree.
enum class MarkupObjectKind {
    RedButton,   // $ia / $in  (sprite via _BUTTON_RED)
    Embedded,    // $i<sel>    (Object_AddToWindow of a pending object id)
    InputField,  // $t / $tt   (Input_AddFieldToWindow)
};

struct MarkupObject {
    MarkupObjectKind kind;
    int widgetIdx = -1;   // slot returned by the create leaf
    char selector = 0;    // the selector letter ('a','n','c','i','b','t', or 0)
    int  objId    = -1;   // for Embedded: the pending object id consumed
    bool radio    = false; // $in radio flag
};

// gilde.exe 0x416720 — VIBE_Window_ParseMarkupAndBuild (object-build half).
//
// Walks the markup in `str` and creates the inline child widgets it requests on
// window `winSlot`, in order, reusing the widget-create leaves. `pendingObjectIds`
// supplies the embedded-object ids the "$i<sel>" cases consume (in order; the
// original reads them from the window's per-object id slots seeded by the screen
// builder). Returns the created objects in creation order.
//
// The text cursor (dword_69FFA8 x / dword_69FFAC y) starts at the window content
// origin and advances by each created widget's width, exactly as the original; the
// glyph-blit / line-wrap / scroll-content layout is the deferred renderer half.
std::vector<MarkupObject> BuildMarkupIntoWindow(
    int winSlot, const char* str,
    const std::vector<int>& pendingObjectIds = {});

// Property id for the inline red button graphic ("_BUTTON_RED"); resolved via
// Property_Validate at build time in the original. Exposed so tests can seed the
// gfx metric for the created sprite. (-1 until first resolved.)
extern int g_buttonRedGfx; // VIBE_Property_Validate(aButtonRed)

// Reset module-local state (the cached _BUTTON_RED id). Folded into ResetGuiState by
// callers; provided for test isolation.
void ResetMarkupBuild();

} // namespace guild::gui
