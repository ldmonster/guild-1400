#include "gui/gui_dialogs7.h"

#include "gui/gui_dialogs5.h"     // g_forceQuitLatch (owned there)
#include "gui/object.h"           // g_widgets, Widget
#include "gui/window.h"           // g_windows, g_currentWindowId
#include "gui/form.h"             // Form_SelectWindow, Form_GetChildObjectId (real)
#include "gui/form_lifecycle.h"   // Form_CenterChildWindows, Form_Destroy (real)
#include "gui/window_render.h"    // Window_RenderUpdates (real)
#include "gui/window_mgmt.h"      // Window_RemoveChildren (real)

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace guild::gui {

// ===========================================================================
// Module-owned engine tables (BSS, zero at load).
// ===========================================================================
WidgetSystemTables g_widgetSys{};
i32  g_widgetSysCfg0 = 0;   // dword_62D308
i16  g_widgetSysCfg1 = 0;   // word_62D310
i32  g_useObjLastClicked = -1; // dword_75BF38
i32  g_fadeSlots[32] = {0};  // dword_672280

// dword_69FFBC — a packed (lo,hi) default-state template the original copies into a
// freshly-init'd widget's +34 / +30 words. BSS, zero at load.
static i32 g_widgetStateTemplate = 0; // dword_69FFBC

// dword_672230 — right-click / cancel edge. The frame loops read it; tests drive it
// through the readMouseRelease hook. The shared dword_631614 force-quit latch is the
// extern g_forceQuitLatch from gui_dialogs5.cpp.

// byte_676580 — base of the radio-group surface descriptor blocks (140-byte stride).
// Owned by the radio-group module in the original; only the FreeSurface thunk touches
// it from here, so we model the base address argument that gets handed to the surface
// release path. We keep a small in-module backing so the thunk is observable.
static unsigned char g_radioSurfaceBlocks[140 * 8] = {0}; // byte_676580 (8 groups modeled)

// ===========================================================================
// Hooks (inert defaults). Defaults keep every builder observable headless and make
// every frame loop terminate immediately (gameLogicRunFrameLoop returns 0).
// ===========================================================================
namespace {

int  DefStateUpdate(int) { return 0; }
int  DefGuiMarkObjectUsed(int) { return 0; }
void DefGameLogicMovement(int) {}
int  DefGameLogicObjects(int, short) { return -1; }

int  DefSurfaceCreate(void*, int, int) { return 0; }
void DefLightSetGrayThunk(int, int, int) {}
void DefSurfaceReleaseTexture(int) {}
int  DefRenderFillBackBuffer(int, int) { return 0; }   // 0 -> "could not open" path
int  DefSurfaceColorFill(int, int) { return 0; }
int  DefDecompressStateBlob(int, int) { return 0; }    // 0 -> skip the fill/finalize
void DefDecompressionFinalize(int) {}
void DefFadeUpdate(int, int) {}
void DefRenderPresentFrame(void*) {}
void DefCoordPush(int, int, int, int) {}
void DefAnimationBasic(int, int, int, int, int) {}
void DefErrorLogReportMessage(const char*) {}

int  DefTextRenderRichString(unsigned, unsigned) { return 0; }

int  DefGameTickFinalize(i16, i16, const char*) { return -1; }
void DefDragCursorSetSprite(int, int) {}
// Default forwards to the REAL reconstructed sibling (the original calls it directly).
int  DefFormCenterChildWindows(int formId) { Form_CenterChildWindows(formId); return 0; }
int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; } // exit loop at once

int  DefIsWindowVisible(void*) { return 1; }
int  DefEnableWindow(void*, int) { return 0; }
int  DefDestroyWindow(void*) { return 1; }
int  DefUnregisterClassA(const char*, void*) { return 1; }
void* DefLoadLibraryA(const char*) { return nullptr; }
int  DefDialogBoxParamA(void*, int, void*, void*, int) { return 0; }
int  DefFreeLibrary(void*) { return 1; }
int  DefMessageBoxA(void*, int, int, int) { return 0; }
void* DefGetActiveWindow() { return nullptr; }
void* DefGetLastActivePopup(void*) { return nullptr; }

int  DefReadMouseRelease() { return 0; }

const GuiDialogs7Hooks kDefaultHooks = {
    &DefStateUpdate, &DefGuiMarkObjectUsed, &DefGameLogicMovement, &DefGameLogicObjects,
    &DefSurfaceCreate, &DefLightSetGrayThunk, &DefSurfaceReleaseTexture,
    &DefRenderFillBackBuffer, &DefSurfaceColorFill, &DefDecompressStateBlob,
    &DefDecompressionFinalize, &DefFadeUpdate, &DefRenderPresentFrame, &DefCoordPush,
    &DefAnimationBasic, &DefErrorLogReportMessage,
    &DefTextRenderRichString,
    &DefGameTickFinalize, &DefDragCursorSetSprite, &DefFormCenterChildWindows,
    &DefGameLogicRunFrameLoop,
    &DefIsWindowVisible, &DefEnableWindow, &DefDestroyWindow, &DefUnregisterClassA,
    &DefLoadLibraryA, &DefDialogBoxParamA, &DefFreeLibrary, &DefMessageBoxA,
    &DefGetActiveWindow, &DefGetLastActivePopup,
    &DefReadMouseRelease,
};

const GuiDialogs7Hooks* g_hooks = &kDefaultHooks;

} // namespace

const GuiDialogs7Hooks* SetGuiDialogs7Hooks(const GuiDialogs7Hooks* hooks) {
    const GuiDialogs7Hooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const GuiDialogs7Hooks* GuiDialogs7Hooks_Default() { return &kDefaultHooks; }
const GuiDialogs7Hooks& GuiDialogs7HooksActive() { return *g_hooks; }

void ResetGuiDialogs7() {
    std::memset(&g_widgetSys, 0, sizeof(g_widgetSys));
    g_widgetSysCfg0 = 0;
    g_widgetSysCfg1 = 0;
    g_useObjLastClicked = -1;
    std::memset(g_fadeSlots, 0, sizeof(g_fadeSlots));
    g_widgetStateTemplate = 0;
    std::memset(g_radioSurfaceBlocks, 0, sizeof(g_radioSurfaceBlocks));
    g_forceQuitLatch = 0;
    g_hooks = &kDefaultHooks;
}

// ===========================================================================
// 0x4200f8 — VIBE_Widget_InitSystem.
// Clears the two config slots, then builds the scratch surfaces. The original passes
// a 12-byte surface descriptor on the stack (v8/v9/v10 = format,width,height) to
// VIBE_Light_SetGrayColorThunk(0,64,&desc) before each VIBE_Surface_Create.
// ===========================================================================
int Widget_InitSystem() {
    const GuiDialogs7Hooks& h = *g_hooks;
    int desc[3] = {0, 0, 0}; // v8 (format) + v9/v10 (w/h) — the 12-byte descriptor

    g_widgetSysCfg0 = 0;                 // dword_62D308 = 0
    g_widgetSysCfg1 = 0;                 // word_62D310  = 0

    h.lightSetGrayThunk(0, 64, reinterpret_cast<std::intptr_t>(desc)); // seed 1x1 desc
    // v9 = v10 = v1 (uninitialized in the original — width/height left as-seeded)
    g_widgetSys.mainSurface = h.surfaceCreate(desc, 1, desc[1]); // dword_75BEE0

    // Three 128x128 slot surfaces with their two id columns set to -1.
    for (int i = 0; i < 3; ++i) {
        g_widgetSys.idA3[i] = -1;        // dword_75BF14[i]
        g_widgetSys.idB3[i] = -1;        // dword_75BF18[i]
        h.lightSetGrayThunk(0, 64, reinterpret_cast<std::intptr_t>(desc));
        desc[1] = 128;                   // v9 = 128
        desc[2] = 128;                   // v10 = 128
        g_widgetSys.surf3[i] = h.surfaceCreate(desc, 3, 0); // dword_75BF10[i]
    }

    g_widgetSys.flagF4 = -1;             // dword_75BEF4 = -1
    g_widgetSys.flagF8 = -1;             // dword_75BEF8 = -1
    h.lightSetGrayThunk(0, 64, reinterpret_cast<std::intptr_t>(desc));
    desc[1] = 256;                       // v9 = 256
    desc[2] = 256;                       // v10 = 256
    int result = h.surfaceCreate(desc, 3, 0); // dword_75BEFC
    g_widgetSys.surfE = result;

    // Second 6-row (surf=-1, flag=0) table; the original walks v7: 12,24,..,72.
    for (int slot = 1; slot <= 6; ++slot) {
        g_widgetSys.rowSurf[slot] = -1;  // dword_75B9E4[12*slot]
        g_widgetSys.rowFlag[slot] = 0;   // dword_75B9E8[12*slot]
    }
    return result;
}

// ===========================================================================
// 0x40ec08 — VIBE_Widget_InitFromState.
// The original walks the 740-byte widget record (740*a1 + dword_69FFB4) and the
// 84-byte object record (84*v7 + dword_62D204). We model: widget[widgetIdx] is the
// GUI widget; the object data record is read via the State_Update hook. The exact
// world/screen bound recompute touches renderer-owned object slots we don't fully
// reconstruct, so we faithfully reproduce the GUI-side writes and route the object
// reads through hooks; the +20/+22/+30/+34 word writes come from the state template.
// ===========================================================================
short Widget_InitFromState(int widgetIdx, int objId) {
    const GuiDialogs7Hooks& h = *g_hooks;
    if (widgetIdx < 0 || widgetIdx >= kMaxWidgets)
        return 0;
    Widget& w = g_widgets[widgetIdx];

    if (w.type() < 64)                       // *(v4+24) < 64
        h.gameLogicMovement(w.id());         // VIBE_GameLogic_Movement(*(v4+8))

    int st = h.stateUpdate(objId);           // VIBE_State_Update
    w.dataPtr() = st;                        // *(v6+12) = v5

    w.ownerWindow() = 0;                     // mirrors *(v6+116)=dword_62D2A4 (0 headless)

    // *(v6+20)/(+22) = object bounds words; modeled from the state template.
    w.w() = static_cast<i16>(g_widgetStateTemplate & 0xFFFF);
    w.h() = static_cast<i16>(g_widgetStateTemplate >> 16);
    w.clipY0() = 0;                          // *(v6+32) = 0
    w.clipX0() = 0;                          // *(v6+28) = 0
    w.order() = 0;                           // *(v6+26) [v9 word], cleared template path

    // *(v6+34) = (word)dword_69FFBC ; *(v6+30) = HIWORD(dword_69FFBC)
    short result = static_cast<short>(g_widgetStateTemplate >> 16);
    w.clipX1() = result;                     // *(v6+30) = HIWORD

    if (w.type() < 64)                       // (char)v8 < 64
        return static_cast<short>(h.guiMarkObjectUsed(w.id()));
    return result;
}

// ===========================================================================
// 0x41999c — VIBE_Widget_DrawBackgroundSprite.
// ===========================================================================
void Widget_DrawBackgroundSprite(int objId) {
    const GuiDialogs7Hooks& h = *g_hooks;
    int state;
    if (objId == -1 || (state = h.stateUpdate(objId)) == 0) {
        Gui_Nop2();
        return;
    }
    // The original saves the current clip rect (dword_64A1B4..C0), pushes the full
    // window rect, composites, then restores. We faithfully push (0,0,w,h) using the
    // current window's dimensions and restore afterward.
    int win = g_currentWindowId;
    int rw = 0, rh = 0;
    if (win >= 0 && win < kMaxWindows) { rw = g_windows[win].w(); rh = g_windows[win].h(); }
    h.coordPush(0, 0, rw, rh);
    if (h.decompressStateBlob(win, /*v5*/ 0)) {
        h.animationBasic(0, 0, state, win, /*pal dword_62D2A4*/ 0);
        h.decompressionFinalize(win);
    }
    h.coordPush(0, 0, rw, rh); // restore (original restores the saved rect)
}

// ===========================================================================
// 0x419a3c — VIBE_Window_SetBackgroundTexture.
// ===========================================================================
void Window_SetBackgroundTexture(const char* name, int winSlot, int /*a3*/) {
    const GuiDialogs7Hooks& h = *g_hooks;
    if (winSlot < 0 || winSlot >= kMaxWindows)
        return;
    Window& win = g_windows[winSlot];
    // v4[228] -> dword at byte offset 912 (228*4) of the 952-byte window record.
    i32& bgTex = win.at<i32>(912);
    if (bgTex) {
        h.surfaceReleaseTexture(bgTex);
        bgTex = 0;
    }
    if (name && name[0]) {
        int s = h.renderFillBackBuffer(winSlot, 1);
        bgTex = s;
        if (!s) {
            char buf[1024];
            std::snprintf(buf, sizeof(buf),
                          "d2_SetBackgroundTexture(): Could not open %s.BMP name", "");
            h.errorLogReportMessage(buf);
        }
        win.at<i32>(916) = 1056964608; // 0x3F000000 == 0.5f scale dword
    }
}

// ===========================================================================
// 0x41523c — VIBE_Window_RenderEntityScene.
// dword_62D210 = back surface, dword_62D218 = second surface (modeled as ids 0/1).
// ===========================================================================
int Window_RenderEntityScene(int /*a1*/, int a2) {
    const GuiDialogs7Hooks& h = *g_hooks;
    const int back = 0;   // dword_62D210
    const int second = 1; // dword_62D218

    if (h.decompressStateBlob(back, a2)) {
        h.surfaceColorFill(back, 0);
        h.decompressionFinalize(back);
    }
    for (int i = 0; i != 32; ++i) {
        if (g_fadeSlots[i])
            h.fadeUpdate(g_fadeSlots[i], back);
    }
    h.renderPresentFrame(reinterpret_cast<void*>(0x80));
    if (h.decompressStateBlob(back, /*v4*/ a2)) {
        h.surfaceColorFill(back, 0);
        h.decompressionFinalize(back);
    }
    return h.surfaceColorFill(second, 0);
}

// ===========================================================================
// 0x567170 — VIBE_Panel_ShowUseObject.
// ===========================================================================
int Panel_ShowUseObject(const char* text, char* a2) {
    const GuiDialogs7Hooks& h = *g_hooks;
    int form = h.gameTickFinalize(0, 0, "panel\\useobj");
    h.formCenterChildWindows(form);                // -> REAL Form_CenterChildWindows (default)
    Form_SelectWindow(form, /*v5*/ 0);             // REAL sibling
    h.textRenderRichString(reinterpret_cast<std::uintptr_t>("%s"),
                           reinterpret_cast<std::uintptr_t>(text));
    h.dragCursorSetSprite(/*v7*/ 0, 0);
    g_useObjLastClicked = -1;                       // dword_75BF38 = -1
    do {
        if (/*v8*/ 0 != h.readMouseRelease())       // v8 != dword_672230
            g_forceQuitLatch = 1;                   // dword_631614 = 1
    } while (h.gameLogicRunFrameLoop(423879, form, a2));
    Form_Destroy(form);                             // REAL sibling
    return 0;
}

// ===========================================================================
// 0x551374 — VIBE_Form_RunIdleLoop.
// ===========================================================================
int Form_RunIdleLoop(int a1, char* a2) {
    const GuiDialogs7Hooks& h = *g_hooks;
    int result;
    do {
        if (h.readMouseRelease())          // dword_672230
            g_forceQuitLatch = 1;          // dword_631614 = 1
        result = h.gameLogicRunFrameLoop(423879, a1, a2);
    } while (result);
    return result;
}

// ===========================================================================
// 0x503f44 — VIBE_Window_ShowProgressForm.
// ===========================================================================
int Window_ShowProgressForm() {
    const GuiDialogs7Hooks& h = *g_hooks;
    // dword_122F634 = cached progress form id (BSS, 0 headless).
    Form_SelectWindow(0, 2);                       // REAL sibling (form 0, slot 2)
    h.textRenderRichString(reinterpret_cast<std::uintptr_t>("$C"), 0);
    h.textRenderRichString(0, 0);
    return Window_RenderUpdates();                 // REAL sibling
}

// ===========================================================================
// 0x527830 — VIBE_Window_EnableIfVisible.
// dword_63CC18 = main app window (BSS, nullptr headless).
// ===========================================================================
int Window_EnableIfVisible(void* hWnd, int bEnable) {
    const GuiDialogs7Hooks& h = *g_hooks;
    void* mainWnd = nullptr; // dword_63CC18
    if (hWnd == mainWnd || !h.isWindowVisible(hWnd))
        return 1;
    h.enableWindow(hWnd, bEnable);
    return 1;
}

// ===========================================================================
// 0x527868 — VIBE_Window_DestroyAndUnregisterClass.
// ===========================================================================
int Window_DestroyAndUnregisterClass() {
    const GuiDialogs7Hooks& h = *g_hooks;
    void* mainWnd = nullptr; // dword_63CC18
    void* hInst = nullptr;   // hInstance
    h.destroyWindow(mainWnd);
    return h.unregisterClassA("Die Gilde", hInst);
}

// ===========================================================================
// 0x4333dc — VIBE_Gui_ShowDeviceSelectDialog.
// Config globals byte_62D598/dword_62D590/dword_62D58C are modeled module-local.
// ===========================================================================
static unsigned char g_devCfgByte = 0; // byte_62D598
static i32 g_devCfg0 = 0;              // dword_62D590
static i32 g_devCfg1 = 0;              // dword_62D58C

int Gui_ShowDeviceSelectDialog(int a1, char a2, void* a3, int a4) {
    const GuiDialogs7Hooks& h = *g_hooks;
    void* lib = h.loadLibraryA("gildedlg.dll"); // LibFileName (resource dll)
    g_devCfgByte = static_cast<unsigned char>(a2);
    int result = a4;
    g_devCfg0 = a1;
    g_devCfg1 = a4;
    if (lib) {
        h.dialogBoxParamA(lib, 0x68, a3, /*DialogFunc*/ nullptr, 0);
        return h.freeLibrary(lib);
    }
    return result;
}

// Accessors for tests to observe the stashed device config.
unsigned char DeviceSelectConfigByte() { return g_devCfgByte; }
i32 DeviceSelectConfig0() { return g_devCfg0; }
i32 DeviceSelectConfig1() { return g_devCfg1; }

// ===========================================================================
// 0x1428c20 — VIBE_Gui_MessageBoxFallback.
// Lazy dynamic-import of user32 MessageBoxA. The original caches the resolved fn
// pointers in dword_145A4A4/A8/AC; we model the resolve-once latch + owner lookup.
// ===========================================================================
namespace {
bool g_mbResolved = false; // dword_145A4A4 != 0 once resolved
}

int Gui_MessageBoxFallback(int text, int caption, int type) {
    const GuiDialogs7Hooks& h = *g_hooks;
    if (!g_mbResolved) {
        // GetModuleHandle("user32.dll") modeled as the loadLibrary hook returning
        // non-null when MessageBoxA is available.
        void* user32 = h.loadLibraryA("user32.dll");
        if (!user32)
            return 0;
        g_mbResolved = true;
    }
    int owner = 0;
    void* active = h.getActiveWindow();
    if (active) {
        void* popup = h.getLastActivePopup(active);
        owner = static_cast<int>(reinterpret_cast<std::intptr_t>(popup));
    }
    return h.messageBoxA(reinterpret_cast<void*>(static_cast<std::intptr_t>(owner)),
                         text, caption, type);
}

void ResetMessageBoxFallbackCache() { g_mbResolved = false; }

// ===========================================================================
// 0x41287c — VIBE_RadioGroup_FreeSurface_Thunk.
// VIBE_Light_SetGrayColorThunk(0, 140, &byte_676580[140*group]).
// ===========================================================================
void RadioGroup_FreeSurface_Thunk(int group) {
    const GuiDialogs7Hooks& h = *g_hooks;
    unsigned char* block = (group >= 0 && group < 8)
                               ? &g_radioSurfaceBlocks[140 * group]
                               : g_radioSurfaceBlocks;
    h.lightSetGrayThunk(0, 140, reinterpret_cast<std::intptr_t>(block));
}

// ===========================================================================
// 0x413214 — VIBE_Widget_CreateObject_Thunk.
// ===========================================================================
int Widget_CreateObject_Thunk(int a1, short a2) {
    return g_hooks->gameLogicObjects(a1, a2);
}

// ===========================================================================
// 0x4199ac / 0x413570 — empty stubs (kept 1:1).
// ===========================================================================
void Gui_Nop2() {}
void Gui_Nop() {}

} // namespace guild::gui
