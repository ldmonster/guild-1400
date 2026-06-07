// Integration: drive gui_dialogs3's VIBE_Widget_CreateRawBitmap (0x412560)
// against the REAL reconstructed memory sibling, wired exactly as the live engine
// forwards it. The original's CreateRawBitmap allocates the raw-bitmap pixel block
// via VIBE_Memory_AllocDebug (0x438f10) and qmemcpy's the caller's pixels into it.
// This reimpl routes that allocation through GuiDialogs3Hooks.AllocDebug; we
// forward that hook straight into a REAL guild::mem::MemoryTracker::AllocDebug
// (mem/memory_debug.cpp) backed by a REAL guild::mem::Heap (mem/heap.cpp) — the
// same guarded allocator the binary uses — and assert the cross-module flow: the
// widget is created as a 'G' raw bitmap, the tracker accounts one guarded block of
// 2*w*h bytes, and the pixels survive the real qmemcpy into that block.
//
// The remaining gui_dialogs3 leaves (Form broadcast / button-anim / present) have
// no reconstructed sibling. The ConsumeClickFlag / ClipRectToBuffers /
// SetTooltipText tests below exercise the module's pure deterministic globals and
// its INERT default-hook path end to end (noted inline).
#include "test.h"

#include "gui/gui_dialogs3.h"
#include "gui/object.h"
#include "gui/widget_create.h"
#include "gui/form.h"
#include "mem/memory_debug.h"   // REAL sibling: MemoryTracker
#include "mem/heap.h"           // REAL sibling: Heap

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {
// The live allocator, owned by the test. CreateRawBitmap's AllocDebug edge is
// forwarded into the genuine MemoryTracker (guard-word accounting and all).
mem::MemoryTracker* g_tracker = nullptr;

void* AllocHook(int size, const char* tag) {
    if (!g_tracker) return nullptr;
    return g_tracker->AllocDebug(static_cast<u32>(size), tag);
}
} // namespace

// CreateRawBitmap routes its pixel allocation through the REAL MemoryTracker. We
// assert the widget metadata, the tracker accounting, and the real qmemcpy result.
TEST(GuiDialogs3Itest, CreateRawBitmapAllocatesThroughRealTracker) {
    ResetGuiDialogs3();

    mem::Heap heap;
    mem::MemoryTracker tracker(heap);
    tracker.Init(256);
    g_tracker = &tracker;

    GuiDialogs3Hooks h{};
    h.AllocDebug = &AllocHook;   // -> real MemoryTracker::AllocDebug
    const GuiDialogs3Hooks* prev = SetGuiDialogs3Hooks(&h);

    const i16 w = 4, ht = 3;
    const int bytes = 2 * w * ht;            // 24 bytes (2 bytes/pixel)
    unsigned char pixels[24];
    for (int i = 0; i < bytes; ++i) pixels[i] = static_cast<unsigned char>(i + 1);

    CHECK_EQ(tracker.CurBlocks(), 0);
    int slot = Widget_CreateRawBitmap(/*x*/ 10, /*y*/ 20, w, ht, pixels);

    SetGuiDialogs3Hooks(prev);
    g_tracker = nullptr;

    CHECK(slot >= 0);
    if (slot >= 0) {
        Widget& wg = g_widgets[slot];
        CHECK_EQ((int)wg.type(), 71);            // 'G' raw bitmap
        CHECK_EQ((int)wg.id(), -1);
        CHECK_EQ((int)wg.at<u16>(20), (int)w);   // +20 = width
        CHECK_EQ((int)wg.at<u16>(22), (int)ht);  // +22 = height/row count

        // The REAL tracker accounted exactly one guarded block.
        CHECK_EQ(tracker.CurBlocks(), 1);
        // The pixel block lives in the parallel data table; the real qmemcpy copied
        // the caller's bytes into the guarded allocation.
        unsigned char* buf = reinterpret_cast<unsigned char*>(WidgetData(slot));
        CHECK(buf != nullptr);
        if (buf) {
            bool match = true;
            for (int i = 0; i < bytes; ++i)
                if (buf[i] != static_cast<unsigned char>(i + 1)) match = false;
            CHECK(match);
            // The block came from the real allocator -> it is a valid tracked pointer.
            CHECK(tracker.IsValidPointer(buf));
        }
    }
}

// ConsumeClickFlag is a pure read-and-clear of the module globals — no hooks. It
// snapshots the pending flag into g_lastClick, returns it, and clears the pending.
TEST(GuiDialogs3Itest, ConsumeClickFlagReadAndClear) {
    ResetGuiDialogs3();

    g_pendingClick = 7;
    g_lastClick = 0;
    int r = Window_ConsumeClickFlag();

    CHECK_EQ(r, 7);             // returns the snapshot
    CHECK_EQ(g_lastClick, 7);   // snapshotted
    CHECK_EQ(g_pendingClick, 0);// cleared

    // A second consume now reads the cleared flag.
    CHECK_EQ(Window_ConsumeClickFlag(), 0);
}

// ClipRectToBuffers clamps the rect to the active draw extent and appends it to
// both clip buffers — pure deterministic global math, no hooks.
TEST(GuiDialogs3Itest, ClipRectClampsAndPushesBothBuffers) {
    ResetGuiDialogs3();

    g_drawClipTop = 10;
    g_drawClipBottom = 100;
    g_clipRectLen[0] = 0;
    g_clipRectLen[1] = 0;

    // y=5 (above top -> clamp to 10), h=200: y+h=205 > bottom 100 -> outH = 100-5 = 95;
    // then y<top shaves (10-5)=5 -> final outH = 90. flags bit0 clear -> x kept, outX=flags.
    int r = Gui_ClipRectToBuffers(/*flags*/ 0, /*y*/ 5, /*h*/ 200, /*x*/ 40);

    CHECK_EQ(r, 8);                       // 2 buffers * 4
    CHECK_EQ(g_clipRectLen[0], 1);
    CHECK_EQ(g_clipRectLen[1], 1);
    for (int b = 0; b < 2; ++b) {
        CHECK_EQ(g_clipRectBuf[b][0].y, 10);   // clamped to top
        CHECK_EQ(g_clipRectBuf[b][0].h, 90);   // 95 - (10-5)
        CHECK_EQ(g_clipRectBuf[b][0].w, 40);   // x kept (flag clear)
        CHECK_EQ(g_clipRectBuf[b][0].x, 0);    // outX == flags == 0
    }
}

// SetTooltipText copies the source string (incl. NUL) into the global tooltip
// buffer — pure global mutation, no hooks.
TEST(GuiDialogs3Itest, SetTooltipTextCopiesIntoGlobalBuffer) {
    ResetGuiDialogs3();

    char last = Widget_SetTooltipText("hello");
    CHECK_EQ((int)last, 0);                      // returns the final (NUL) byte
    CHECK(std::strcmp(g_tooltipText, "hello") == 0);
}

// Form_RefreshIfVisible runs through the module's INERT default-hook path: with no
// hooks installed the not-loaded form returns 0 before reaching any (defaulted)
// renderer edge, so the inert defaults are exercised end to end (no reconstructed
// sibling exists for the Form broadcast/anim/present leaves).
TEST(GuiDialogs3Itest, FormRefreshInertWhenNotVisible) {
    ResetGuiDialogs3();
    SetGuiDialogs3Hooks(nullptr);   // inert defaults

    // g_forms[0] is zeroed -> not loaded / not shown -> early 0 (no hook fires).
    g_forms[0] = Form{};
    int r = Form_RefreshIfVisible(0, "x", "y");
    CHECK_EQ(r, 0);
}
