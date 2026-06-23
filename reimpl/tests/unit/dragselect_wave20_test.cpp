// ===========================================================================
// Golden-vector tests for gui/dragselect — drag-select orchestration + drag
// cursor render/state, reconstructed 1:1 from gilde.exe (wave-20).
//   0x4be154 DrawBox · 0x4bdc3c/0x4bdecc Apply · 0x4ba2bc UpdateUnitList
//   0x41fcbc SetSprite · 0x41fa1c Render (badge layout)
// ===========================================================================
#include "test.h"
#include "gui/dragselect.h"

using namespace guild;
using namespace guild::gui;
using guild::play::DragBox;
using guild::play::Viewport;
using guild::play::ProjectParams;

// --- 0x4be154 DrawBox geometry ---------------------------------------------
TEST(DragSelectW20_DrawBox, InactiveEmitsNothing) {
    DragBox box; box.active = 0; box.ax = 1; box.ay = 2; box.bx = 3; box.by = 4;
    DragBoxSegment seg[4];
    CHECK_EQ(DragSelect_BoxSegments(box, seg), 0);
}

TEST(DragSelectW20_DrawBox, FourEdgesExactEndpoints) {
    DragBox box; box.active = 1;
    box.ax = 10; box.ay = 20; box.bx = 50; box.by = 80;
    DragBoxSegment seg[4];
    CHECK_EQ(DragSelect_BoxSegments(box, seg), 4);
    // call1: (ax,ay)->bx
    CHECK_EQ(seg[0].x1, 10); CHECK_EQ(seg[0].y1, 20); CHECK_EQ(seg[0].x2, 50);
    // call2: (bx,by+1)->bx   (the inc ecx == by+1)
    CHECK_EQ(seg[1].x1, 50); CHECK_EQ(seg[1].y1, 81); CHECK_EQ(seg[1].x2, 50);
    // call3: (bx,by)->ax
    CHECK_EQ(seg[2].x1, 50); CHECK_EQ(seg[2].y1, 80); CHECK_EQ(seg[2].x2, 10);
    // call4: (ax,ay)->ax
    CHECK_EQ(seg[3].x1, 10); CHECK_EQ(seg[3].y1, 20); CHECK_EQ(seg[3].x2, 10);
    for (int i = 0; i < 4; ++i) CHECK_EQ((int)seg[i].color, -1);
}

static int g_drawCalls = 0;
static char DrawLineSpy(int, int, int, i16) { ++g_drawCalls; return 0; }

TEST(DragSelectW20_DrawBox, RouteThroughHookFourTimes) {
    DragBox box; box.active = 1; box.ax = 0; box.ay = 0; box.bx = 4; box.by = 4;
    DragDrawHooks h; h.drawLineLocked = &DrawLineSpy;
    g_drawCalls = 0;
    DragSelect_DrawBox(box, h);
    CHECK_EQ(g_drawCalls, 4);
    // Inactive -> no calls.
    box.active = 0; g_drawCalls = 0;
    DragSelect_DrawBox(box, h);
    CHECK_EQ(g_drawCalls, 0);
}

// --- 0x4bdc3c/0x4bdecc Apply box hit-test ----------------------------------
TEST(DragSelectW20_Apply, ClampsCornerAndSelectsInsideUnits) {
    DragBox box; box.active = 1;
    box.ax = 0; box.ay = 0;  // first corner at origin
    Viewport vp; vp.x0 = 0; vp.y0 = 0; vp.x1 = 100; vp.y1 = 100;
    // Drag the second corner toward (200,200): clamps to (99,99).
    int cx = 200 << 16, cy = 200 << 16;

    // Identity projection: sx = sumX*0.125 / (0.125*sumZ), centerX=0, scaleX=1.
    ProjectParams pp; pp.centerX = 0; pp.centerY = 0; pp.scaleX = 1.0f; pp.scaleY = 1.0f;

    DragUnit units[2];
    // Unit 0: centroid (sumX/8, sumY/8) at (50,50)/(0.125*8)=... use sumZ=8 so inv=1.
    // centroid x = sumX*0.125, sx = sumX*0.125 / (0.125*sumZ) = sumX/sumZ.
    units[0].handle = 1; units[0].active = 1; units[0].selFlag = 0x800;
    units[0].hasMesh = true; units[0].sumX = 50; units[0].sumY = 50; units[0].sumZ = 1;
    // Unit 1: outside (200,200) -> sx=200 > 99 -> not selected.
    units[1].handle = 1; units[1].active = 1; units[1].selFlag = 0x800;
    units[1].hasMesh = true; units[1].sumX = 200; units[1].sumY = 200; units[1].sumZ = 1;

    DragSelect_ApplyToUnits(box, units, 2, cx, cy, vp, pp);
    CHECK_EQ(box.bx, 99);   // clamped to x1-1
    CHECK_EQ(box.by, 99);
    CHECK_EQ((int)units[0].inBox, 1);
    CHECK_EQ((int)units[1].inBox, 0);
}

TEST(DragSelectW20_Apply, FlagGateAndDeadSlots) {
    DragBox box; box.active = 1; box.ax = 0; box.ay = 0;
    Viewport vp; vp.x0 = 0; vp.y0 = 0; vp.x1 = 100; vp.y1 = 100;
    ProjectParams pp; pp.scaleX = 1.0f; pp.scaleY = 1.0f;
    DragUnit units[3];
    // missing 0x800 flag -> never selected even if geometrically inside
    units[0].handle = 1; units[0].active = 1; units[0].selFlag = 0; units[0].inBox = 1;
    units[0].hasMesh = true; units[0].sumX = 1; units[0].sumY = 1; units[0].sumZ = 1;
    // dead slot (handle 0) -> untouched
    units[1].handle = 0; units[1].inBox = 1;
    // inactive (active 0) -> untouched
    units[2].handle = 1; units[2].active = 0; units[2].inBox = 1;
    DragSelect_ApplyToSelection(box, units, 3, 1 << 16, 1 << 16, vp, pp);
    CHECK_EQ((int)units[0].inBox, 0);  // flag cleared it, gate prevented re-set
    CHECK_EQ((int)units[1].inBox, 1);  // dead slot untouched
    CHECK_EQ((int)units[2].inBox, 1);  // inactive untouched
}

// --- 0x4ba2bc UpdateUnitList state machine ---------------------------------
static int g_single49 = 0, g_named53 = 0, g_entity29 = 0, g_slotReset = 0, g_playerAct = 0;
static void Spy_Single(i32) { ++g_single49; }
static void Spy_Named(i32, i32, int, i32, int, int) { ++g_named53; }
static void Spy_Entity(i32, i32) { ++g_entity29; }
static void Spy_Slot(int) { ++g_slotReset; }
static void Spy_Player(i32, int, int, u16) { ++g_playerAct; }
static i32  Spy_Query(i32, int, int, int) { return 0; }

static UpdateUnitListHooks MakeHooks() {
    UpdateUnitListHooks h;
    h.queryFind = &Spy_Query;
    h.changePlayerAction = &Spy_Player;
    h.queueSingle49 = &Spy_Single;
    h.queueNamed53 = &Spy_Named;
    h.queueEntity29 = &Spy_Entity;
    h.queueSlotReset28 = &Spy_Slot;
    return h;
}
static void ResetSpies() {
    g_single49 = g_named53 = g_entity29 = g_slotReset = g_playerAct = 0;
}

TEST(DragSelectW20_UpdateUnitList, NonPlayerOpensBatchAndCollapses) {
    ResetSpies();
    UpdateUnitListState st; st.playerRecord = 999;  // never matches
    UpdateCell cells[2];
    cells[0].pending = 1; cells[0].owner = 7; cells[0].target = 1; cells[0].kind = 67;
    cells[0].targetId = 42; cells[0].action = 5;
    cells[0].followSlots[0] = 11; cells[0].followSlots[1] = 42; // second slot == id
    cells[1].pending = 0;
    int queued = DragSelect_UpdateUnitList(cells, 2, st, MakeHooks());
    CHECK_EQ(queued, 1);                  // v1 accumulator
    CHECK_EQ(g_slotReset, 1);             // batch was opened -> tail flush
    CHECK_EQ(g_entity29, 1);              // last target flushed
    CHECK_EQ(cells[0].followSlots[1], -1);// collapsed (first match)
    CHECK_EQ(cells[0].followSlots[0], 11);// earlier non-match preserved
    CHECK_EQ((int)cells[0].pending, 0);   // cleared
    CHECK_EQ(g_playerAct, 0);             // not the player
}

TEST(DragSelectW20_UpdateUnitList, PlayerPathQueuesCommands) {
    ResetSpies();
    UpdateUnitListState st; st.playerRecord = 7;
    UpdateCell cells[1];
    cells[0].pending = 1; cells[0].owner = 7; cells[0].target = 1; cells[0].kind = 67;
    cells[0].targetId = 42; cells[0].action = 9; cells[0].followSlots[0] = 42;
    int queued = DragSelect_UpdateUnitList(cells, 1, st, MakeHooks());
    CHECK_EQ(queued, 0);                  // player path does not ++v1
    CHECK_EQ(g_playerAct, 1);
    CHECK_EQ(g_single49, 1);
    CHECK_EQ(g_named53, 1);
    CHECK_EQ(cells[0].followSlots[0], -1);
    CHECK_EQ(g_slotReset, 0);             // no batch opened on player path
}

TEST(DragSelectW20_UpdateUnitList, NonCharKindSkipped) {
    ResetSpies();
    UpdateUnitListState st; st.playerRecord = 7;
    UpdateCell cells[1];
    cells[0].pending = 1; cells[0].owner = 7; cells[0].target = 1; cells[0].kind = 5; // not 'C'
    int queued = DragSelect_UpdateUnitList(cells, 1, st, MakeHooks());
    CHECK_EQ(queued, 0);
    CHECK_EQ(g_single49, 0);
    CHECK_EQ((int)cells[0].pending, 0);   // still cleared
}

// --- 0x41fcbc SetSprite -----------------------------------------------------
static int g_freeSlot = 0, g_register = 0;
static void Spy_Free(i32) { ++g_freeSlot; }
static i32  Spy_State(i32 id) { return id + 100; }
static i32  Spy_Register(u16, i16, int, i32) { ++g_register; return 55; }

TEST(DragSelectW20_SetSprite, SetRegistersAndClearFrees) {
    g_freeSlot = g_register = 0;
    DragCursorSpriteHooks h; h.freeSlot = &Spy_Free; h.stateUpdate = &Spy_State;
    h.registerSlot = &Spy_Register;
    DragCursorCoords c; c.x = 12; c.y = 34;
    DragCursorSprite s;  // slot = -1, mode = 0
    // a2 != 0, no prior slot: register, no free.
    DragCursor_SetSprite(s, 7, c, h);
    CHECK_EQ(s.slot, 55);
    CHECK_EQ(g_register, 1);
    CHECK_EQ(g_freeSlot, 0);
    // a2 != 0 with prior slot: free then register.
    DragCursor_SetSprite(s, 8, c, h);
    CHECK_EQ(g_freeSlot, 1);
    CHECK_EQ(g_register, 2);
    CHECK_EQ(s.slot, 55);
    // a2 == 0 with slot: free + clear + mode 0.
    s.mode = 3;
    DragCursor_SetSprite(s, 0, c, h);
    CHECK_EQ(g_freeSlot, 2);
    CHECK_EQ(s.slot, -1);
    CHECK_EQ((int)s.mode, 0);
}

TEST(DragSelectW20_SetSprite, ClearWithNoSlotJustResetsMode) {
    g_freeSlot = g_register = 0;
    DragCursorSpriteHooks h; h.freeSlot = &Spy_Free; h.stateUpdate = &Spy_State;
    h.registerSlot = &Spy_Register;
    DragCursorCoords c;
    DragCursorSprite s; s.slot = -1; s.mode = 4;
    DragCursor_SetSprite(s, 0, c, h);
    CHECK_EQ(g_freeSlot, 0);   // no slot to free
    CHECK_EQ(s.slot, -1);
    CHECK_EQ((int)s.mode, 0);
}

// --- 0x41fa1c Render badge layout ------------------------------------------
TEST(DragSelectW20_Render, HiddenEmitsNothing) {
    DragCursorRenderState rs; rs.hidden = 1; rs.childShape[0] = 5;
    DragCursorBadge out[6];
    CHECK_EQ(DragCursor_BadgeLayout(rs, out), 0);
}

TEST(DragSelectW20_Render, GridLayoutSkipsMinusOne) {
    DragCursorRenderState rs;
    rs.cursorX = 100; rs.cursorY = 200; rs.rowAddend = 0; rs.xBias = 4;
    // slots: [0]=valid,[1]=-1(skip),[2]=valid,[3]=valid -> 3 emitted, v10 0,1,2
    rs.childShape[0] = 10; rs.childCount[0] = 1;
    rs.childShape[1] = -1;
    rs.childShape[2] = 20; rs.childCount[2] = 2;
    rs.childShape[3] = 30; rs.childCount[3] = 3;
    DragCursorBadge out[6];
    int n = DragCursor_BadgeLayout(rs, out);
    CHECK_EQ(n, 3);
    // v10=0: x = 0 + 100 + 4 = 104, y = (0+26)*(0) + 200 = 200
    CHECK_EQ(out[0].x, 104); CHECK_EQ(out[0].y, 200); CHECK_EQ(out[0].shapeId, 10);
    // v10=1: x = 26 + 104 = 130, y = 26*(1/3=0) + 200 = 200
    CHECK_EQ(out[1].x, 130); CHECK_EQ(out[1].y, 200); CHECK_EQ(out[1].shapeId, 20);
    // v10=2: x = 52 + 104 = 156, y = 26*(2/3=0) + 200 = 200
    CHECK_EQ(out[2].x, 156); CHECK_EQ(out[2].y, 200); CHECK_EQ(out[2].shapeId, 30);
}

TEST(DragSelectW20_Render, GridWrapsRowsEveryThree) {
    DragCursorRenderState rs;
    rs.cursorX = 0; rs.cursorY = 0; rs.rowAddend = 0; rs.xBias = 0;
    for (int i = 0; i < 6; ++i) { rs.childShape[i] = i + 1; rs.childCount[i] = i; }
    DragCursorBadge out[6];
    int n = DragCursor_BadgeLayout(rs, out);
    CHECK_EQ(n, 6);
    // v10=3 -> row 1: y = 26*(3/3=1) = 26, x = 26*(3%3=0) = 0
    CHECK_EQ(out[3].x, 0);  CHECK_EQ(out[3].y, 26);
    CHECK_EQ(out[4].x, 26); CHECK_EQ(out[4].y, 26);
    CHECK_EQ(out[5].x, 52); CHECK_EQ(out[5].y, 26);
}
