#include "gui/hud_draw.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Scroll arrows — gilde.exe 0x4bbd34.
// The original computes a per-arrow "code" (0=off, 1=idle, 2=moving) from the scroll
// state + the settled checks, then for each nonzero code calls VIBE_Animation_Basic
// with the move/idle gfx.  We reproduce the code derivation:
//   vScroll == -1  -> up arrow active   (v1)
//   vScroll == +1  -> down arrow active (v13)
//   vScroll ==  0  -> both up+down
//   hScroll == -1  -> left arrow active (v12)
//   hScroll == +1  -> right arrow active(v3)
//   hScroll ==  0  -> both left+right
// "settled" (the VectorWithinTolerance true) => idle sprite (code 1); else moving (2).
// ---------------------------------------------------------------------------
namespace {
// Build a placement if `active`; code 2 (moving) when not settled, 1 (idle) when
// settled.  In the original, code 1 maps to the idle gfx, code 2 to the move gfx.
void MaybeArrow(std::vector<ArrowPlacement>& out, bool active, bool settled,
                Arrow arrow, int x, int y, int moveGfx, int idleGfx) {
    if (!active)
        return;
    bool moving = !settled;
    out.push_back({arrow, x, y, moving ? moveGfx : idleGfx, moving});
}
} // namespace

std::vector<ArrowPlacement> Hud_ScrollArrowPlacements(
    const ScreenEdges& edges, int vScroll, int hScroll,
    bool leftSettled, bool downSettled, bool rightSettled, bool upSettled) {
    std::vector<ArrowPlacement> out;

    // Vertical: -1 => up only, +1 => down only, 0 => both.
    bool upActive   = (vScroll == -1) || (vScroll == 0);
    bool downActive = (vScroll == 1)  || (vScroll == 0);
    // Horizontal: -1 => left only, +1 => right only, 0 => both.
    bool leftActive  = (hScroll == -1) || (hScroll == 0);
    bool rightActive = (hScroll == 1)  || (hScroll == 0);

    // left arrow: (rightX-16, centerY-32), gfx 0/1, settled@+252
    MaybeArrow(out, leftActive, leftSettled, Arrow::kLeft,
               edges.rightX - kArrowRightInsetX, edges.centerY - kArrowLeftCenterDY,
               kArrowLeftMoveGfx, kArrowLeftIdleGfx);
    // down arrow: (4, bottomY), gfx 2/3, settled@+180
    MaybeArrow(out, downActive, downSettled, Arrow::kDown,
               kArrowDownInsetX, edges.bottomY,
               kArrowDownMoveGfx, kArrowDownIdleGfx);
    // right arrow: (rightX2-150, bottomY), gfx 4/5, settled@+204
    MaybeArrow(out, rightActive, rightSettled, Arrow::kRight,
               edges.rightX2 - kArrowRight2InsetX, edges.bottomY,
               kArrowRightMoveGfx, kArrowRightIdleGfx);
    // up arrow: (rightX-16, 56), gfx 6/7, settled@+228
    MaybeArrow(out, upActive, upSettled, Arrow::kUp,
               edges.rightX - kArrowRightInsetX, kArrowUpY,
               kArrowUpMoveGfx, kArrowUpIdleGfx);
    return out;
}

// ---------------------------------------------------------------------------
// Object-action panel — gilde.exe 0x4bd0d4.
//   AddToWindow(win, 9, 599, 1221)           frame right
//   centre: hasSubject? subjectKind+1010 : 1221/1039
//   AddToWindow(win, 9, 11, centre)
//   right block: actionGfx==1709 special row, else the standard 4-button block.
// ---------------------------------------------------------------------------
std::vector<ActionButton> Hud_BuildObjectActionPanel(int actionGfx, bool hasSubject,
                                                     int subjectKind) {
    std::vector<ActionButton> out;

    // Top frame sprite (always).
    out.push_back({9, 599, kActionFrameGfx});

    // Left/centre id (a3): if a3 already set, keep; else subject? kind+1010 : 1221.
    int centre;
    if (hasSubject)
        centre = kActionFrameGfx;          // a3 nonzero path leaves a3 as-is (caller's)
    else if (subjectKind != 0)
        centre = subjectKind + kActionSubjectGfxBase;
    else
        centre = kActionFrameGfx;          // 1221
    out.push_back({9, 11, centre});

    // Right id (v9): subject? kind+1010 : 1039 ; the 1709 row inserts an extra frame.
    int rightX = 599;                      // v8
    if (actionGfx == kActionRowInteraction) {
        out.push_back({9, 491, kActionFrameGfx}); // extra frame at x=491
        rightX = 491;
    }
    int rightGfx = (subjectKind != 0) ? (subjectKind + kActionSubjectGfxBase)
                                      : kActionDefaultRightGfx;
    out.push_back({9, rightX, rightGfx});

    // Action button block (v18 = 0 here; the original derives x from v19 hi-word = 0).
    int v17 = actionGfx + 2;
    if (actionGfx == kActionRowInteraction) {
        out.push_back({0,   0, kActionRowInteraction}); // (0, x, 1709)
        out.push_back({67,  0, v17});                   // (67, x, a2+2)
        out.push_back({67,  503, actionGfx + 3});       // (67, 503, a2+3)
        out.push_back({424, 0, actionGfx + 1});         // (424, x, a2+1)
    } else {
        out.push_back({0,   0, actionGfx});             // (0, x, a2)
        out.push_back({400, 0, actionGfx + 1});         // (400, x, a2+1)
        out.push_back({77,  0, v17});                   // (77, x, a2+2)
        out.push_back({77,  583, actionGfx + 3});       // (77, 583, a2+3)
    }
    return out;
}

// ---------------------------------------------------------------------------
// Drag-drop / tutorial click state machine — gilde.exe 0x59542c.
// ---------------------------------------------------------------------------
namespace {
DragCommandSink g_defaultDragSink;
DragCommandSink* g_dragSink = &g_defaultDragSink;
} // namespace

void Hud_SetDragCommandSink(DragCommandSink* sink) {
    g_dragSink = sink ? sink : &g_defaultDragSink;
}

DragResult Hud_ProcessDragClick(int state, bool hasNextForm) {
    DragResult r{state, DragCmd::kNone};
    switch (state) {
        case 1: // arm: show step, advance to 2
            r.cmd = DragCmd::kShowStep;
            r.nextState = 2;
            break;
        case 2: // confirm -> 3 (slot click)
            r.cmd = DragCmd::kSlotClick;
            r.nextState = 3;
            break;
        case 3: // confirm -> 4 (slot click)
            r.cmd = DragCmd::kSlotClick;
            r.nextState = 4;
            break;
        case 4: // reminder re-arm; stays 4 (or 5 when the step advances)
            r.cmd = DragCmd::kReminderShow;
            r.nextState = 4;
            break;
        case 5: // hide reminder -> 6 (next form) or 7 (no next form)
            r.cmd = DragCmd::kReminderHide;
            r.nextState = hasNextForm ? 6 : 7;
            break;
        case 6: // advance forms; loops 6 until exhausted, then 7
            r.cmd = DragCmd::kAdvanceForms;
            r.nextState = hasNextForm ? 6 : 7;
            break;
        case 7: // advance forms; -> 9 (has done-panel form) or 10 (none)
            r.cmd = DragCmd::kAdvanceForms;
            r.nextState = hasNextForm ? 9 : 10;
            break;
        case 9: // show done panel; stays 9
            r.cmd = DragCmd::kDonePanel;
            r.nextState = 9;
            break;
        case 10: // status banner; -> 11 (edge scroll arm)
            r.cmd = DragCmd::kStatusBanner;
            r.nextState = 11;
            break;
        case 11: // status banner; -> 12
            r.cmd = DragCmd::kStatusBanner;
            r.nextState = 12;
            break;
        case 12: // status banner; loops back to 1 (re-arm next step)
            r.cmd = DragCmd::kStatusBanner;
            r.nextState = 1;
            break;
        default:
            r.cmd = DragCmd::kNone;
            r.nextState = state;
            break;
    }
    g_dragSink->Fire(r.cmd);
    return r;
}

// ---------------------------------------------------------------------------
// Selected-unit caption base id — gilde.exe 0x4bae88 (the v10 switch).
// ---------------------------------------------------------------------------
int Hud_SelectedUnitCaptionBase(int categoryByte) {
    unsigned v10 = (unsigned)(categoryByte & 0xFF);
    if (v10 < 0x43u) {
        if (v10 < 0x1Au) {
            if (v10 >= 0x18u) {
                if (v10 > 0x18u)
                    return 3774; // 25: building FindById
                return 3780;     // 24: person v9[49]
            }
            if (v10 == 20)
                return 3768;     // 20
            return -1;           // LABEL_54
        }
        if (v10 <= 0x1Au)
            return 3801;         // 26: person v9[45]
        if (v10 < 0x3Cu) {
            if (v10 == 40)
                return 3759;     // 40
            return -1;
        }
        if (v10 <= 0x3Cu)
            return 3765;         // 60: person v9[43]
        if (v10 < 0x3Fu)
            return -1;           // 61,62
        if (v10 > 0x3Fu) {
            if (v10 == 64)
                return 3783;     // 64: building FindById
            return -1;
        }
        return 3762;             // 63 (==0x3F): building FindById (LABEL_64)
    }
    if (v10 <= 0x43u)
        return 3786;             // 65,66,67
    if (v10 < 0x61u) {
        if (v10 >= 0x48u) {
            if (v10 <= 0x48u)
                return 3792;     // 72
            if (v10 == 73)
                return 3756;     // 73: building FindById
            return -1;
        }
        if (v10 == 68)
            return 3771;         // 68: building FindById
        return -1;
    }
    if (v10 <= 0x61u)
        return 3789;             // 97
    if (v10 >= 0x64u) {
        if (v10 <= 0x64u)
            return 3795;         // 100: building v9[44]
        if (v10 <= 0x65u)
            return 3798;         // 101
        if (v10 == 117)
            return 3762;         // 117: building FindById (LABEL_64)
        return -1;
    }
    if (v10 == 98)
        return 3777;             // 98
    return -1;                   // LABEL_54
}

} // namespace guild::gui
