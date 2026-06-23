// ===========================================================================
// Unit tests for src/play/session_input.{h,cpp} — the REAL input -> selection
// frame path (suite prefix: SessionInput).
//
// Scripted shim input -> deterministic selection over synthetic entity
// fixtures, plus golden vectors for the poll/gate ordering:
//   0x40dab8 VIBE_Input_LatchMouseState (frame head: ring pop -> mirror)
//   0x40cdd0 VIBE_Input_ProcessMouseClicks (click/double-click/ring/latch)
//   0x40d920 keyboard auto-repeat latch (+9 / +5 tick windows, sentinel)
//   0x40d388 device-step binding (frame tail, 0x4c0f21) + word_75BF4A latch
//   0x414a38 MainLoop scan over original-layout 740-byte records
//   0x4147cc SelectEntity fallback (REAL ComputeSelectionVolumeSolve kernel)
//   0x4b950c commit on the left-RELEASE edge frame / empty-click reset
//   0x4b9444/0x4b94d8 deselect pair on the right-click edge
//
// FRAME LATENCY (the original's, reproduced exactly): the mouse device is
// polled at the END of a frame (events spill into the unk_670FE0 ring), and
// LatchMouseState pops ONE row at the HEAD of each following frame — so a
// physical transition in frame N surfaces as a mirror edge in frame N+1 (and
// the edge-clearing row pops in frame N+2).
// ===========================================================================
#include "tests/framework/test.h"

#include "gui/gui_dialogs4.h"
#include "gui/input.h"
#include "gui/input_state.h"
#include "play/input_recon4_hotkey.h"
#include "play/session_input.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Absolute-address peek into the gui::g_mouseInput block (base 0x672174).
i32 peek32(int absAddr) {
    i32 v;
    std::memcpy(&v, gui::g_mouseInput.raw + (absAddr - gui::kInputStateBase),
                sizeof v);
    return v;
}
i16 peek16(int absAddr) {
    i16 v;
    std::memcpy(&v, gui::g_mouseInput.raw + (absAddr - gui::kInputStateBase),
                sizeof v);
    return v;
}

// Reset every global the chain touches (the modules share live stores).
void ResetAll() {
    gui::ResetMouseInput();
    g_inputClick = InputClickState{};
    g_inputClock = 0;
    g_softCursorMode = 0;
    g_widgetCursorX = 0;
    g_widgetCursorY = 0;
    std::memset(g_mouseEventRing, 0, sizeof g_mouseEventRing);
    std::memset(g_keyDownTable, 0, sizeof g_keyDownTable);
    std::memset(g_keyReleasedTable, 0, sizeof g_keyReleasedTable);
    g_keyRepeatLatch = 0;
    g_keyRepeatDueTick = 0;
    g_lastHotkeyChar = 0;

    g_selectionAnchors = SelectionAnchors{};
    g_selectionOwnerA = 0;
    g_selectionOwnerB = nullptr;
    Selection_SetResetHooks(SelectionResetHooks{});
    SelectEntity_SetHooks(SelectEntityHooks{});
    Input_SetPollHooks(InputPollHooks{});

    g_selectionContact = SelectionContactLatch{};
    g_selectionAnchorRecords = SelectionAnchorRecords{};
    g_selectionCommit = SelectionCommitState{};
    g_selectionOwners = SelectionOwnerRecords{};
    g_quickJump = QuickJumpRequest{};
    g_selectGate = SelectGateState{};
    g_selectionStatus = SelectionStatusLatch{};
    Selection_SetCommitHooks(SelectCommitHooks{});
    StatusLatchHooks sh;
    sh.computeSelectionFlags = [](u16, u8*, u8*, u8*) -> u16 { return 0; };
    sh.statusTextReset = []() {};
    Selection_SetStatusLatchHooks(sh);

    gui::g_hoverObject = -1;
    gui::g_hoverWindow = -1;
    gui::g_lastClickedWindow = -1;
    gui::g_mouseClick = 0;
    gui::g_mouseDown = 0;
}

SessionInput::Result Drive(SessionInput& si, i32 x, i32 y, bool l, bool r,
                           u32 clock) {
    SessionInputFrame f;
    f.mouseX = x;
    f.mouseY = y;
    f.left = l;
    f.right = r;
    f.clock = clock;
    return si.Frame(f);
}

// Click helper: press -> pop press row -> release -> pop clear row -> pop
// release row (the commit frame).  Returns the COMMIT-frame result.
// Clocks step by 1 from `c0` (use gaps > 40 between clicks: the dword_62D0FC
// double-click window).
SessionInput::Result Click(SessionInput& si, i32 x, i32 y, u32 c0) {
    Drive(si, x, y, true, false, c0);        // press (spills press+clear rows)
    Drive(si, x, y, true, false, c0 + 1);    // pops the press row (click edge)
    Drive(si, x, y, false, false, c0 + 2);   // pops press-clear; spills release
    SessionInput::Result r =
        Drive(si, x, y, false, false, c0 + 3);    // pops the RELEASE row
    Drive(si, x, y, false, false, c0 + 4);   // drains the release-clear row
    return r;
}

// A standard "city entity" descriptor under (120, 120) with id 3.
SessionInputEntity CityEntity3() {
    SessionInputEntity e;
    e.id = 3;
    e.actionCode = 0x55;
    e.left = 100; e.top = 100; e.width = 50; e.height = 40;   // outer box
    e.innerLeft = 100; e.innerRight = 160;                    // second-scan x
    e.yMin = 100; e.yMax = 150;                               // second-scan y
    e.typeByte = 0;                                           // city profile
    e.kind = 1;
    e.typeCode = 4;
    e.handle = "obj_house";
    e.name = "Haus";
    return e;
}

} // namespace

// ===========================================================================
// Poll chain — live/current/mirror packet propagation + edge frame latency.
// ===========================================================================
TEST(SessionInput, PollPacketMirrorAndEdgeLatency) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);

    // frame 1: the tail poll latches live + current; the mirror still holds
    // the PREVIOUS frame's cursor (LatchMouseState ran before the poll).
    SessionInput::Result r = Drive(si, 10, 20, false, false, 100);
    CHECK_EQ((int)peek16(0x6721C4), 10);     // live (dword_6721C4 lo word)
    CHECK_EQ((int)peek16(0x6721C6), 20);
    CHECK_EQ((int)peek16(0x672174), 10);     // current (0x40d09e latch)
    CHECK_EQ((int)peek16(0x672176), 20);
    CHECK_EQ((int)peek16(0x672210), 0);      // mirror lags one frame
    CHECK_EQ((int)g_widgetCursorX, 10);      // word_75BF4A (0x4c0f35)
    CHECK_EQ((int)g_widgetCursorY, 20);      // word_75BF48 (0x4c0f41)

    // frame 2: LatchMouseState copies current -> mirror cursor (0x40dc06).
    r = Drive(si, 10, 20, false, false, 101);
    CHECK_EQ((int)peek16(0x672210), 10);
    CHECK_EQ((int)peek16(0x672212), 20);
    CHECK_EQ(g_selectGate.cursorX16 >> 16, 10);   // unk_67220E view
    CHECK_EQ(g_selectGate.cursorY16 >> 16, 20);   // dword_672210 view
    CHECK(!r.leftClickEdge);
    CHECK(!r.leftRelease);

    // frame 3: PRESS — the transition spills ring rows at the frame tail;
    // no edge is visible yet (the original's one-frame event latency).
    r = Drive(si, 10, 20, true, false, 102);
    CHECK(!r.leftClickEdge);
    i32 rowFlag;                                  // row 1 spilled (flag +72)
    std::memcpy(&rowFlag, g_mouseEventRing + 76 * 1 + 72, sizeof rowFlag);
    CHECK_EQ(rowFlag, 1);

    // frame 4: the press row pops -> click edge + held in the mirror.
    r = Drive(si, 10, 20, true, false, 103);
    CHECK(r.leftClickEdge);                       // dword_672228
    CHECK(gui::g_mouseClick != 0);                // dual view
    CHECK(gui::g_mouseDown != 0);                 // dword_672220 (held)

    // frame 5: the edge-clear row pops -> edge gone, held persists.
    r = Drive(si, 10, 20, true, false, 104);
    CHECK(!r.leftClickEdge);
    CHECK(gui::g_mouseDown != 0);

    // frame 6: RELEASE (spills release + clear rows at the tail).
    r = Drive(si, 10, 20, false, false, 105);
    CHECK(!r.leftRelease);                        // not visible yet
    // frame 7: the release row pops -> the commit-gate flag.
    r = Drive(si, 10, 20, false, false, 106);
    CHECK(r.leftRelease);                         // dword_67221C — THE GATE
    CHECK_EQ(g_selectGate.g67221C, peek32(0x67221C));
}

TEST(SessionInput, DoubleClickSwallowsSecondRelease) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    // First click pair (clocks > 40: the BSS-zero dword_62D0FC boot state
    // means "a click at tick 0" — the original's exact quirk).
    SessionInput::Result r1 = Click(si, 5, 5, 100);
    CHECK(r1.leftRelease);                        // first release surfaces
    // Second click within the 40-tick dword_62D0FC window -> 0x40d183:
    // the release becomes a DOUBLE-CLICK row (dword_6721E0) with the left-up
    // flag swallowed (0x40d189).
    SessionInput::Result r2 = Click(si, 5, 5, 110);
    CHECK(!r2.leftRelease);
    CHECK(r2.doubleClick);                        // dword_6721E0 mirror row
}

// ===========================================================================
// Keyboard auto-repeat latch (0x40d920 tail): +9 initial, +5 repeat, sentinel.
// ===========================================================================
TEST(SessionInput, KeyboardRepeatWindows) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);

    SessionKeyEvent press{0x1E, true};
    SessionInputFrame f;
    f.clock = 100;
    f.keys = &press;
    f.keyCount = 1;
    SessionInput::Result r = si.Frame(f);
    CHECK_EQ((int)r.repeatScancode, 0x1E);
    CHECK_EQ((int)g_lastHotkeyChar, 0x1E);       // byte_67225C latched
    CHECK_EQ((int)g_keyRepeatLatch, 0x1E);       // byte_62D100
    CHECK_EQ((unsigned)g_keyRepeatDueTick, 109u); // clock + 9

    // held, repeat not due (due+5 = 114 not < 110): byte_67225C stays clear
    r = Drive(si, 0, 0, false, false, 110);
    CHECK_EQ((int)g_lastHotkeyChar, 0);
    CHECK_EQ((int)r.repeatScancode, 0x1E);       // al = the held latch

    // repeat due at clock 115 (109 + 5 < 115): byte_67225C re-latched
    r = Drive(si, 0, 0, false, false, 115);
    CHECK_EQ((int)g_lastHotkeyChar, 0x1E);
    CHECK_EQ((unsigned)g_keyRepeatDueTick, 115u);

    // release: -1 return, latch cleared, the 1316134911 sentinel
    SessionKeyEvent rel{0x1E, false};
    f = SessionInputFrame{};
    f.clock = 116;
    f.keys = &rel;
    f.keyCount = 1;
    r = si.Frame(f);
    CHECK_EQ((int)r.repeatScancode, 0xFF);
    CHECK_EQ((int)g_keyRepeatLatch, 0);
    CHECK_EQ((unsigned)g_keyRepeatDueTick, 1316134911u);
    CHECK_EQ((int)g_keyReleasedTable[0x1E], 1);  // byte_671F60 edge
}

// ===========================================================================
// MainLoop scan over the default record store (0x414a38).
// ===========================================================================
TEST(SessionInput, ScanPicksCityEntity) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    si.UpdateEntity(CityEntity3());

    // frame 1 latches word_75BF4A/48; the scan consumes it from frame 2 on
    // (MainLoop runs on the PREVIOUS frame's cursor — 0x4c0f2f vs 0x4215b0).
    Drive(si, 120, 120, false, false, 100);
    SessionInput::Result r = Drive(si, 120, 120, false, false, 101);
    CHECK_EQ(r.pickedId, 3);                     // dword_62D22C
    CHECK_EQ(r.actionCode, 0x55);                // dword_75BF40
    CHECK_EQ(r.secondaryId, 3);                  // dword_62D240 = dword_62D22C
    CHECK_EQ(r.hoverChildId, -1);                // city profile: no hover
    CHECK_EQ(r.mainGroupId, -1);
    CHECK_EQ(gui::g_hoverObject, 3);             // dual view dword_62D22C
    CHECK_EQ(gui::g_hoverPrev, 0x55);            // dword_75BF40 (0x4215b0)
    CHECK_EQ(gui::g_lastClickedWindow, -1);      // dword_75BF08 stays open
    CHECK(!r.selected);                          // no click yet

    Drive(si, 300, 300, false, false, 102);      // move off the box
    r = Drive(si, 300, 300, false, false, 103);
    CHECK_EQ(r.pickedId, -1);
    CHECK_EQ(r.actionCode, -1);
}

TEST(SessionInput, Type9Returns1155Or1210) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    SessionInputEntity e = CityEntity3();
    e.typeByte = 9;                              // the 0x414a38 type-9 branch
    e.flag444 = 0x10;
    si.UpdateEntity(e);
    Drive(si, 120, 120, false, false, 100);
    SessionInput::Result r = Drive(si, 120, 120, false, false, 101);
    CHECK_EQ(r.actionCode, 1155);                // *(v11+444) & 0x10
    e.flag444 = 0;
    si.UpdateEntity(e);
    r = Drive(si, 120, 120, false, false, 102);
    CHECK_EQ(r.actionCode, 1210);
}

TEST(SessionInput, WidgetProfileBlocksCommitGate) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    SessionInputEntity e = CityEntity3();
    e.typeByte = 64;                             // widget profile: hover-able
    e.groupId = 7;
    e.groupGate = true;
    si.UpdateEntity(e);

    Drive(si, 120, 120, false, false, 100);      // cursor latch
    SessionInput::Result r = Click(si, 120, 120, 101);
    CHECK_EQ(r.hoverChildId, 7);                 // dword_62D290 = v3[29]
    CHECK_EQ(r.mainGroupId, 7);                  // dword_62D294 (gated scan)
    CHECK_EQ(gui::g_lastClickedWindow, 7);       // dword_75BF08 (0x4218e0)
    CHECK(r.leftRelease);
    CHECK(!r.commitRan);                         // modal veto: dword_75BF08 != -1
    CHECK(!r.selected);
}

// ===========================================================================
// The commit (0x4b950c): on the left-RELEASE edge frame, inside the viewport.
// ===========================================================================
TEST(SessionInput, CommitOnLeftRelease) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    si.UpdateEntity(CityEntity3());

    Drive(si, 120, 120, false, false, 100);      // cursor latch
    // press frame + press-row frame: no commit yet (gate dword_67221C == 0)
    Drive(si, 120, 120, true, false, 101);
    SessionInput::Result r = Drive(si, 120, 120, true, false, 102);
    CHECK(r.leftClickEdge);
    CHECK(!r.commitRan);
    CHECK(!r.selected);
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);

    // release (pops press-clear, spills release rows), then the release-row
    // frame: the real commit fires.
    Drive(si, 120, 120, false, false, 103);
    r = Drive(si, 120, 120, false, false, 104);
    CHECK(r.leftRelease);
    CHECK(r.commitRan);
    CHECK(r.selected);
    CHECK_EQ(r.selectedId, 3);
    CHECK_EQ(r.selectedKind, 1);
    CHECK(g_selectionAnchorRecords.a631740 != nullptr);
    CHECK_EQ(g_selectionAnchorRecords.a11BC278, 1);   // dword_631730 verdict
    CHECK(std::strcmp(si.selectedName(), "Haus") == 0);
}

TEST(SessionInput, ReleaseOutsideViewportNoCommit) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 100, 100);              // cursor (120,120) is outside
    si.UpdateEntity(CityEntity3());
    Drive(si, 120, 120, false, false, 100);
    SessionInput::Result r = Click(si, 120, 120, 101);
    CHECK(r.leftRelease);
    CHECK(!r.commitRan);                         // cursor gate fails
    CHECK(!r.selected);
}

TEST(SessionInput, EmptyClickDeselects) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    si.UpdateEntity(CityEntity3());
    Drive(si, 120, 120, false, false, 100);
    SessionInput::Result r = Click(si, 120, 120, 101);
    CHECK(r.selected);

    // click empty ground: empty latch -> the real Selection_Reset @0x4b9444
    Drive(si, 300, 300, false, false, 150);      // cursor latch (off the box)
    r = Click(si, 300, 300, 151);
    CHECK(r.commitRan);                          // gate passed, latch empty
    CHECK(!r.selected);
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
    CHECK_EQ(g_selectionAnchors.g631740, 0);     // i32 dual view in lockstep
}

TEST(SessionInput, RightClickClearsSelection) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    si.UpdateEntity(CityEntity3());
    Drive(si, 120, 120, false, false, 100);
    CHECK(Click(si, 120, 120, 101).selected);

    Drive(si, 120, 120, false, true, 150);       // right press (spills)
    SessionInput::Result r = Drive(si, 120, 120, false, true, 151); // row pops
    CHECK(r.rightClickEdge);                     // dword_6721F0 mirror
    CHECK(!r.selected);                          // Reset @0x4b9444 + ClearAll
    CHECK(g_selectionCommit.g11BC270 == nullptr);
    CHECK_EQ(g_selectionCommit.g6317B0, 0);
}

TEST(SessionInput, WorkerSelectMarksAndClears) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    SessionInputEntity p = CityEntity3();
    p.id = 9;
    p.kind = 3;                                  // person
    p.name = "Knecht";
    p.selectionFlags = 0x800;                    // own worker (bit 3 of hi byte)
    si.UpdateEntity(p);

    Drive(si, 120, 120, false, false, 100);
    SessionInput::Result r = Click(si, 120, 120, 101);
    CHECK(r.selected);
    CHECK_EQ(r.selectedId, 9);
    CHECK_EQ(r.selectedKind, 3);
    CHECK(g_selectionCommit.g11BC270 != nullptr); // dword_11BC270 worker latch
    CHECK_EQ(g_selectionCommit.g6317B0, 1);
    CHECK(si.workerSelected(9));                  // byte_12CEA98[536*id] view

    si.ClearSelection();
    CHECK(!si.workerSelected(9));
    CHECK(g_selectionCommit.g11BC270 == nullptr);
}

TEST(SessionInput, Type29ContactVoidsSelection) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    SessionInputEntity e = CityEntity3();
    e.typeCode = 29;                             // street/decor: unselectable
    si.UpdateEntity(e);
    Drive(si, 120, 120, false, false, 100);
    SessionInput::Result r = Click(si, 120, 120, 101);
    CHECK(r.commitRan);
    CHECK(!r.selected);                          // 0x4b98c7 voids the anchors
    CHECK(g_selectionAnchorRecords.a631740 == nullptr);
}

// ===========================================================================
// SelectEntity fallback (0x4147cc) through the DEFAULT actor store with the
// REAL ComputeSelectionVolumeSolve kernel (@0x5b7134).
// ===========================================================================
namespace {

// Register the actor + hotspot fixtures: a quad the kernel solves at cursor
// (0,0) with proj center (0, 5) — the +5.0 Y bias (flt_610EBC) cancels into a
// straight ray; the quad spans z ~ 1 with all edge components nonzero.
void RegisterActorFixture(SessionInput& si, bool rectSecondary) {
    SessionInputActor a;
    a.index = 0;
    a.live = true;
    a.anim0 = "act";
    a.animName = "Bob";
    a.animScale = 100;                  // anim record +116
    a.hasVolume = true;
    const float pos[4][3] = {{-1.0f, -1.0f, 0.9f},
                             {1.0f, -0.9f, 1.1f},
                             {-0.9f, 1.0f, 1.2f},
                             {1.0f, 1.0f, 1.0f}};
    const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    std::memcpy(a.cornerPos, pos, sizeof pos);
    std::memcpy(a.cornerUV, uv, sizeof uv);
    a.proj.centerX = 0.0f;
    a.proj.centerY = 5.0f;              // cancels the +5.0 screen-Y bias
    a.proj.scaleX = 1.0f;
    a.halfTileFlag = 0;
    a.listCount = 1;
    a.listIds[0] = 0;
    si.UpdateActor(a);

    // hotspot rect record id 6 (NOT in the 512-slot table)
    SessionInputEntity rect;
    rect.id = 6;
    rect.slotted = false;
    rect.actionCode = 0x77;
    rect.left = 0; rect.top = 0; rect.width = 1000; rect.height = 1000;
    rect.typeByte = 1;
    rect.kind = 1;
    rect.typeCode = 4;
    rect.handle = "act_spot";
    rect.name = "Akteur";
    rect.hasSecondary = rectSecondary;
    rect.secondaryValue = 0xABC;
    si.UpdateEntity(rect);

    const i32 ids[1] = {6};
    si.UpdateHotspotList(0, ids, 1);
}

} // namespace

TEST(SessionInput, FallbackActorHitMergesSharedGlobals) {
    ResetAll();
    SessionInput si;
    si.SetViewport(-10, -10, 10, 10);
    RegisterActorFixture(si, /*rectSecondary=*/true);

    SessionInput::Result r = Drive(si, 0, 0, false, false, 100);
    CHECK_EQ(r.actionCode, 0x77);                // rect +8 through the fallback
    CHECK_EQ(r.pickedId, 6);                     // dword_62D22C merge
    CHECK_EQ(r.hoverChildId, 0xABC);             // dword_62D290 merge (+44 ptr)
    // the secondary makes dword_75BF08 != -1 -> the commit gate's modal veto
    CHECK_EQ(gui::g_lastClickedWindow, 0xABC);
    r = Click(si, 0, 0, 101);
    CHECK(r.leftRelease);
    CHECK(!r.commitRan);                         // widget hotspots never select
    CHECK(!r.selected);
}

TEST(SessionInput, FallbackActorHitWithoutSecondarySelects) {
    ResetAll();
    SessionInput si;
    si.SetViewport(-10, -10, 10, 10);
    RegisterActorFixture(si, /*rectSecondary=*/false);

    SessionInput::Result r = Click(si, 0, 0, 100);
    CHECK_EQ(r.pickedId, 6);
    CHECK_EQ(r.hoverChildId, -1);                // no +44 secondary written
    CHECK(r.commitRan);
    CHECK(r.selected);
    CHECK_EQ(r.selectedId, 6);
}

TEST(SessionInput, FallbackVolumeAbsentIsOriginalMissPath) {
    ResetAll();
    SessionInput si;
    si.SetViewport(-10, -10, 10, 10);
    RegisterActorFixture(si, false);
    // strip the volume: the bone-extent fill is renderer-owned (named gap) —
    // the actor sub-slot must take the original miss path (return 0 -> skip).
    SessionInputActor a;
    a.index = 0;
    a.live = true;
    a.anim0 = "act";
    a.animName = "Bob";
    a.animScale = 100;
    a.hasVolume = false;
    a.listCount = 1;
    a.listIds[0] = 0;
    si.UpdateActor(a);

    SessionInput::Result r = Drive(si, 0, 0, false, false, 100);
    CHECK_EQ(r.actionCode, -1);
    CHECK_EQ(r.pickedId, -1);                    // dword_62D22C = -1 (0x414a38)
}

// ===========================================================================
// Ordering golden vector: latch -> scan(prev cursor) -> modal -> gate -> poll.
// ===========================================================================
TEST(SessionInput, FrameOrderingGolden) {
    ResetAll();
    SessionInput si;
    si.SetViewport(0, 0, 640, 480);
    si.UpdateEntity(CityEntity3());

    // frame 1: poll latches live+current+word_75BF4A; mirror lags.
    SessionInput::Result r1 = Drive(si, 120, 120, false, false, 100);
    CHECK_EQ((int)peek16(0x6721C4), 120);        // live
    CHECK_EQ((int)peek16(0x672174), 120);        // current
    CHECK_EQ((int)g_widgetCursorX, 120);         // word_75BF4A
    CHECK_EQ(r1.pickedId, -1);                   // scan used the OLD cursor (0)

    // frame 2: mirror cursor latched; the scan consumed word_75BF4A.
    SessionInput::Result r = Drive(si, 120, 120, false, false, 101);
    CHECK_EQ((int)peek16(0x672210), 120);        // mirror (gate cursor)
    CHECK_EQ(r.pickedId, 3);
    // dword_75BF40 / dword_62D22C / dword_75BF08 dual views in lockstep
    CHECK_EQ(gui::g_hoverPrev, r.actionCode);
    CHECK_EQ(gui::g_hoverObject, r.pickedId);
    CHECK_EQ(gui::g_lastClickedWindow, g_selectGate.g75BF08);
    // the gate views are the mirror's bytes, not recomputed values
    CHECK_EQ(g_selectGate.cursorX16, peek32(0x67220E));
    CHECK_EQ(g_selectGate.cursorY16, peek32(0x672210));
    CHECK_EQ(g_selectGate.g67221C, peek32(0x67221C));
}
