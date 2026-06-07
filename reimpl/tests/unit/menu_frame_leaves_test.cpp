// Unit tests for guild::gui menu_frame_leaves — the genuinely-missing supporting
// leaves that gui::Menu_RunMainMenu and the sub-screens call.  Each test asserts the
// EXACT field/bit behaviour recovered from the decompile, recording host edges via the
// installable MenuFrameLeavesHooks.  Headless + deterministic.
#include "test.h"

#include "gui/menu_frame_leaves.h"
#include "gui/gui_dialogs7.h"   // g_fadeSlots
#include "gui/object.h"         // g_widgets, ResetGuiState
#include "gui/window.h"         // g_windows, g_defaultFont, WindowChildList
#include "gui/widget_create.h"  // g_defaultCtrlH, g_screenClipExt
#include "gui/radiogroup.h"     // g_radioGroups, ResetRadioGroups

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

// A recording hook set so we can observe the DDraw/render edges.
struct Rec {
    int  allocCalls = 0;
    int  freeCalls = 0;
    int  releaseCalls = 0;
    i32  lastReleasedTex = -999;
    int  fillCalls = 0;
    int  fillColorArg = -999;
    const u32* fillPixels = nullptr;
    i32  fillReturn = 0;
};
Rec g_rec;
FadeRecord g_ownedFade[4];
int g_ownedIdx = 0;

FadeRecord* HAlloc(int /*size*/, const char* /*tag*/) {
    ++g_rec.allocCalls;
    FadeRecord* r = &g_ownedFade[g_ownedIdx++ & 3];
    std::memset(r->raw, 0, sizeof(r->raw));
    return r;
}
void HFree(FadeRecord* /*b*/) { ++g_rec.freeCalls; }
void HRelease(i32 tex) { ++g_rec.releaseCalls; g_rec.lastReleasedTex = tex; }
i32  HFill(int /*mode*/, const u32* pix, int color) {
    ++g_rec.fillCalls; g_rec.fillColorArg = color; g_rec.fillPixels = pix;
    return g_rec.fillReturn;
}

MenuFrameLeavesHooks MakeHooks() {
    MenuFrameLeavesHooks h{};
    h.allocFade = &HAlloc;
    h.freeFade = &HFree;
    h.releaseTexture = &HRelease;
    h.fillBackBuffer = &HFill;
    // leave render edges null -> install proper defaults via Set(nullptr) elsewhere
    return h;
}

void FullReset() {
    ResetGuiState();
    ResetRadioGroups();
    ResetMenuFrameLeaves();
    g_rec = Rec{};
    g_ownedIdx = 0;
    SetMenuFrameLeavesHooks(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// Fade_Register: first-free-slot scan, field writes, texture snapshot, handle.
// ---------------------------------------------------------------------------
TEST(MenuFrameLeaves, FadeRegisterFillsRecordAndSlot) {
    FullReset();
    MenuFrameLeavesHooks h = MakeHooks();
    g_rec.fillReturn = 0xC0FFEE;
    SetMenuFrameLeavesHooks(&h);

    i32 handle = Fade_Register(/*x*/ 10, /*y*/ 20, /*h*/ 30, /*w*/ 40,
                               /*flags*/ kFadeDirIn, /*dur*/ 100,
                               /*pixels*/ nullptr, /*now*/ 7777);
    CHECK(handle != 0);
    CHECK_EQ(g_fadeSlots[0], handle);   // stored in slot 0 (first free)
    CHECK_EQ(g_rec.allocCalls, 1);
    CHECK_EQ(g_rec.fillCalls, 1);
    CHECK_EQ(g_rec.fillColorArg, 30);   // FillBackBuffer colour arg == h (a3)

    // Read the record back through Unregister's texture path: release should pass +96.
    i32 r = Fade_Unregister(handle);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_rec.releaseCalls, 1);
    CHECK_EQ(g_rec.lastReleasedTex, 0xC0FFEE); // texture stored at +96 was returned
    CHECK_EQ(g_fadeSlots[0], 0);               // slot cleared
    SetMenuFrameLeavesHooks(nullptr);
}

TEST(MenuFrameLeaves, FadeRegisterFindsFirstFreeAfterGap) {
    FullReset();
    MenuFrameLeavesHooks h = MakeHooks();
    SetMenuFrameLeavesHooks(&h);

    i32 a = Fade_Register(0, 0, 0, 0, kFadeDirIn, 1, nullptr, 1);
    i32 b = Fade_Register(0, 0, 0, 0, kFadeDirIn, 1, nullptr, 1);
    i32 c = Fade_Register(0, 0, 0, 0, kFadeDirIn, 1, nullptr, 1);
    CHECK_EQ(g_fadeSlots[0], a);
    CHECK_EQ(g_fadeSlots[1], b);
    CHECK_EQ(g_fadeSlots[2], c);

    // Free the middle: the scan stops at slot 0 being occupied, so the next register
    // walks to the first hole. The original increments while the slot is non-empty
    // starting from index 0; with slot1 empty it stops at index 1.
    Fade_Unregister(b);
    CHECK_EQ(g_fadeSlots[1], 0);
    i32 d = Fade_Register(0, 0, 0, 0, kFadeDirIn, 1, nullptr, 1);
    CHECK_EQ(g_fadeSlots[1], d);  // reused the hole at index 1
    SetMenuFrameLeavesHooks(nullptr);
}

TEST(MenuFrameLeaves, FadeRegisterFullReturnsZero) {
    FullReset();
    // Fill all 32 slots manually (no alloc needed; we test the >=32 guard).
    for (int i = 0; i < 32; ++i) g_fadeSlots[i] = 1000 + i;
    MenuFrameLeavesHooks h = MakeHooks();
    SetMenuFrameLeavesHooks(&h);
    i32 r = Fade_Register(0, 0, 0, 0, kFadeDirIn, 1, nullptr, 1);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_rec.allocCalls, 0); // never allocates when full
    SetMenuFrameLeavesHooks(nullptr);
}

TEST(MenuFrameLeaves, FadeUnregisterNotFoundSentinel) {
    FullReset();
    for (int i = 0; i < 32; ++i) g_fadeSlots[i] = 5000 + i; // none equals 1
    i32 r = Fade_Unregister(/*handle*/ 1); // not present
    CHECK_EQ(r, 32 * 4); // result*4 == 128 not-found sentinel
}

// ---------------------------------------------------------------------------
// Window_RenderEntityList: call order + fade iteration.
// ---------------------------------------------------------------------------
namespace {
int g_order[16];
int g_orderN;
void RecOrder(int tag) { if (g_orderN < 16) g_order[g_orderN++] = tag; }
void OGameLogic(int, int, i32) { RecOrder(1); }
void OResult(int, int, i32, i32, i32 src, int, int, i32 /*dst*/) { RecOrder(src == 0 ? 2 : 3); }
int  g_fadeUpdateCount;
void OFade(i32, i32) { ++g_fadeUpdateCount; RecOrder(4); }
void OPresent(void*) { RecOrder(5); }
void ONop() { RecOrder(6); }
} // namespace

TEST(MenuFrameLeaves, RenderEntityListCallOrder) {
    FullReset();
    g_orderN = 0; g_fadeUpdateCount = 0;
    g_fadeSlots[0] = 11; g_fadeSlots[5] = 22; // two active fades
    MenuFrameLeavesHooks h{};
    h.allocFade = &HAlloc; h.freeFade = &HFree; h.releaseTexture = &HRelease;
    h.fillBackBuffer = &HFill;
    h.gameLogicEntities = &OGameLogic;
    h.resultHandlerInteraction = &OResult;
    h.fadeUpdate = &OFade;
    h.renderPresentFrame = &OPresent;
    h.guiNop = &ONop;
    SetMenuFrameLeavesHooks(&h);

    Window_RenderEntityList(0x700);

    CHECK_EQ(g_fadeUpdateCount, 2);
    // order: entities(1), blit src=back(2), fades(4,4), present(5), blit src=work(3), nop(6)
    CHECK_EQ(g_order[0], 1);
    CHECK_EQ(g_order[1], 2);
    CHECK_EQ(g_order[2], 4);
    CHECK_EQ(g_order[3], 4);
    CHECK_EQ(g_order[4], 5);
    CHECK_EQ(g_order[5], 3);
    CHECK_EQ(g_order[6], 6);
    SetMenuFrameLeavesHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Object_CreateTextLabel: type/colour/geometry field writes.
// ---------------------------------------------------------------------------
TEST(MenuFrameLeaves, CreateTextLabelStampsFields) {
    FullReset();
    g_defaultFont   = 0x55;   // dword_62D2B0 -> +112
    g_defaultCtrlH  = 0x18;   // dword_69FFB0 -> +22
    g_screenClipExt = 0x01400258; // 0x0140 hi=320, 0x0258 lo=600 -> +30/+34

    int slot = Object_CreateTextLabel(64, 90, "version");
    CHECK(slot >= 0);
    Widget& w = g_widgets[slot];
    CHECK_EQ((int)w.type(), (int)kTypeLabel);      // +24 == 67 'C'
    CHECK_EQ((int)w.at<u16>(112), 0x55);            // +112 colour/font word
    CHECK_EQ((int)w.at<i16>(16), 64);               // +16 x
    CHECK_EQ((int)w.at<i16>(18), 90);               // +18 y
    CHECK_EQ((int)w.at<u16>(26), 2);                // +26 == 2
    CHECK_EQ((int)w.at<u16>(28), 0);                // +28 == 0
    CHECK_EQ((int)w.at<u16>(32), 0);                // +32 == 0
    CHECK_EQ((int)w.at<u16>(22), 0x18);             // +22 default ctrl height
    CHECK_EQ((int)w.at<u16>(34), 0x0258);           // +34 LOWORD(clipExt)
    CHECK_EQ((int)w.at<u16>(30), 0x0140);           // +30 HIWORD(clipExt)
    // The text was copied into the widget's owned +116 buffer.
    const char* buf = static_cast<const char*>(WidgetData(slot));
    CHECK(buf != nullptr);
    CHECK(std::strcmp(buf, "version") == 0);
}

// ---------------------------------------------------------------------------
// Object_SetColor: +112 write + type-keyed propagation.
// ---------------------------------------------------------------------------
TEST(MenuFrameLeaves, SetColorWritesWidgetWordAlways) {
    FullReset();
    Widget& w = g_widgets[3];
    w.type() = kTypeLabel; // 67 -> only the +112 write happens (< 0x40 path is NOT taken;
                           // 67 >= 0x40 but != 64 and != 65, so no propagation)
    Object_SetColor(3, 0x1234);
    CHECK_EQ((int)w.at<u16>(112), 0x1234);
}

TEST(MenuFrameLeaves, SetColorWindowTypePropagates) {
    FullReset();
    Widget& w = g_widgets[4];
    w.type() = kTypeWindow; // 64 ('@')
    w.ownerWindow() = 7;
    Object_SetColor(4, 0xABCD);
    CHECK_EQ((int)w.at<u16>(112), 0xABCD);
    CHECK_EQ((int)g_windows[7].at<u16>(636), 0xABCD); // title colour word
}

TEST(MenuFrameLeaves, SetColorAnimTypePropagates) {
    FullReset();
    Widget& w = g_widgets[5];
    w.type() = kTypeAnim; // 65 ('A')
    w.ownerWindow() = 9;
    Object_SetColor(5, 0x778899);
    CHECK_EQ((int)w.at<u16>(112), (int)(u16)0x778899);
    CHECK_EQ((int)g_windows[9].at<i32>(904), 0x778899); // object colour dword
}

// ---------------------------------------------------------------------------
// Object_SetVisibleRecursive: +52 hidden flag + child recursion.
// ---------------------------------------------------------------------------
TEST(MenuFrameLeaves, SetVisibleFlagPolarity) {
    FullReset();
    Widget& w = g_widgets[2];
    w.type() = kTypeLabel; // not a window -> no recursion
    Object_SetVisibleRecursive(2, 1);   // vis!=0 -> shown -> hidden flag 0
    CHECK_EQ(w.at<i32>(52), 0);
    Object_SetVisibleRecursive(2, 0);   // vis==0 -> hidden -> hidden flag 1
    CHECK_EQ(w.at<i32>(52), 1);
}

TEST(MenuFrameLeaves, SetVisibleRecursesWindowChildren) {
    FullReset();
    // Window-backing widget 1 owns window slot 8 with 3 child label widgets.
    Widget& parent = g_widgets[1];
    parent.type() = kTypeWindow;
    parent.ownerWindow() = 8;
    g_windows[8].objCount() = 3;
    i32* kids = WindowChildList(8);
    kids[0] = 20; kids[1] = 21; kids[2] = 22;
    for (int k : {20, 21, 22}) g_widgets[k].type() = kTypeLabel;

    int n = Object_SetVisibleRecursive(1, 0); // hide all
    CHECK_EQ(n, 3);                            // returns objCount
    CHECK_EQ(parent.at<i32>(52), 1);
    CHECK_EQ(g_widgets[20].at<i32>(52), 1);
    CHECK_EQ(g_widgets[21].at<i32>(52), 1);
    CHECK_EQ(g_widgets[22].at<i32>(52), 1);
}

// ---------------------------------------------------------------------------
// InitStateReader: empty group, ENTER fire, UP/DOWN selection step.
// ---------------------------------------------------------------------------
namespace {
// Build a radio group with N button widgets at known ids.
void BuildGroup(int gi, int n, const int* ids) {
    g_radioGroups[gi].count = n;
    g_radioGroups[gi].selected = 0;
    for (int i = 0; i < n; ++i) {
        g_radioGroups[gi].button[i] = ids[i];
        g_widgets[ids[i]].type() = kTypeWindow; // not 9 -> ENTER takes the id() payload path
        g_widgets[ids[i]].id() = 5000 + ids[i];
    }
}
} // namespace

TEST(MenuFrameLeaves, InitStateReaderEmptyGroupReturns) {
    FullReset();
    g_radioGroups[0].count = 0;
    int r = InitStateReader(0);
    CHECK_EQ(r, 0); // 140*0
    CHECK_EQ(g_menuInput.clickFlag, 0);
}

TEST(MenuFrameLeaves, InitStateReaderEnterFiresButton) {
    FullReset();
    int ids[3] = {30, 31, 32};
    BuildGroup(0, 3, ids);
    g_radioGroups[0].selected = 1;       // currently on button index 1 (id 31)
    g_menuInput.focusLatch = 0;
    g_menuInput.lastMouseX = 0; g_menuInput.lastMouseY = 0;
    g_menuInput.mouseX1616 = 0; g_menuInput.mouseY1616 = 0; // mouse still
    g_menuInput.lastKey = kKeyEnter;     // ENTER

    int r = InitStateReader(0);
    CHECK_EQ(g_menuInput.clickFlag, 1);          // dword_672228 = 1
    CHECK_EQ(g_menuInput.lastKey, 0);            // byte_67225C cleared
    CHECK_EQ(g_menuInput.selectedWidget, 31);    // dword_62D22C = button id (slot)
    CHECK_EQ(g_menuInput.hoverHelpId, 5000 + 31);// dword_75BF38 = widget.id()
    CHECK_EQ(r, 5000 + 31);
}

TEST(MenuFrameLeaves, InitStateReaderDownStepsSelection) {
    FullReset();
    int ids[3] = {40, 41, 42};
    BuildGroup(0, 3, ids);
    g_radioGroups[0].selected = 0;
    g_menuInput.lastKey = kKeyDown;      // DOWN

    InitStateReader(0);
    // DOWN: v3 = (0+1) % 3 == 1 -> Selection_Update(group,1) sets selected=1
    CHECK_EQ(g_radioGroups[0].selected, 1);
}

TEST(MenuFrameLeaves, InitStateReaderUpWrapsToLast) {
    FullReset();
    int ids[3] = {50, 51, 52};
    BuildGroup(0, 3, ids);
    g_radioGroups[0].selected = 0;
    g_menuInput.lastKey = kKeyUp;        // UP

    InitStateReader(0);
    // UP: v6 = 0-1 = -1 -> wrap to count-1 = 2 -> 2 % 3 == 2
    CHECK_EQ(g_radioGroups[0].selected, 2);
}
