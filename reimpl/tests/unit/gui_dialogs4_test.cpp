// Unit tests for guild::gui gui_dialogs4 (interaction / layout / render leaves).
#include "test.h"

#include "gui/gui_dialogs4.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"
#include "gui/widget_create.h"

#include <cstdint>
#include <cstring>
#include <sys/mman.h>

using namespace guild;
using namespace guild::gui;

namespace {
// Some widget fields are 32-bit pointer slots (e.g. +44 group link, group +24
// child-id list) that the module truncates to u32 and re-dereferences. On a 64-bit
// host those must live in the low 4 GiB so the truncated pointer round-trips. Map a
// page with MAP_32BIT and bump-allocate test records into it.
void* Low32Alloc(std::size_t n) {
    static unsigned char* base = nullptr;
    static std::size_t used = 0;
    static std::size_t cap = 0;
    if (!base || used + n > cap) {
        cap = 1u << 20;
        base = static_cast<unsigned char*>(
            mmap(nullptr, cap, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0));
        used = 0;
    }
    void* p = base + used;
    used += (n + 15) & ~std::size_t(15);
    std::memset(p, 0, n);
    return p;
}
} // namespace

// Address a heap-backed "data record" the way the original treats the focused
// widget data pointer (an absolute 32-bit handle). On 64-bit hosts we allocate
// in the low 4 GiB-safe way by using a static buffer and storing its truncated
// address; the module truncates to u32 then re-extends, so any address whose low
// 32 bits round-trip works. We keep buffers static and assert the round-trip.
namespace {
// Record pointers are stored host-pointer-width (std::uintptr_t) in this module,
// so any real address is exact — no 32-bit truncation, no ASLR flakiness.
std::uintptr_t HandleOf(void* p) { return reinterpret_cast<std::uintptr_t>(p); }
} // namespace

TEST(GuiDialogs4Unit, ResetEstablishesSentinels) {
    ResetGuiDialogs4();
    CHECK_EQ((unsigned long long)g_focusWidget, 0ull);
    CHECK_EQ(g_caretWidget, -1);
    CHECK_EQ(g_caretBaseW, -1);
    CHECK_EQ(g_focusIndex, -1);
    CHECK_EQ(g_hoverSlot, -1);
    CHECK_EQ(g_hitTestSlot4, -1);
    CHECK_EQ((int)g_wndDeactivated, 0);
    for (int i = 0; i < 18; ++i) CHECK_EQ(g_groupSlots[i], -1);
}

TEST(GuiDialogs4Unit, HooksInstallAndRestore) {
    ResetGuiDialogs4();
    const GuiDialogs4Hooks* def = GuiDialogs4Hooks_Default();
    GuiDialogs4Hooks custom = *def;
    const GuiDialogs4Hooks* prev = SetGuiDialogs4Hooks(&custom);
    CHECK(prev == def);
    const GuiDialogs4Hooks* prev2 = SetGuiDialogs4Hooks(nullptr); // nullptr restores default
    CHECK(prev2 == &custom);
}

// ---- Form_MarkDirtyAndRender: resolves a form via hook, marks widgets dirty ----
namespace {
int g_mdName = 0;
int FakeGameTick(i16, i16, const char* name) {
    return (name && std::strcmp(name, "panel\\test") == 0) ? 3 : 0; // form id 3
}
int g_surfaceCalls = 0;
int FakeSurfaceCreate(void*, int, int) { return ++g_surfaceCalls + 100; }
}

TEST(GuiDialogs4Unit, FormMarkDirtyResolvesAndMarks) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    h.gameTickFinalize = &FakeGameTick;
    h.surfaceCreate = &FakeSurfaceCreate;
    g_surfaceCalls = 0;
    SetGuiDialogs4Hooks(&h);

    // Form 3 owns 1 window (slot 5); that window owns a backing widget (slot 7)
    // and one child (slot 9). Set up the pools.
    Form& f = g_forms[3];
    f = Form{};
    f.windowCount() = 1;
    f.windowId(0) = 5;

    Window& win = g_windows[5];
    win = Window{};
    win.backWidget() = 7;
    win.objCount() = 1;
    WindowChildList(5)[0] = 9;        // child widget slot 9

    g_widgets[7] = Widget{};
    g_widgets[9] = Widget{};

    int fid = Form_MarkDirtyAndRender(0, 0, "panel\\test");
    CHECK_EQ(fid, 3);
    CHECK_EQ(g_widgets[7].at<i32>(52), 1);   // backing widget dirty
    CHECK_EQ(g_widgets[9].at<i32>(52), 1);   // child dirty
    CHECK_EQ(f.shownFlag(), 1);
    CHECK_EQ(f.surfaceA(), 101);
    CHECK_EQ(f.surfaceB(), 102);

    CHECK_EQ(Form_MarkDirtyAndRender(0, 0, "nope"), 0); // unresolved => 0
    SetGuiDialogs4Hooks(nullptr);
    (void)g_mdName;
}

// ---- Widget_DrawScrollBar glyph selection (state-byte logic) ----
namespace {
int g_lastGlyph = -1;
int CaptureAnimApply(int, int, int, int, const char*, int flags) { g_lastGlyph = flags; return 0; }
// A 16-byte coord-record with the word at +6 / +10 = 0 (so the track loop runs 0 times).
unsigned char g_coordRec[16];
std::uintptr_t FakeCoordTransform(int, unsigned) {
    std::memset(g_coordRec, 0, sizeof(g_coordRec));
    return reinterpret_cast<std::uintptr_t>(g_coordRec); // record ptr; test reads only +6/+10 (=0)
}
}

TEST(GuiDialogs4Unit, DrawScrollBarGlyphByState) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    h.animApply = &CaptureAnimApply;
    h.coordTransform = &FakeCoordTransform;
    SetGuiDialogs4Hooks(&h);

    // Widget slot 11: pressed (+40 set) => glyph 10.
    g_widgets[11] = Widget{};
    g_widgets[11].radioFlag() = 0;            // +444 = 0 (skip the focus branch)
    g_widgets[11].at<i32>(68) = 1;            // btnFlagA -> mirror +36 into +40
    g_widgets[11].at<i32>(36) = 1;            // +36 set => +40 becomes 1
    g_focusWidget = 0;
    Widget_DrawScrollBar(11, 0);
    CHECK_EQ(g_lastGlyph, 10);

    // Widget slot 12: hover (+76 set, not pressed) => glyph 9.
    g_widgets[12] = Widget{};
    g_widgets[12].radioFlag() = 0;
    g_widgets[12].at<i32>(76) = 1;            // disabledB/hover mirror -> glyph 9
    Widget_DrawScrollBar(12, 0);
    CHECK_EQ(g_lastGlyph, 9);

    // Widget slot 13: idle => glyph 8.
    g_widgets[13] = Widget{};
    g_widgets[13].radioFlag() = 0;
    Widget_DrawScrollBar(13, 0);
    CHECK_EQ(g_lastGlyph, 8);
    SetGuiDialogs4Hooks(nullptr);
}

// ---- Widget_DrawScrollBar track segment count (0x412361 loop) ----
// The original loops i = 0 .. floor(trackLen/step) INCLUSIVE: the condition is
// (double)i < (double)(trackLen/step) + 0.5, i.e. one more iteration than the
// integer quotient.  We count the frameBase+2 (segment) animBasic calls.
namespace {
int g_segCount = 0;
int CountSegAnimBasic(int, int, int, int, int frame) {
    if (frame == 2) ++g_segCount;   // frameBase==0 here (widget +40 unset)
    return 0;
}
unsigned char g_sbRec[4][16];  // one buffer per `which` (0..3) so reads don't alias
std::uintptr_t SegCoordTransform(int, unsigned which) {
    unsigned char* rec = g_sbRec[which & 3];
    std::memset(rec, 0, 16);
    // step (+6) = 10 for the track-piece record (which==2); 0 for the caps.
    if (which == 2) *reinterpret_cast<unsigned short*>(rec + 6) = 10;
    return reinterpret_cast<std::uintptr_t>(rec);
}
} // namespace

TEST(GuiDialogs4Unit, DrawScrollBarTrackSegmentCountInclusive) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    // animApply is the final call (glyph); animBasic does the segments.
    static int s_animBasic = 0; (void)s_animBasic;
    h.animBasic = &CountSegAnimBasic;
    h.coordTransform = &SegCoordTransform;
    SetGuiDialogs4Hooks(&h);

    // widget +18 high word = track length 35; step = 10 -> 35/10 = 3, inclusive => 4.
    g_widgets[14] = Widget{};
    g_widgets[14].radioFlag() = 0;
    g_widgets[14].at<i32>(18) = 35 << 16;   // *(v2+18)>>16 == 35 (caps width = 0)
    g_segCount = 0;
    g_focusWidget = 0;
    Widget_DrawScrollBar(14, 0);
    CHECK_EQ(g_segCount, 4);                 // floor(35/10)+1 == 4 (binary: 0x412361)
    SetGuiDialogs4Hooks(nullptr);
}

// ---- Widget_DrawCheckbox composite state byte ----
namespace {
int g_cbState = -1;
int CaptureCbApply(int, int, int, int, const char*, int flags) { g_cbState = flags; return 1234; }
}

TEST(GuiDialogs4Unit, DrawCheckboxStateByte) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    h.animApply = &CaptureCbApply;
    SetGuiDialogs4Hooks(&h);

    // Build a 512-byte data record (offsets up to +460).
    static unsigned char rec[600];
    std::memset(rec, 0, sizeof(rec));
    rec[184] = 1;                 // +184 nonzero => not early-out
    // +444 = 0 (no 0x40 early-out, no 0x4/0x8/0x20 special-paths)
    // base state = 8 (no +76,+92,+64 set; no 0x8 bit) -> expect 8.
    *reinterpret_cast<i32*>(rec + 456) = 5;  // off
    *reinterpret_cast<i32*>(rec + 460) = 6;  // span
    std::uintptr_t handle = HandleOf(rec);
    int r = Widget_DrawCheckbox(handle);
    CHECK_EQ(g_cbState, 8);
    CHECK_EQ(r, 1234);

    // Now toggle checked (+76) and selected (+92) and disabled (+64).
    *reinterpret_cast<i32*>(rec + 76) = 1; // state 9
    *reinterpret_cast<i32*>(rec + 92) = 1; // |0x10
    *reinterpret_cast<i32*>(rec + 64) = 1; // |0x4
    Widget_DrawCheckbox(handle);
    CHECK_EQ(g_cbState, 9 | 0x10 | 0x4);

    // +184 == 0 => early return of the input handle, no draw.
    rec[184] = 0;
    g_cbState = -1;
    int r2 = Widget_DrawCheckbox(handle);
    CHECK_EQ(r2, static_cast<int>(handle));
    CHECK_EQ(g_cbState, -1);  // animApply not called
    SetGuiDialogs4Hooks(nullptr);
}

// ---- Window_MainWndProc message dispatch ----
namespace {
int g_quit = 0, g_validate = 0;
long g_defCalls = 0;
void FakePostQuit(int) { ++g_quit; }
void FakeValidate(void*) { ++g_validate; }
long FakeDefProc(void*, unsigned, unsigned long, long) { ++g_defCalls; return 77; }
}

TEST(GuiDialogs4Unit, MainWndProcDispatch) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    h.postQuitMessage = &FakePostQuit;
    h.validateRect = &FakeValidate;
    h.defWindowProc = &FakeDefProc;
    SetGuiDialogs4Hooks(&h);

    g_quit = g_validate = 0; g_defCalls = 0;
    CHECK_EQ(Window_MainWndProc(0,0,nullptr,kWmDestroy,0,0), 0L);
    CHECK_EQ(g_quit, 1);

    CHECK_EQ(Window_MainWndProc(0,0,nullptr,kWmPaint,0,0), 0L);
    CHECK_EQ(g_validate, 1);

    CHECK_EQ(Window_MainWndProc(0,0,nullptr,kWmEraseBkgnd,0,0), 1L);
    CHECK_EQ(g_validate, 2);

    // 0x218 with wParam==0 => magic accept value.
    CHECK_EQ(Window_MainWndProc(0,0,nullptr,0x218,0,0), kWndProcMagicAccept);

    // Unknown small message => DefWindowProc.
    CHECK_EQ(Window_MainWndProc(0,0,nullptr,0x100,0,0), 77L);
    CHECK(g_defCalls >= 1);
    SetGuiDialogs4Hooks(nullptr);
}

// ---- Window_MainWndProc activate latch ----
TEST(GuiDialogs4Unit, MainWndProcActivateLatch) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    SetGuiDialogs4Hooks(&h);
    g_wndAudioActive = 0; // skip the heavy reacquire branch
    // Deactivate (wParam==0, not already deactivated): latch flips to 1.
    CHECK_EQ(g_wndDeactivated, 0);
    Window_MainWndProc(0,0,nullptr,kWmActivate,0,0);
    CHECK_EQ((int)g_wndDeactivated, 1);
    // Reactivate (wParam!=0, deactivated): latch flips back to 0.
    Window_MainWndProc(0,0,nullptr,kWmActivate,1,0);
    CHECK_EQ((int)g_wndDeactivated, 0);
    SetGuiDialogs4Hooks(nullptr);
}

// ---- Widget_HandleKeyInput backspace / no-focus guard ----
TEST(GuiDialogs4Unit, HandleKeyInputNoFocusIsNoop) {
    ResetGuiDialogs4();
    g_focusWidget = 0;
    g_pendingKey = kKeyBackspace;
    g_pendingMouseX = 42 << 16;
    int r = Widget_HandleKeyInput();
    CHECK_EQ(r, 42); // returns mouse-x high word, unchanged
}

TEST(GuiDialogs4Unit, HandleKeyInputBackspaceTrimsBuffer) {
    ResetGuiDialogs4();
    static unsigned char rec[400];
    std::memset(rec, 0, sizeof(rec));
    std::strcpy(reinterpret_cast<char*>(rec + 40), "abc");
    rec[38] = 0; // not numeric (flag & 2 == 0), not text-flag(1) for cap path
    g_focusWidget = HandleOf(rec);
    g_pendingKey = kKeyBackspace;
    Widget_HandleKeyInput();
    CHECK_EQ(std::strcmp(reinterpret_cast<char*>(rec + 40), "ab"), 0);
    CHECK_EQ((int)g_pendingKey, 0);
}

// ---- Widget_HandleKeyInput: enter (0x1C) clears focus + zeroes backing slot ----
TEST(GuiDialogs4Unit, HandleKeyInputEnterClearsFocus) {
    ResetGuiDialogs4();
    static unsigned char rec[400];
    std::memset(rec, 0, sizeof(rec));
    *reinterpret_cast<i32*>(rec + 304) = 22;  // backing slot 22
    g_widgets[22] = Widget{};
    g_widgets[22].at<i32>(40) = 1;            // highlighted
    g_focusWidget = HandleOf(rec);
    g_focusFlag = 1;
    g_pendingKey = kKeyEnter;
    Widget_HandleKeyInput();
    CHECK_EQ((unsigned long long)g_focusWidget, 0ull); // focus cleared
    CHECK_EQ(g_focusFlag, 0);
    CHECK_EQ(g_widgets[22].at<i32>(40), 0);   // backing slot un-highlighted
}

// ---- Widget_HandleKeyInput: tab (0x0F) forward group navigation ----
// Two type-'A' editable children in a group; tab from index 0 transfers focus to
// the next editable child (index 1) and toggles the +40 highlight on the backing
// slots.  Reconstructed group-walk path (0x42056e..).
TEST(GuiDialogs4Unit, HandleKeyInputTabNavigatesGroupForward) {
    ResetGuiDialogs4();
    // child widget slots 30 (focused), 31 (next), each backs a data record.
    static unsigned char recA[400], recB[400];
    std::memset(recA, 0, sizeof(recA));
    std::memset(recB, 0, sizeof(recB));
    *reinterpret_cast<i32*>(recA + 304) = 30;   // recA backed by slot 30
    *reinterpret_cast<i32*>(recB + 304) = 31;   // recB backed by slot 31
    recA[38] = 0; recB[38] = 0;

    // group child-id list: [30, 31], count 2 (word at group+28 = high word of +26 dword).
    // group/childIds are 32-bit pointer slots in widget+44 / group+24 -> low-32 memory.
    i32* childIds = static_cast<i32*>(Low32Alloc(2 * sizeof(i32)));
    childIds[0] = 30; childIds[1] = 31;
    unsigned char* group = static_cast<unsigned char*>(Low32Alloc(64));
    *reinterpret_cast<i32*>(group + 24) = static_cast<i32>(reinterpret_cast<std::intptr_t>(childIds));
    // Count is read as *(int*)(group+26) >> 16, i.e. the WORD at group+28 (the +24
    // id-list pointer occupies +24..+27).  Write the count word there.
    *reinterpret_cast<i16*>(group + 28) = 2;

    // Both backing slots are type 'A', not "destroyed" (+52 == 0), and point at the
    // owning group via +44.  recA's slot also points at the group.
    g_widgets[30] = Widget{}; g_widgets[31] = Widget{};
    g_widgets[30].type() = kTypeAnim; g_widgets[31].type() = kTypeAnim;
    g_widgets[30].at<i32>(44) = static_cast<i32>(reinterpret_cast<std::intptr_t>(group));
    g_widgets[31].at<i32>(44) = static_cast<i32>(reinterpret_cast<std::intptr_t>(group));
    SetWidgetData(30, recA);                    // child 30 -> recA (data ptr +12)
    SetWidgetData(31, recB);                    // child 31 -> recB

    g_focusWidget = HandleOf(recA);
    g_widgets[30].at<i32>(40) = 1;              // recA currently highlighted
    g_focusIndex = 0;
    g_shiftHeldL = g_shiftHeldR = 0;            // forward
    g_pendingKey = kKeyTab;

    Widget_HandleKeyInput();

    CHECK_EQ(g_focusWidget, HandleOf(recB));    // focus moved to recB
    CHECK_EQ(g_focusIndex, 1);                  // dword_75BEBC = found index (1)
    CHECK_EQ(g_widgets[30].at<i32>(40), 0);     // old highlight cleared
    CHECK_EQ(g_widgets[31].at<i32>(40), 1);     // new highlight set
    CHECK_EQ((int)g_pendingKey, 0);
}

// ---- Widget_SetFocus: focus a record, set highlight, no-op when already focused ----
TEST(GuiDialogs4Unit, SetFocusHighlightsAndIsIdempotent) {
    ResetGuiDialogs4();
    // data record with slot index at +304, flags at +38 (no edit), group at +44 = 0.
    static unsigned char rec[400];
    std::memset(rec, 0, sizeof(rec));
    *reinterpret_cast<i32*>(rec + 304) = 20;  // slot 20 backs this record
    rec[38] = 0;
    std::uintptr_t handle = HandleOf(rec);
    g_widgets[15] = Widget{};
    SetWidgetData(15, rec);                   // widget slot 15 -> this data record (+12 pointer)
    g_widgets[20] = Widget{};

    Widget_SetFocus(15);
    CHECK_EQ(g_focusWidget, handle);
    CHECK_EQ(g_widgets[20].at<i32>(40), 1);   // highlight set on the backing slot

    // Focusing the same record again: g_focusWidget unchanged.
    std::uintptr_t before = g_focusWidget;
    Widget_SetFocus(15);
    CHECK_EQ(g_focusWidget, before);
}

// ---- Widget_ProcessMouseDrag: mouse-down focus GRAB (0x420eb2 block) ----
// Cursor is over a type-'A' widget owning a data record; the left button is held;
// no current focus; focusFlag clear.  Focus transfers to that record, the backing
// slot is highlighted, and the focus-value caches are seeded.
TEST(GuiDialogs4Unit, ProcessMouseDragFocusGrab) {
    ResetGuiDialogs4();
    // The hit widget (slot 40) is type 'A' and owns data record `rec`; rec's owning
    // slot (+304) is the same slot 40, so `g_hitTestSlot4 == *(rec+304)` holds.
    static unsigned char rec[400];
    std::memset(rec, 0, sizeof(rec));
    *reinterpret_cast<i32*>(rec + 304) = 40;   // owning slot
    *reinterpret_cast<i32*>(rec + 296) = 0x55; // value cached into g_focusValue
    rec[38] = 0;                               // no edit/group special bits
    *reinterpret_cast<i32*>(rec + 44) = 0;     // (group accessed via backing slot +44)

    g_widgets[40] = Widget{};
    g_widgets[40].type() = kTypeAnim;          // 'A'
    g_widgets[40].at<i32>(44) = 0;             // no owning group
    SetWidgetData(40, rec);                    // slot 40 -> rec (+12 data ptr)

    g_hitTestSlot4 = 40;                        // dword_62D22C
    g_mouseDown = 1;                            // dword_672220
    g_focusWidget = 0;                          // no current focus
    g_focusFlag = 0;                            // dword_62D348
    g_d4_dragOriginX = 100;
    g_pendingMouseX = 30 << 16;                 // v3 = 30

    Widget_ProcessMouseDrag();

    CHECK_EQ(g_focusWidget, HandleOf(rec));     // focus grabbed
    CHECK_EQ(g_widgets[40].at<i32>(40), 1);     // backing slot highlighted
    CHECK_EQ(g_focusValue, 0x55);               // dword_75BECC = *(rec+296)
    CHECK_EQ(g_focusValueAcc, 100 - 30);        // dword_75BED4 = dword_62D0C8 - v3
    CHECK_EQ(g_focusValuePrev, 0);              // dword_75BED0 = 0
    // mouse still down + focus now set => focusFlag latches to 1 (0x4210bc).
    CHECK_EQ(g_focusFlag, 1);
}

// ---- Widget_HoverUpdate: wheel-scroll forwarding via the real Window_Scroll ----
// dword_62D294 (g_scrollWheelWin) points at a window with scroll content; with the
// wheel-up latch set and no focus, HoverUpdate forwards to Window_Scroll which
// advances the window's scroll offset (+592).
TEST(GuiDialogs4Unit, HoverUpdateWheelScrollForward) {
    ResetGuiDialogs4();
    GuiDialogs4Hooks h = *GuiDialogs4Hooks_Default();
    SetGuiDialogs4Hooks(&h);

    // Window slot 4: scrollable.  scrollOffset(+592)=0, scrollCur(+584)=0,
    // contentHeight(+580)=200, h(+8 word)=10.  +612 (v[153]) = step 80.
    Window& win = g_windows[4];
    win = Window{};
    win.at<i32>(0) = 4;      // *(win+0) == own slot id; Window_Scroll indexes g_windows[id]
    win.contentHeight() = 200;
    win.scrollCur() = 0;
    win.scrollOffset() = 0;
    win.h() = 10;
    win.at<i32>(612) = 80;   // wheel step (v[153])

    g_scrollWheelWin = 4;    // dword_62D294
    g_wheelUp = 0;           // dword_672254
    g_wheelDn = 1;           // dword_672250 -> amount = +step (downward scroll)
    g_focusWidget = 0;
    g_stateFinalizeReq = -1; // no deferred finalize
    g_hoverScrollWin = -1;   // skip the hovered-scroll block
    g_mouseDown = 0;
    g_mouseDownHover = 0;

    // g_wheelUp != g_wheelDn, win[153]!=0, no focus -> Window_Scroll(0, +80, 4):
    // else-branch advances scrollOffset from 0 to min(step, contentHeight-h) = 80.
    Widget_HoverUpdate();
    CHECK_EQ(g_windows[4].scrollOffset(), 80); // +592 advanced by the step
    SetGuiDialogs4Hooks(nullptr);
}
