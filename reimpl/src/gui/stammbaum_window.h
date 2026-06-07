#pragma once
// guild::gui — VIBE_Stammbaum_RunFamilyTreeWindow @0x55ab84.  The family-tree ("Stammbaum")
// window: a centred portrait of a person with their spouse, parents, children and siblings
// laid out around them, connected by drawn lines, each node clickable to recentre the tree.
//
//   form = "Special\Stammbaum";  SelectWindow(form,0) -> title (text arg `a3`, default 156);
//   SelectWindow(form,1) is the tree canvas; its pixel width `w = window.w >> 16` drives the
//   horizontal layout.  Recovered layout anchors (all in pixels):
//     center   = w / 2;                       // the focus person's column
//     sibLeftX = w/5 - 16;   sibRightX = 4*w/5;
//     sibTop   = w/5 + 16;   sibBot    = 4*w/5 + 16;
//   node width  `nw = node.w >> 16` (sprite metric), half = nw/2;
//   node height `nh = node.h >> 16`, used as a +5 / +3 vertical lead for lines/labels.
//
//   Per generation the node sprite ids are:  self/spouse row uses gfx 1402/1190 at scale
//   10/12; parents use 150/152; children use 290/292.  A "$P" empty-slot sprite (1190) is
//   placed where a relative is missing.  Each node also gets its person record stored at
//   widget+736 and a selectable flag (+444 = 3).
//
//   Node X placement around `center` (the load-bearing math, recovered byte-for-byte):
//     self           : center - (nw + 90);     portrait at +2
//     spouse         : center + 90;            portrait at +2  (only when married)
//     parents (2)    : left = center - (nw+16), right = center + 16   (when both present)
//     parent  (1)    : center - nw/2                                   (single parent)
//     children odd   : center - nw/2, then spread by +/- (nw+40) outward
//     children even  : center - nw - 20, center + 20, then +/- (nw+40) outward
//
//   modal loop: right-click exits; a click on any node's object recentres the tree on that
//   person (the loop re-runs the layout for the new focus).  Depending on the mode byte the
//   click may instead open that person's info window (VIBE_InfoPanel_RunPlayerInfoWindow).
//
// We recover the form name, the title default (156), the sprite ids + scales, the layout
// anchors and the node-X placement math, populating a synthetic family.  The frame loop,
// line/glyph blitter and person-record lookups are forward-declared / mocked.

#include "gui/types.h"

#include <vector>

namespace guild::gui {

inline constexpr const char* kFormStammbaum = "Special\\Stammbaum";

inline constexpr int kStammTitleSlot     = 0;
inline constexpr int kStammCanvasSlot    = 1;
inline constexpr int kStammTitleDefault  = 156; // RenderRichString(156) when arg a3 == 0

// Node sprite + scale ids (the AddToWindow gfx/scale arguments).
inline constexpr int kStammSelfGfx     = 1402; // self/spouse frame sprite
inline constexpr int kStammEmptyGfx    = 1190; // empty-slot ("$P") sprite
inline constexpr int kStammSelfScale   = 10;   // self/spouse scale
inline constexpr int kStammSelfPortr   = 12;   // self/spouse portrait scale
inline constexpr int kStammParentScale = 150;  // parent/sibling sprite scale
inline constexpr int kStammParentPortr = 152;  // parent/sibling portrait scale
inline constexpr int kStammChildScale  = 290;  // child sprite scale
inline constexpr int kStammChildPortr  = 292;  // child portrait scale

inline constexpr int kStammSelfGap = 90; // self/spouse horizontal gap from center
inline constexpr int kStammParentGap = 16; // parent inner gap from center
inline constexpr int kStammChildEvenGap = 20; // child even-count inner gap
inline constexpr int kStammChildSpread = 40;  // child outward spread addend

// Widget back-store offsets the original writes for each node (per types.h Widget).
//   +444 = 3  : "selectable node" radio flag
//   +736      : person-record pointer (stored so a click can re-centre)
inline constexpr int kStammNodeFlagOff = 444;
inline constexpr int kStammNodeRecOff  = 736;

// A person in the synthetic family graph.
struct FamilyPerson {
    int entity = 0;  // person record handle (0 = none / missing slot)
    int rank   = 0;  // used by some mode gates; carried through for completeness
};

// The family to lay out (focus + relatives).  Missing relatives have entity == 0.
struct Family {
    FamilyPerson focus;
    FamilyPerson spouse;        // entity 0 -> unmarried (single-parent layout for own gen)
    FamilyPerson father;        // a parent
    FamilyPerson mother;        // the spouse-side / second parent (drives 2-parent layout)
    std::vector<FamilyPerson> children; // up to 5
    std::vector<FamilyPerson> siblings; // up to 6 (3 left of focus, 3 right)
};

// A placed tree node.
struct TreeNode {
    int entity = 0;     // the person at this node (0 = empty slot)
    int x = 0;          // frame x (pixels, around center)
    int portraitX = 0;  // portrait x (= x + 2)
    int objectId = -1;  // the clickable object id
    int scale = 0;      // sprite scale id (10/150/290)
    bool empty = false; // true when no person -> 1190 placeholder
};

struct FamilyTreeLayout {
    const char* form = nullptr;
    int titleText = kStammTitleDefault;
    int canvasWidth = 0;  // w
    int center = 0;       // w/2
    int nodeWidth = 0;    // nw
    TreeNode self;
    TreeNode spouse;       // empty when unmarried
    std::vector<TreeNode> parents;  // 1 or 2
    std::vector<TreeNode> children; // 0..5
};

// Mode bits (the `a1`/v158 dl argument) controlling what a click does.
inline constexpr int kStammModeRecentre  = 0x01; // a node click re-centres the tree
inline constexpr int kStammModeInfoWindow = 0x08; // a node click opens the info window
inline constexpr int kStammModeForceInfo  = 0x10; // always open info (even on the focus)

// gilde.exe 0x55ab84 (layout half) — lay out the family tree for a focus person.
//   `canvasWidth` is the canvas window's pixel width (w); `nodeWidth` the node sprite width.
FamilyTreeLayout Stammbaum_BuildLayout(const Family& fam, int canvasWidth, int nodeWidth,
                                       int titleArg = 0);

// gilde.exe 0x55ab84 (wiring half) — resolve a click on a node to the person to act on.
//   Returns the entity id of the clicked node (focus/spouse/parent/child) or -1.  The
//   caller uses `mode` to decide recentre vs. info-window.
int Stammbaum_DispatchClick(const FamilyTreeLayout& l, int clickedObj);

} // namespace guild::gui
