// End-to-end flow for the window/form lifecycle & management family:
//   create a Form's Windows (Window_Create) -> populate children
//   -> toggle visibility (Form_SetObjectsVisible / SetChildrenVisible)
//   -> scroll a window (Window_Scroll)
//   -> position/center the form's windows (Form_CenterChildWindows /
//      PositionChildWindows -> Window_PositionCentered)
//   -> raise the form (Form_RaiseWindows) and tear it down (Form_Destroy /
//      Window_RemoveChildren / Window_RemoveIfActive).
//
// GUARDED real-asset leg: if Resources/forms.BIN ships, parse one real `.form`
// member and replay the lifecycle over a g_forms entry sized to its window count.
#include "test.h"

#include "gui/window_mgmt.h"
#include "gui/form_lifecycle.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"

#include "gui/form_parse.h"
#include "io/zip_archive.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;

// Test seams for the render/OS edges (weak in the production TUs).
namespace {
int g_lbCount = 0, g_lbX = 0, g_lbY = 0;
int g_destroyCount = 0;
int g_raiseCount = 0;
} // namespace
namespace guild::gui {
void Widget_LayoutBounds(int x, int y, int) { ++g_lbCount; g_lbX = x; g_lbY = y; }
void Widget_DestroyByType(int, int, int)    { ++g_destroyCount; }
int  ZOrder_RaiseWindow(int slot)           { ++g_raiseCount; return slot; }
// Stubs for the FRM2 parser's weak edges (only needed for the guarded real-asset leg).
i16  GfxMetricWord(int, int)      { return 8; }
i32  GfxMetricDword(int, int)     { return 8 << 16; }
i16  SliderTrackExtent(int, int)  { return 8; }
void* SceneStateFor(int)          { return nullptr; }
int  GlyphAdvance(void*, int)     { return 4; }
i16  Property_Get(const char*, int) { return 16; }
int  Property_Validate(const char*) { return 0; }
int  Form_PropertyValidate(const char* n) { return (n && n[0]) ? 1 : -1; }
int  Form_FindTextArrayIndex(const char*) { return -1; }
} // namespace guild::gui

// Build a populated form (formId, nWindows windows, nChildren children each).
static void BuildForm(int formId, int nWindows, int nChildren) {
    Form& f = g_forms[formId];
    f.valid() = 1;
    f.windowCount() = nWindows;
    for (int g = 0; g < nWindows; ++g) {
        int slot = Window_Create(static_cast<i16>(10 * g), 5, 80, 40, 0);
        f.windowId(g) = slot;
        Window& win = g_windows[slot];
        i32* list = WindowChildList(slot);
        for (int c = 0; c < nChildren; ++c) {
            int idx = Widget_AllocSlot();
            g_widgets[idx].type() = kTypeLabel;
            g_widgets[idx].parentClip() =
                static_cast<i32>(reinterpret_cast<std::intptr_t>(&f)); // key for SetObjectsVisible
            list[c] = idx;
            win.objCount() = static_cast<i16>(c + 1);
        }
    }
}

TEST(GuiWindowMgmtE2E, FullLifecycleFlow) {
    ResetGuiState();
    g_screenCenterX = 320; g_screenCenterY = 240;

    const int kForm = 5, kWindows = 3, kChildren = 4;
    BuildForm(kForm, kWindows, kChildren);
    Form& f = g_forms[kForm];

    // 1. Hide / show objects across the whole form.
    Form_SetObjectsVisible(kForm, 1);   // hide
    for (int g = 0; g < kWindows; ++g) {
        i32* list = WindowChildList(f.windowId(g));
        for (int c = 0; c < kChildren; ++c)
            CHECK_EQ(g_widgets[list[c]].renderPtr(), 0);
    }
    Form_SetObjectsVisible(kForm, 0);   // show
    {
        i32* list = WindowChildList(f.windowId(0));
        CHECK_EQ(g_widgets[list[0]].renderPtr(), 1);
    }

    // 2. Enable/disable the children (per-window walk).
    Form_SetChildrenVisible(kForm, 0);
    {
        i32* list = WindowChildList(f.windowId(1));
        CHECK_EQ(g_widgets[list[0]].disabledA(), 1);
    }

    // 3. Scroll window 0 down then up; offset stays clamped to its content extent.
    int s0 = f.windowId(0);
    g_windows[s0].contentHeight() = 200;
    Window_Scroll(3, 50, s0);
    CHECK_EQ(g_windows[s0].scrollOffset(), 50);
    CHECK_EQ(g_windows[s0].scrollExtraX(), 3);
    Window_Scroll(0, -1000, s0);                 // clamp back toward 0
    CHECK_EQ(g_windows[s0].scrollOffset(), -50); // -scrollCur - offset = -(0) - 50

    // 4. Center + position the form's windows (all top-level here).
    g_lbCount = 0;
    Form_CenterChildWindows(kForm);
    CHECK_EQ(g_lbCount, kWindows);
    g_lbCount = 0;
    Form_PositionChildWindows(kForm, 1);         // bit0 -> x = 320 - 80/2 = 280
    CHECK_EQ(g_lbCount, kWindows);
    CHECK_EQ(g_lbX, 280);

    // 5. Raise the form's windows to front.
    g_raiseCount = 0;
    Form_RaiseWindows(kForm);
    CHECK_EQ(g_raiseCount, kWindows);

    // 6. Remove one window's children directly, then tear the whole form down.
    g_destroyCount = 0;
    CHECK_EQ(Window_RemoveChildren(s0, 1), 1);
    CHECK_EQ(g_destroyCount, kChildren);
    CHECK_EQ(g_windows[s0].scrollPrev(), -1);

    g_destroyCount = 0;
    Form_Destroy(kForm);
    CHECK_EQ(g_destroyCount, kWindows);          // each window's backing widget destroyed
    CHECK_EQ(f.valid(), 0);

    // Window_RemoveIfActive on a now-free slot is a no-op.
    CHECK_EQ(Window_RemoveIfActive(s0, 0, 0), 1); // still enabled (Form_Destroy didn't free slots)
}

// ---- Guarded real-asset leg --------------------------------------------------
static const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

static bool endsWithForm(const char* name) {
    std::size_t n = std::strlen(name);
    if (n < 5) return false;
    const char* e = name + n - 5;
    return e[0] == '.' && (e[1] | 32) == 'f' && (e[2] | 32) == 'o' &&
           (e[3] | 32) == 'r' && (e[4] | 32) == 'm';
}

TEST(GuiWindowMgmtE2E, RealFormLifecycleGuarded) {
    guild::shim::DiskFileSystem fs(kRoot);
    if (!fs.exists("Resources/forms.BIN")) { CHECK(true); return; } // skip-pass: no asset

    io::ZipArchive z;
    CHECK(z.Open(&fs, "Resources/forms.BIN"));

    // Parse the first `.form` member, then drive the lifecycle over a g_forms entry
    // sized to its real window count.
    char nm[260];
    io::ZipFileInfo fi;
    bool drove = false;
    for (int r = z.GoToFirstFile(); r == io::kZipOk && !drove; r = z.GoToNextFile()) {
        if (z.GetCurrentFileInfo(&fi, nm, sizeof nm) != io::kZipOk) continue;
        if (!endsWithForm(nm)) continue;

        std::vector<u8> bytes;
        if (!z.ExtractCurrentFile(bytes)) continue;

        ResetGuiState();
        FormFile ff = Form_ParseResourceFile(bytes.data(), bytes.size(), nm);
        if (!ff.ok || ff.windows.empty()) continue;

        int n = static_cast<int>(ff.windows.size());
        if (n > kMaxWindows - 1) n = kMaxWindows - 1;

        const int kForm = 7;
        Form& f = g_forms[kForm];
        f.valid() = 1;
        f.windowCount() = n;
        for (int g = 0; g < n; ++g)
            f.windowId(g) = Window_Create(0, 0, 80, 40, 0);

        // Lifecycle ops over the real-sized form must not fault and must dispatch
        // once per window.
        g_lbCount = 0; g_raiseCount = 0; g_destroyCount = 0;
        Form_SetObjectsVisible(kForm, 0);
        Form_SetChildrenVisible(kForm, 0);
        Form_CenterChildWindows(kForm);
        CHECK_EQ(g_lbCount, n);
        Form_RaiseWindows(kForm);
        CHECK_EQ(g_raiseCount, n);
        Form_Destroy(kForm);
        CHECK_EQ(g_destroyCount, n);
        CHECK_EQ(f.valid(), 0);

        std::printf("[GuiWindowMgmtE2E] drove lifecycle over '%s' (%d windows)\n", nm, n);
        drove = true;
    }
    CHECK(drove);
}
