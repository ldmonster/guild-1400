// End-to-end flow across the gui_dialogs3 leaves: build a window with a raw-bitmap
// child, anchor the window, blit the bitmap into a surface clipped to the active draw
// extent, hit-test a point onto the child, and consume the resulting click flag.
#include "test.h"

#include "gui/gui_dialogs3.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/form.h"
#include "gui/window_render.h"
#include "gui/widget_create.h"
#include "gui/hud.h"

#include <cstring>
#include <vector>

using namespace guild::gui;

namespace {

int g_e2eLayoutX, g_e2eLayoutIdx, g_e2eLayoutCalls;
unsigned char g_e2eArena[8192];
int g_e2eAllocSize;
int g_e2eAttachWidget, g_e2eAttachWin, g_e2eAttachCalls;

void* E2EAlloc(int size, const char*) {
    g_e2eAllocSize = size;
    if (size <= 0 || size > (int)sizeof(g_e2eArena)) return nullptr;
    std::memset(g_e2eArena, 0, sizeof(g_e2eArena));
    return g_e2eArena;
}

} // namespace

// Strong overrides of the two weak cross-module edges so the flow is observable in
// isolation (the unified build keeps the weak inert defaults).
namespace guild::gui {
void Widget_LayoutBounds(int x, int /*y*/, int widgetIdx) {
    g_e2eLayoutX = x; g_e2eLayoutIdx = widgetIdx; ++g_e2eLayoutCalls;
}
void GameObject_AttachToWindow(int widgetIdx, int winSlot) {
    g_e2eAttachWidget = widgetIdx; g_e2eAttachWin = winSlot; ++g_e2eAttachCalls;
}
} // namespace guild::gui

TEST(GuiDialogs3E2E, BuildAnchorBlitHitConsumeFlow) {
    ResetGuiState();
    ResetGuiDialogs3();
    ResetWidgetCreate();
    g_e2eLayoutCalls = g_e2eAttachCalls = 0;

    GuiDialogs3Hooks hooks{};
    hooks.AllocDebug = &E2EAlloc;
    const GuiDialogs3Hooks* prev = SetGuiDialogs3Hooks(&hooks);

    g_screenClipExt = (640 << 16) | 0; // HIWORD = 640 (max x), LOWORD 0

    // --- 1) Set up a window slot manually (the data-model core lives elsewhere). ----
    int ws = 4;
    Window& win = g_windows[ws];
    win.x() = 100;  // word@+4
    win.y() = 30;   // word@+6
    win.w() = 120;
    win.h() = 80;
    win.backWidget() = 0; // assigned below
    win.enabled() = 1;

    // --- 2) Add a raw-bitmap child to the window (relative coords -> absolute). -----
    //     A 4x3 16bpp image: 2*w*h = 2*4*3 = 24 bytes.
    unsigned short img[4 * 3];
    for (int i = 0; i < 12; ++i) img[i] = (unsigned short)(0x2000 + i);
    int bmp = Widget_AddRawBitmapToWindow(/*x*/ 5, /*y*/ 7, /*w*/ 4, /*h*/ 3, img, ws);
    CHECK(bmp >= 0);
    CHECK_EQ(g_e2eAttachCalls, 1);
    CHECK_EQ(g_e2eAttachWidget, bmp);
    CHECK_EQ(g_e2eAttachWin, ws);
    // Absolute position = window origin + relative.
    CHECK_EQ(g_widgets[bmp].at<guild::u16>(16), (guild::u16)(100 + 5)); // x
    CHECK_EQ(g_widgets[bmp].at<guild::u16>(18), (guild::u16)(30 + 7));  // y
    CHECK_EQ(g_e2eAllocSize, 24);
    CHECK_EQ(std::memcmp(WidgetData(bmp), img, 24), 0);

    // --- 3) Anchor the window's backing widget (use the bitmap as backing for the
    //        flow; centred anchor about 320 with divisor 2). --------------------------
    win.backWidget() = bmp;
    g_centreRefX = 320;
    g_anchorDivisor = 2.0f;
    Window_PositionAtCoord(ws, /*corner*/ 1);
    CHECK_EQ(g_e2eLayoutCalls, 1);
    CHECK_EQ(g_e2eLayoutIdx, bmp);
    // anchor = (120/2+100) - 320/2 + 320 - 120/2 = 160 -160 +320 -60 = 260; maxX=640-120=520
    CHECK_EQ(g_e2eLayoutX, 260);

    // --- 4) Clip a draw rect (the child band) to the active extent, push to buffers. -
    g_drawClipTop = 20;
    g_drawClipBottom = 200;
    int n = Gui_ClipRectToBuffers(/*flags*/ 0, /*y*/ 10, /*h*/ 300, /*x*/ 0);
    CHECK_EQ(n, 8);
    CHECK_EQ(g_clipRectBuf[0][0].y, 20);   // clamped to top
    CHECK_EQ(g_clipRectBuf[0][0].h, 180);  // 200-10=190 then -10 = 180
    CHECK_EQ(g_clipRectLen[0], 1);

    // --- 5) Blit the bitmap rows into a destination surface, clipped to its band. ----
    //     Configure the child widget's clip band so all 3 rows copy.
    Widget& bw = g_widgets[bmp];
    bw.y() = 0;          // dst top row
    bw.h() = 3;          // 3 rows
    bw.w() = 4;          // 4 px/row -> 8 bytes/row
    bw.clipY0() = 0;     // top clamp
    bw.clipY1() = 100;   // bottom clamp
    bw.x() = 0;          // dst x

    std::vector<unsigned char> surface(2 * 4 * 8, 0xEE); // 4-px stride * 8 rows, 16bpp
    int dstStridePx = 4;
    int rowBytes = Widget_BlitClippedRows(bmp, dstStridePx, surface.data());
    CHECK_EQ(rowBytes, 2 * 4); // 2*w
    // Row 0 of the surface must equal the first 8 source bytes.
    CHECK_EQ(std::memcmp(surface.data(), WidgetData(bmp), 8), 0);

    // --- 6) Hit-test a point onto the child via the pointer cache. -------------------
    bw.x() = 0; bw.w() = 200; bw.y() = 0; bw.h() = 100;
    bw.clipY0() = 0; bw.clipY1() = 100;
    bw.disabledB() = 0; bw.renderPtr() = 0; bw.parentClip() = 0;
    bw.marker() = bmp; bw.type() = kTypeLabel; bw.id() = 0xABCD; bw.groupLink() = 0;
    int hitId = Gui_HitTestObject(40, 40);
    CHECK_EQ(hitId, 0xABCD);
    CHECK_EQ(g_hitTestSlot, bmp);

    // --- 7) A click was registered by the dispatch; consume the flag. ----------------
    g_pendingClick = kClickFlagInfoPanel; // 0x100
    int consumed = Window_ConsumeClickFlag();
    CHECK_EQ(consumed, 0x100);
    CHECK_EQ(g_pendingClick, 0);
    CHECK_EQ(g_lastClick, 0x100);

    SetGuiDialogs3Hooks(prev);
}
