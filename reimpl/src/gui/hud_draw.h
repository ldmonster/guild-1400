#pragma once
// guild::gui — HUD overlay draw-data leaves: scroll arrows, the object-action button
// panel, the drag-drop / tutorial click state machine, and the selected-unit info
// text-id selection.  All recover the DATA/LAYOUT/STATE half byte-for-byte; the glyph
// blit, the 3D animation push, and the world/scene mutation are forward-declared and
// routed through mock command sinks.

#include "gui/types.h"
#include <vector>

namespace guild::gui {

// ===========================================================================
// Scroll arrows — gilde.exe 0x4bbd34 (VIBE_Hud_DrawScrollArrows).
// The HUD draws up-to-four edge arrows (left / down / right / up) when the camera can
// scroll in that direction.  Which arrows are lit is driven by the scroll-direction
// state (dword_6316CC = vertical: -1 up-ok / +1 down-ok / 0 both; dword_6316D0 =
// horizontal: -1 left-ok / +1 right-ok / 0 both) plus a per-arrow "settled" check.
//
// The four arrows are placed relative to the screen edges (the original reads
// dword_69FFA0 = bottom y, dword_69FFA4 = right x, dword_69FFB8>>16 = a center y,
// dword_69FFBC>>16 = right x2).  We recover the fixed offsets and the gfx-pair table.
// ===========================================================================
enum class Arrow { kLeft, kDown, kRight, kUp };

// Per-arrow sprite pair: index 0 = "moving/highlighted" (the *2 case), 1 = "idle".
// gilde.exe: VIBE_Animation_Basic(x, y, frame, ctx, gfxId).
//   left : gfx 0 (move) / 1 (idle)   at (rightX-16, centerY-32)
//   down : gfx 2 / 3                 at (4, bottomY)
//   right: gfx 4 / 5                 at (rightX2-150, bottomY)
//   up   : gfx 6 / 7                 at (rightX-16, 56)
inline constexpr int kArrowLeftMoveGfx  = 0, kArrowLeftIdleGfx  = 1;
inline constexpr int kArrowDownMoveGfx  = 2, kArrowDownIdleGfx  = 3;
inline constexpr int kArrowRightMoveGfx = 4, kArrowRightIdleGfx = 5;
inline constexpr int kArrowUpMoveGfx    = 6, kArrowUpIdleGfx    = 7;

inline constexpr int kArrowRightInsetX = 16;   // rightX - 16 (left + up arrows)
inline constexpr int kArrowLeftCenterDY = 32;  // centerY - 32 (left arrow)
inline constexpr int kArrowDownInsetX  = 4;    // x = 4        (down arrow)
inline constexpr int kArrowRight2InsetX = 150; // rightX2 - 150 (right arrow)
inline constexpr int kArrowUpY         = 56;   // up arrow y

struct ArrowPlacement {
    Arrow arrow;
    int   x;
    int   y;
    int   gfx;   // resolved sprite (move/idle)
    bool  moving;
};

// Screen-edge inputs (the dword_69FFA0/A4/B8/BC globals; supplied so the placement is
// testable without the renderer).
struct ScreenEdges {
    int bottomY;  // dword_69FFA0
    int rightX;   // dword_69FFA4
    int centerY;  // dword_69FFB8 >> 16
    int rightX2;  // dword_69FFBC >> 16
};

// gilde.exe 0x4bbd34 (the v1/v3/v12/v13 -> VIBE_Animation_Basic dispatch).  Given the
// vertical scroll state `vScroll` (dword_6316CC: -1/0/+1), the horizontal scroll state
// `hScroll` (dword_6316D0: -1/0/+1), and per-arrow "settled" flags (true => the camera
// has reached the limit so the arrow shows idle, matching the
// VIBE_Math_VectorWithinTolerance checks), return the list of arrows to draw with
// their resolved position + sprite.  `leftSettled/downSettled/rightSettled/upSettled`
// correspond to the four tolerance checks at +252/+180/+204/+228 of dword_13FCD1C.
std::vector<ArrowPlacement> Hud_ScrollArrowPlacements(
    const ScreenEdges& edges, int vScroll, int hScroll,
    bool leftSettled, bool downSettled, bool rightSettled, bool upSettled);

// ===========================================================================
// Object-action button panel — gilde.exe 0x4bd0d4 (VIBE_Hud_BuildObjectActionPanel).
// The bottom HUD action bar: a backdrop + 1..N action buttons.  The original places a
// fixed frame (gfx 1221 at x=599 and x=11) then 4-5 action buttons whose y/x depend on
// the action group `a2` (1709 => the special "interaction" row).  We recover the
// placement table.
// ===========================================================================
struct ActionButton { int y; int x; int gfx; };

// gilde.exe 0x4bd0d4.  Build the action-panel button list for action group `actionGfx`
// (a2), where `hasSubject` (a3 != 0) and `subjectKind` (the *selected* byte, 0 when no
// subject) influence the centre button id (subjectKind + 1010, else 1221/1039).
std::vector<ActionButton> Hud_BuildObjectActionPanel(int actionGfx, bool hasSubject,
                                                     int subjectKind);

// The special interaction-row action id.
inline constexpr int kActionRowInteraction = 1709;
inline constexpr int kActionFrameGfx       = 1221; // backdrop frame sprite
inline constexpr int kActionSubjectGfxBase = 1010; // subjectKind + 1010
inline constexpr int kActionDefaultRightGfx = 1039;

// ===========================================================================
// Drag-drop / tutorial click state machine — gilde.exe 0x59542c
// (VIBE_Hud_ProcessDragDropClick).
// The tutorial "drag the cursor here" panels are a small state machine driven by a
// per-step state byte (off_5953F0[0], values 1..0xC).  A click resolves the next state
// and which tutorial command fires.  We recover the STATE TRANSITION + COMMAND
// selection; the panel rendering / voice playback are routed through the sink.
// ===========================================================================
enum class DragCmd {
    kNone,
    kSlotClick,     // VIBE_EventPanel_HandleSlotClick
    kShowStep,      // VIBE_Tutorial_ShowStepWithVoice
    kAdvanceForms,  // VIBE_Tutorial_AdvanceStepForms
    kReminderShow,  // VIBE_Tutorial_ShowReminderPanel
    kReminderHide,  // VIBE_Tutorial_HideReminderPanel
    kDonePanel,     // VIBE_Tutorial_ShowDonePanel
    kStatusBanner,  // VIBE_Hud_SetStatusBannerText
};

struct DragResult {
    int     nextState; // the new off_5953F0[0] value
    DragCmd cmd;       // the command fired (mock)
};

struct DragCommandSink {
    virtual ~DragCommandSink() = default;
    virtual void Fire(DragCmd) {}
};
void Hud_SetDragCommandSink(DragCommandSink* sink);

// gilde.exe 0x59542c (the switch over *off_5953F0).  Given the current state byte
// `state` (1..0xC) and whether the tutorial step has a follow-up (`hasNextForm` for
// states that branch on *(step+80) etc.), return the next state + command and fire it
// on the sink.  This models the deterministic transition skeleton; the time-gated
// re-arm branches (which compare dword_62EB38) are taken as "fire" here.
DragResult Hud_ProcessDragClick(int state, bool hasNextForm);

// ===========================================================================
// Selected-unit info text id — gilde.exe 0x4bae88 (VIBE_Hud_DrawSelectedUnitInfo).
// The floating "what is this" caption over a selected object chooses a localized text
// id from the object's category byte (v10 at building+384) plus a 0..2 random variant.
// We recover the category -> base-text-id mapping table byte-for-byte (the random
// addend 0..2 is added by the caller).
// ===========================================================================
// Returns the base text id for an object category byte, or -1 when the category has no
// caption (falls through to LABEL_54 in the original).  The caller adds RandomModulo(3).
int Hud_SelectedUnitCaptionBase(int categoryByte);

} // namespace guild::gui
