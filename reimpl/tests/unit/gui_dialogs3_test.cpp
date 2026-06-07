// Unit tests for src/gui/gui_dialogs3.cpp — the GUI clip / hit-test / raw-bitmap /
// anchor / tooltip leaves.  Golden vectors computed with python (see the brief).
#include "test.h"

#include "gui/gui_dialogs3.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"
#include "gui/window_render.h"
#include "gui/widget_create.h"

#include <cstring>

using namespace guild::gui;

namespace {

// Capture Widget_LayoutBounds calls (it is a weak inert default in window_render.cpp;
// we override it here so PositionAtCoord's layout edge is observable). The unified
// build keeps the weak default; this strong definition wins in the isolated build.
struct LayoutCall { int x, y, idx; };
LayoutCall g_lastLayout;
int        g_layoutCalls;

void ResetAll() {
    ResetWidgets();
    ResetWindows();
    ResetGuiState();   // resets forms + windows + widgets together
    ResetGuiDialogs3();
    ResetWidgetCreate();
    g_lastLayout = {0, 0, 0};
    g_layoutCalls = 0;
}

} // namespace

// Override the weak renderer edge so PositionAtCoord is observable in isolation.
namespace guild::gui {
void Widget_LayoutBounds(int x, int y, int widgetIdx) {
    g_lastLayout = {x, y, widgetIdx};
    ++g_layoutCalls;
}
} // namespace guild::gui

// ---------------------------------------------------------------------------
// VIBE_Util_StrNCopyPad clone — golden: copy up to n, stop at NUL, zero-fill rest.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, StrNCopyPadStopsAtNulAndPads) {
    char dst[8];
    std::memset(dst, 0x55, sizeof(dst));
    char* r = StrNCopyPadLocal(dst, "AB", 5);
    CHECK_EQ(r, dst);
    CHECK_EQ(dst[0], 'A');
    CHECK_EQ(dst[1], 'B');
    CHECK_EQ(dst[2], (char)0);
    CHECK_EQ(dst[3], (char)0);
    CHECK_EQ(dst[4], (char)0);
    CHECK_EQ(dst[5], (char)0x55); // untouched beyond n
}

TEST(GuiDialogs3, StrNCopyPadFullSpanNoTerminator) {
    char dst[8];
    std::memset(dst, 0x55, sizeof(dst));
    StrNCopyPadLocal(dst, "HELLO", 3);
    CHECK_EQ(dst[0], 'H');
    CHECK_EQ(dst[1], 'E');
    CHECK_EQ(dst[2], 'L');
    CHECK_EQ(dst[3], (char)0x55); // n exhausted before any zero-fill -> no NUL
}

TEST(GuiDialogs3, StrNCopyPadEmptySource) {
    char dst[8];
    std::memset(dst, 0x55, sizeof(dst));
    StrNCopyPadLocal(dst, "", 4);
    for (int i = 0; i < 4; ++i) CHECK_EQ(dst[i], (char)0);
    CHECK_EQ(dst[4], (char)0x55);
}

// ---------------------------------------------------------------------------
// VIBE_Gui_FreeString_Thunk — copies <=63 chars into widget +152.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, FreeStringThunkWritesWidget152) {
    ResetAll();
    int slot = Widget_AllocSlot();
    Gui_FreeString_Thunk(slot, "panel");
    const char* p = reinterpret_cast<const char*>(&g_widgets[slot].at<guild::u8>(152));
    CHECK_EQ(std::strcmp(p, "panel"), 0);
    // Byte at +152+5 must be zero-padded.
    CHECK_EQ(g_widgets[slot].at<guild::u8>(152 + 5), (guild::u8)0);
    // Byte at +152+62 (last of the 63-span) zero; +152+63 untouched-ish.
    CHECK_EQ(g_widgets[slot].at<guild::u8>(152 + 62), (guild::u8)0);
}

// ---------------------------------------------------------------------------
// VIBE_Widget_SetTooltipText — byte-pair copy into the global buffer incl. NUL.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, SetTooltipTextCopiesString) {
    ResetAll();
    char r = Widget_SetTooltipText("Tip");
    CHECK_EQ(r, (char)0);
    CHECK_EQ(std::strcmp(g_tooltipText, "Tip"), 0);
}

TEST(GuiDialogs3, SetTooltipTextEvenLength) {
    ResetAll();
    Widget_SetTooltipText("Quit");
    CHECK_EQ(std::strcmp(g_tooltipText, "Quit"), 0);
    CHECK_EQ(g_tooltipText[4], (char)0);
}

// ---------------------------------------------------------------------------
// VIBE_Gui_ClipRectToBuffers — clamp + push into both buffers.
// golden: clip(0,10,100,5,top=20,bottom=80) => outX0 outY20 x5 outH60
//         clip(1,30,40,7,top=0,bottom=200)  => outX0 outY30 x8 outH40
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, ClipRectClampsTopAndBottom) {
    ResetAll();
    g_drawClipTop = 20;
    g_drawClipBottom = 80;
    int ret = Gui_ClipRectToBuffers(/*flags*/ 0, /*y*/ 10, /*h*/ 100, /*x*/ 5);
    CHECK_EQ(ret, 8);
    for (int b = 0; b < kClipRectBuffers; ++b) {
        CHECK_EQ(g_clipRectLen[b], 1);
        CHECK_EQ(g_clipRectBuf[b][0].x, 0);   // outX = flags
        CHECK_EQ(g_clipRectBuf[b][0].y, 20);  // clamped to top
        CHECK_EQ(g_clipRectBuf[b][0].w, 5);   // x unchanged (no flag bit)
        CHECK_EQ(g_clipRectBuf[b][0].h, 60);  // 70 then -10
    }
}

TEST(GuiDialogs3, ClipRectFlagBitSnapsXEven) {
    ResetAll();
    g_drawClipTop = 0;
    g_drawClipBottom = 200;
    Gui_ClipRectToBuffers(/*flags*/ 1, /*y*/ 30, /*h*/ 40, /*x*/ 7);
    CHECK_EQ(g_clipRectBuf[0][0].x, 0);  // flags-1
    CHECK_EQ(g_clipRectBuf[0][0].y, 30);
    CHECK_EQ(g_clipRectBuf[0][0].w, 8);  // (7+2)&~1
    CHECK_EQ(g_clipRectBuf[0][0].h, 40);
}

TEST(GuiDialogs3, ClipRectAppendsAndIncrementsLength) {
    ResetAll();
    g_drawClipTop = 0;
    g_drawClipBottom = 1000;
    Gui_ClipRectToBuffers(0, 0, 10, 0);
    Gui_ClipRectToBuffers(0, 5, 10, 0);
    CHECK_EQ(g_clipRectLen[0], 2);
    CHECK_EQ(g_clipRectLen[1], 2);
    CHECK_EQ(g_clipRectBuf[0][1].y, 5);
}

// ---------------------------------------------------------------------------
// VIBE_Window_ConsumeClickFlag — read-and-clear.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, ConsumeClickFlagSwapsAndClears) {
    ResetAll();
    g_pendingClick = 0x800;
    int r = Window_ConsumeClickFlag();
    CHECK_EQ(r, 0x800);
    CHECK_EQ(g_lastClick, 0x800);
    CHECK_EQ(g_pendingClick, 0);
    // Second consume returns 0.
    CHECK_EQ(Window_ConsumeClickFlag(), 0);
    CHECK_EQ(g_lastClick, 0);
}

// ---------------------------------------------------------------------------
// VIBE_Gui_HitTestWindow — scan widget array (high-water down) for a window frame.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, HitTestWindowFindsContainingFrame) {
    ResetAll();
    // Build a window-backing widget at slot 3 covering (10..110, 20..70) with the
    // interior band y in [20..70] (clipY0/clipY1 == +32/+34), enabled, id 1027.
    int idx = 3;
    Widget& w = g_widgets[idx];
    w.x() = 10;  w.w() = 100;     // x 10..110
    w.y() = 20;  w.h() = 50;      // y 20..70
    w.type() = kTypeWindow;       // 64
    w.inUse() = 1;
    w.clipY0() = 70;              // band upper (clipX1/+32 in original read order)
    w.clipY1() = 20;              // band lower (+34) — note original's >= +30 / <= +32
    // The original tests: py >= word@+32 (clipY0 accessor) && py <= word@+34 (clipY1).
    // Set so that 45 is inside: clipY0=20, clipY1=70.
    w.clipY0() = 20;
    w.clipY1() = 70;
    w.disabledB() = 0;
    w.id() = 1027;
    g_widgetHighWater = 5;        // scan from 5 down; slot 3 matches

    int hit = Gui_HitTestWindow(50, 45);
    CHECK_EQ(hit, 1027);
    CHECK_EQ(g_hitTestSlot, 3);

    // A point outside the x band misses.
    int miss = Gui_HitTestWindow(200, 45);
    CHECK_EQ(miss, -1);
    CHECK_EQ(g_hitTestSlot, -1);
}

TEST(GuiDialogs3, HitTestWindowDisabledHighWaterMiss) {
    ResetAll();
    g_widgetHighWater = -1;       // empty
    CHECK_EQ(Gui_HitTestWindow(0, 0), -1);
}

// ---------------------------------------------------------------------------
// VIBE_Gui_HitTestObject — scan the widget pointer cache top-down.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, HitTestObjectFindsWidgetInCache) {
    ResetAll();
    // Allocate a widget; AllocSlot records it in the cache.
    int slot = Widget_AllocSlot();
    Widget& w = g_widgets[slot];
    w.x() = 0;   w.w() = 200;   // x 0..200
    w.y() = 0;   w.h() = 100;   // y 0..100
    w.clipY0() = 0;             // +32
    w.clipY1() = 100;           // +34
    w.disabledB() = 0;          // +56
    w.renderPtr() = 0;          // +52
    w.parentClip() = 0;         // +60 (no owner-state block)
    w.marker() = slot;          // +0 == own slot
    w.type() = kTypeLabel;      // owner-of-marker type != 64 -> widget path
    w.id() = 4242;              // +8
    w.groupLink() = 0;          // +44 (no group)

    int r = Gui_HitTestObject(50, 50);
    CHECK_EQ(r, 4242);
    CHECK_EQ(g_hitTestSlot, slot);
    CHECK_EQ(g_hitTestPayload, -1); // no group link

    // Miss outside.
    int m = Gui_HitTestObject(500, 50);
    CHECK_EQ(m, -1);
    CHECK_EQ(g_hitTestSlot, -1);
}

TEST(GuiDialogs3, HitTestObjectWindowBackedReturnsOwner) {
    ResetAll();
    int slot = Widget_AllocSlot();
    Widget& w = g_widgets[slot];
    w.x() = 0; w.w() = 50; w.y() = 0; w.h() = 50;
    w.clipY0() = 0; w.clipY1() = 50;
    w.marker() = slot;
    w.type() = kTypeWindow;     // owner-of-marker type == 64 -> object-state path
    w.ownerWindow() = 9;        // +116
    int r = Gui_HitTestObject(10, 10);
    CHECK_EQ(r, 9);
    CHECK_EQ(g_hitTestPayload, 9);
}

// ---------------------------------------------------------------------------
// VIBE_Window_PositionAtCoord — anchor math + Widget_LayoutBounds edge.
// golden: centred w=100,x=50,centre=320,div=2.0,ext=640<<16 -> x=210.
// ---------------------------------------------------------------------------
TEST(GuiDialogs3, PositionAtCoordCentredAnchor) {
    ResetAll();
    int ws = 2;
    Window& win = g_windows[ws];
    win.w() = 100;
    win.x() = 50;
    win.backWidget() = 7;
    g_centreRefX = 320;
    g_anchorDivisor = 2.0f;
    g_screenClipExt = 640 << 16;  // HIWORD = 640 max x

    Window_PositionAtCoord(ws, /*corner*/ 1);
    CHECK_EQ(g_layoutCalls, 1);
    CHECK_EQ(g_lastLayout.idx, 7);
    CHECK_EQ(g_lastLayout.x, 210);
}

TEST(GuiDialogs3, PositionAtCoordNonCentredUsesWindowX) {
    ResetAll();
    int ws = 1;
    Window& win = g_windows[ws];
    win.w() = 80;
    win.x() = 33;
    win.backWidget() = 4;
    g_screenClipExt = 640 << 16;
    Window_PositionAtCoord(ws, /*corner*/ 0);
    CHECK_EQ(g_lastLayout.x, 33);
    CHECK_EQ(g_lastLayout.idx, 4);
}

TEST(GuiDialogs3, PositionAtCoordClampsToScreen) {
    ResetAll();
    int ws = 1;
    Window& win = g_windows[ws];
    win.w() = 100;
    win.x() = 600;               // would push off a 640-wide screen
    win.backWidget() = 2;
    g_screenClipExt = 640 << 16; // maxX = 640 - 100 = 540
    Window_PositionAtCoord(ws, 0);
    CHECK_EQ(g_lastLayout.x, 540);
}

TEST(GuiDialogs3, PositionAtCoordThunkResolvesFormWindow) {
    ResetAll();
    int formId = 2, ws = 5;
    g_forms[formId].windowId(0) = ws;
    Window& win = g_windows[ws];
    win.w() = 40; win.x() = 12; win.backWidget() = 8;
    g_screenClipExt = 640 << 16;
    Window_PositionAtCoord_Thunk(formId, 0);
    CHECK_EQ(g_lastLayout.x, 12);
    CHECK_EQ(g_lastLayout.idx, 8);
}

// ---------------------------------------------------------------------------
// VIBE_Widget_CreateRawBitmap — slot init + pixel copy via the AllocDebug hook.
// ---------------------------------------------------------------------------
namespace {
unsigned char g_rawArena[4096];
int           g_rawAllocSize;
void* CaptureAlloc(int size, const char* /*tag*/) {
    g_rawAllocSize = size;
    if (size <= 0 || size > (int)sizeof(g_rawArena)) return nullptr;
    std::memset(g_rawArena, 0, sizeof(g_rawArena));
    return g_rawArena;
}
}

TEST(GuiDialogs3, CreateRawBitmapInitsFieldsAndCopiesPixels) {
    ResetAll();
    GuiDialogs3Hooks hooks{};
    hooks.AllocDebug = &CaptureAlloc;
    const GuiDialogs3Hooks* prev = SetGuiDialogs3Hooks(&hooks);

    g_screenClipExt = (300 << 16) | 7; // HIWORD 300, LOWORD 7

    unsigned short pixels[3 * 2]; // w=3, h=2 -> 2*3*2 = 12 bytes
    for (int i = 0; i < 6; ++i) pixels[i] = (unsigned short)(0x1000 + i);

    int slot = Widget_CreateRawBitmap(/*x*/ 11, /*y*/ 22, /*w*/ 3, /*h*/ 2, pixels);
    CHECK(slot >= 0);
    Widget& w = g_widgets[slot];
    CHECK_EQ(w.at<guild::u16>(16), (guild::u16)11); // x
    CHECK_EQ(w.at<guild::u16>(18), (guild::u16)22); // y
    CHECK_EQ(w.at<guild::u16>(20), (guild::u16)3);  // w
    CHECK_EQ(w.at<guild::u16>(22), (guild::u16)2);  // h
    CHECK_EQ(w.type(), (guild::u8)71);              // 'G'
    CHECK_EQ(w.id(), -1);
    CHECK_EQ(w.at<guild::u16>(34), (guild::u16)7);   // LOWORD(clipExt)
    CHECK_EQ(w.at<guild::u16>(30), (guild::u16)300); // HIWORD(clipExt)
    CHECK_EQ(g_rawAllocSize, 2 * 2 * 3);             // 2*h*w = 12
    CHECK_EQ(std::memcmp(WidgetData(slot), pixels, 12), 0);

    SetGuiDialogs3Hooks(prev);
}

// ---------------------------------------------------------------------------
// VIBE_Form_RefreshIfVisible — guard logic + hook sequence.
// ---------------------------------------------------------------------------
namespace {
int g_bcast, g_anim, g_present;
void H_Bcast(int) { ++g_bcast; }
void H_Anim(int, const char*, const char*) { ++g_anim; }
void H_Present(int) { ++g_present; }
}

TEST(GuiDialogs3, RefreshIfVisibleNotLoadedReturns0) {
    ResetAll();
    int f = 1;
    g_forms[f].valid() = 0;
    CHECK_EQ(Form_RefreshIfVisible(f, "x", "y"), 0);
}

TEST(GuiDialogs3, RefreshIfVisibleNullArgsReturns1NoEdges) {
    ResetAll();
    g_bcast = g_anim = g_present = 0;
    GuiDialogs3Hooks h{};
    h.FormBroadcastClickResult = &H_Bcast;
    h.FormSetButtonAnimations = &H_Anim;
    h.FormPresent = &H_Present;
    const GuiDialogs3Hooks* prev = SetGuiDialogs3Hooks(&h);

    int f = 2;
    g_forms[f].valid() = 1;
    g_forms[f].shownFlag() = 1;
    CHECK_EQ(Form_RefreshIfVisible(f, nullptr, "y"), 1);
    CHECK_EQ(g_bcast, 0);

    SetGuiDialogs3Hooks(prev);
}

TEST(GuiDialogs3, RefreshIfVisibleRunsAllEdges) {
    ResetAll();
    g_bcast = g_anim = g_present = 0;
    GuiDialogs3Hooks h{};
    h.FormBroadcastClickResult = &H_Bcast;
    h.FormSetButtonAnimations = &H_Anim;
    h.FormPresent = &H_Present;
    const GuiDialogs3Hooks* prev = SetGuiDialogs3Hooks(&h);

    int f = 3;
    g_forms[f].valid() = 1;
    g_forms[f].shownFlag() = 1;
    CHECK_EQ(Form_RefreshIfVisible(f, "cx", "cy"), 1);
    CHECK_EQ(g_bcast, 1);
    CHECK_EQ(g_anim, 1);
    CHECK_EQ(g_present, 1);

    SetGuiDialogs3Hooks(prev);
}
