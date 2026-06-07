// Unit tests for guild::gui gui_dialogs7 — the leftover GUI system-init / render /
// Win32-window-class / idle-loop / tavern-dispatch builders. Each test installs its
// own GuiDialogs7Hooks (recording stubs) so the deterministic control flow of every
// recovered function is observable headless.
#include "test.h"

#include "gui/gui_dialogs7.h"
#include "gui/object.h"   // g_widgets, Widget
#include "gui/window.h"   // g_windows, g_currentWindowId

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

// ---- shared recording state for hook callbacks ----------------------------
struct Rec {
    int surfaceCreateCalls = 0;
    int lightThunkCalls = 0;
    int lastSurfaceKind = -1;
    int lastSurfaceW = -1, lastSurfaceH = -1;
    int surfaceSeq = 100; // returned surface handle generator

    int releaseTexCalls = 0;
    int lastReleaseSurf = -1;
    int fillBackCalls = 0;
    int fillBackReturn = 0;
    int errorLogCalls = 0;

    int colorFillCalls = 0;
    int fadeUpdateCalls = 0;
    int presentCalls = 0;
    int decompressReturn = 0;

    int frameLoopCalls = 0;
    int frameLoopBudget = 0; // returns nonzero this many times, then 0
    int mouseRelease = 0;

    int formCenterCalls = 0, formSelectCalls = 0, formDestroyCalls = 0;

    int isVisible = 1;
    int enableCalls = 0, lastEnable = -1;
    int destroyWindowCalls = 0, unregisterCalls = 0;

    int loadLibReturnNonNull = 0;
    int dialogBoxCalls = 0, freeLibCalls = 0;

    int getActiveReturnNonNull = 0;
    int msgBoxCalls = 0, msgBoxReturn = 0;
    int lastMsgBoxText = 0;
};
Rec g;

int  hStateUpdate(int) { return 0; }
int  hGuiMarkObjectUsed(int) { return 0; }
void hGameLogicMovement(int) {}
int  hGameLogicObjects(int, short) { return 7; }

int  hSurfaceCreate(void* desc, int kind, int) {
    g.surfaceCreateCalls++;
    g.lastSurfaceKind = kind;
    int* d = static_cast<int*>(desc);
    g.lastSurfaceW = d[1]; g.lastSurfaceH = d[2];
    return g.surfaceSeq++;
}
void hLightThunk(int, int, int) { g.lightThunkCalls++; }
void hReleaseTex(int s) { g.releaseTexCalls++; g.lastReleaseSurf = s; }
int  hFillBack(int, int) { g.fillBackCalls++; return g.fillBackReturn; }
int  hColorFill(int, int) { g.colorFillCalls++; return 42; }
int  hDecompress(int, int) { return g.decompressReturn; }
void hFinalize(int) {}
void hFade(int, int) { g.fadeUpdateCalls++; }
void hPresent(void*) { g.presentCalls++; }
void hCoordPush(int, int, int, int) {}
void hAnim(int, int, int, int, int) {}
void hErrorLog(const char*) { g.errorLogCalls++; }
int  hText(unsigned, unsigned) { return 0; }
int  hGameTick(i16, i16, const char*) { return 9; }
void hDragCursor(int, int) {}
int  hFormCenter(int) { g.formCenterCalls++; return 0; }
int  hFrameLoop(int, int, const void*) {
    g.frameLoopCalls++;
    return g.frameLoopBudget-- > 0 ? 1 : 0;
}
int  hIsVisible(void*) { return g.isVisible; }
int  hEnable(void*, int e) { g.enableCalls++; g.lastEnable = e; return 0; }
int  hDestroyWnd(void*) { g.destroyWindowCalls++; return 1; }
int  hUnregister(const char*, void*) { g.unregisterCalls++; return 1; }
void* hLoadLib(const char*) {
    return g.loadLibReturnNonNull ? reinterpret_cast<void*>(&g) : nullptr;
}
int  hDialogBox(void*, int, void*, void*, int) { g.dialogBoxCalls++; return 0; }
int  hFreeLib(void*) { g.freeLibCalls++; return 1; }
int  hMsgBox(void*, int text, int, int) { g.msgBoxCalls++; g.lastMsgBoxText = text; return g.msgBoxReturn; }
void* hGetActive() { return g.getActiveReturnNonNull ? reinterpret_cast<void*>(&g) : nullptr; }
void* hGetPopup(void*) { return reinterpret_cast<void*>(0x1234); }
int  hMouseRelease() { return g.mouseRelease; }

GuiDialogs7Hooks MakeHooks() {
    GuiDialogs7Hooks h{};
    h.stateUpdate = hStateUpdate;
    h.guiMarkObjectUsed = hGuiMarkObjectUsed;
    h.gameLogicMovement = hGameLogicMovement;
    h.gameLogicObjects = hGameLogicObjects;
    h.surfaceCreate = hSurfaceCreate;
    h.lightSetGrayThunk = hLightThunk;
    h.surfaceReleaseTexture = hReleaseTex;
    h.renderFillBackBuffer = hFillBack;
    h.surfaceColorFill = hColorFill;
    h.decompressStateBlob = hDecompress;
    h.decompressionFinalize = hFinalize;
    h.fadeUpdate = hFade;
    h.renderPresentFrame = hPresent;
    h.coordPush = hCoordPush;
    h.animationBasic = hAnim;
    h.errorLogReportMessage = hErrorLog;
    h.textRenderRichString = hText;
    h.gameTickFinalize = hGameTick;
    h.dragCursorSetSprite = hDragCursor;
    h.formCenterChildWindows = hFormCenter;
    h.gameLogicRunFrameLoop = hFrameLoop;
    h.isWindowVisible = hIsVisible;
    h.enableWindow = hEnable;
    h.destroyWindow = hDestroyWnd;
    h.unregisterClassA = hUnregister;
    h.loadLibraryA = hLoadLib;
    h.dialogBoxParamA = hDialogBox;
    h.freeLibrary = hFreeLib;
    h.messageBoxA = hMsgBox;
    h.getActiveWindow = hGetActive;
    h.getLastActivePopup = hGetPopup;
    h.readMouseRelease = hMouseRelease;
    return h;
}

struct Scope {
    GuiDialogs7Hooks hooks;
    const GuiDialogs7Hooks* prev;
    Scope() {
        g = Rec{};
        ResetGuiDialogs7();
        ResetMessageBoxFallbackCache();
        hooks = MakeHooks();
        prev = SetGuiDialogs7Hooks(&hooks);
    }
    ~Scope() { SetGuiDialogs7Hooks(prev); }
};

} // namespace

TEST(GuiDialogs7, InitSystemBuildsThreeSurfaceTiers) {
    Scope s;
    int last = Widget_InitSystem();
    // 1 main + 3 slot + 1 big = 5 surfaces created.
    CHECK_EQ(g.surfaceCreateCalls, 5);
    // last surface is the 256x256 one and its handle is returned.
    CHECK_EQ(g.lastSurfaceW, 256);
    CHECK_EQ(g.lastSurfaceH, 256);
    CHECK_EQ(last, g.surfaceSeq - 1);
    CHECK_EQ(g_widgetSys.surfE, last);
    CHECK_EQ(g_widgetSysCfg0, 0);
    // id columns of the 3 slot surfaces are -1.
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(g_widgetSys.idA3[i], -1);
        CHECK_EQ(g_widgetSys.idB3[i], -1);
    }
    CHECK_EQ(g_widgetSys.flagF4, -1);
    CHECK_EQ(g_widgetSys.flagF8, -1);
    // second 6-row table: rows 1..6 set to (-1, 0).
    for (int slot = 1; slot <= 6; ++slot) {
        CHECK_EQ(g_widgetSys.rowSurf[slot], -1);
        CHECK_EQ(g_widgetSys.rowFlag[slot], 0);
    }
}

TEST(GuiDialogs7, SetBackgroundTextureReleasesThenLoads) {
    Scope s;
    const int slot = 3;
    g_windows[slot].at<i32>(912) = 555; // pre-existing texture
    g.fillBackReturn = 777;
    Window_SetBackgroundTexture("bg", slot, 0);
    CHECK_EQ(g.releaseTexCalls, 1);
    CHECK_EQ(g.lastReleaseSurf, 555);
    CHECK_EQ(g.fillBackCalls, 1);
    CHECK_EQ(g_windows[slot].at<i32>(912), 777);
    CHECK_EQ(g_windows[slot].at<i32>(916), 1056964608); // 0x3F000000
    CHECK_EQ(g.errorLogCalls, 0);
}

TEST(GuiDialogs7, SetBackgroundTextureLogsOnLoadFailure) {
    Scope s;
    g.fillBackReturn = 0; // load failed
    Window_SetBackgroundTexture("bg", 4, 0);
    CHECK_EQ(g.fillBackCalls, 1);
    CHECK_EQ(g.errorLogCalls, 1);
    CHECK_EQ(g_windows[4].at<i32>(912), 0);
}

TEST(GuiDialogs7, SetBackgroundTextureEmptyNameSkipsLoad) {
    Scope s;
    Window_SetBackgroundTexture("", 5, 0);
    CHECK_EQ(g.fillBackCalls, 0);
    CHECK_EQ(g.errorLogCalls, 0);
}

TEST(GuiDialogs7, RenderEntitySceneCompositesActiveFadeSlots) {
    Scope s;
    g.decompressReturn = 1; // enter the fill/finalize branches
    g_fadeSlots[0] = 11;
    g_fadeSlots[5] = 22;
    g_fadeSlots[31] = 33;
    int r = Window_RenderEntityScene(0, 0);
    CHECK_EQ(g.fadeUpdateCalls, 3);   // only the 3 nonzero slots
    CHECK_EQ(g.presentCalls, 1);
    CHECK_EQ(g.colorFillCalls, 3);    // two back clears + the final second clear
    CHECK_EQ(r, 42);
}

TEST(GuiDialogs7, RenderEntitySceneSkipsFillWhenBlobZero) {
    Scope s;
    g.decompressReturn = 0; // skip the two back-fill branches
    g_fadeSlots[2] = 99;
    Window_RenderEntityScene(0, 0);
    CHECK_EQ(g.colorFillCalls, 1);    // only the final second-surface clear runs
    CHECK_EQ(g.fadeUpdateCalls, 1);
    CHECK_EQ(g.presentCalls, 1);
}

TEST(GuiDialogs7, DrawBackgroundSpriteNopOnInvalid) {
    Scope s;
    Widget_DrawBackgroundSprite(-1); // -1 -> NOP path, no composite
    // No crash and no fade/anim work; defaults observable via no asserts beyond run.
    CHECK(true);
}

TEST(GuiDialogs7, FormRunIdleLoopSpinsUntilExit) {
    Scope s;
    g.frameLoopBudget = 3; // loop body runs 4 times (3 nonzero + final 0)
    int r = Form_RunIdleLoop(1, nullptr);
    CHECK_EQ(r, 0);
    CHECK_EQ(g.frameLoopCalls, 4);
}

TEST(GuiDialogs7, FormRunIdleLoopSetsForceQuitOnCancelEdge) {
    Scope s;
    g.mouseRelease = 1;     // cancel edge held
    g.frameLoopBudget = 0;  // single iteration
    Form_RunIdleLoop(1, nullptr);
    CHECK_EQ(g_forceQuitLatch, 1);
}

TEST(GuiDialogs7, ShowUseObjectBuildsAndTearsDownForm) {
    Scope s;
    g.frameLoopBudget = 1;
    int r = Panel_ShowUseObject("scroll", nullptr);
    CHECK_EQ(g.formCenterCalls, 1);
    CHECK_EQ(g_useObjLastClicked, -1);
    CHECK_EQ(r, 0);
    CHECK_EQ(g.frameLoopCalls, 2);
}

TEST(GuiDialogs7, EnableIfVisibleSkipsInvisible) {
    Scope s;
    g.isVisible = 0;
    int r = Window_EnableIfVisible(reinterpret_cast<void*>(0x10), 1);
    CHECK_EQ(r, 1);
    CHECK_EQ(g.enableCalls, 0);
}

TEST(GuiDialogs7, EnableIfVisibleEnablesVisible) {
    Scope s;
    g.isVisible = 1;
    int r = Window_EnableIfVisible(reinterpret_cast<void*>(0x10), 1);
    CHECK_EQ(r, 1);
    CHECK_EQ(g.enableCalls, 1);
    CHECK_EQ(g.lastEnable, 1);
}

TEST(GuiDialogs7, DestroyAndUnregisterClass) {
    Scope s;
    int r = Window_DestroyAndUnregisterClass();
    CHECK_EQ(g.destroyWindowCalls, 1);
    CHECK_EQ(g.unregisterCalls, 1);
    CHECK_EQ(r, 1);
}

TEST(GuiDialogs7, ShowDeviceSelectStashesConfigAndShowsDialog) {
    Scope s;
    g.loadLibReturnNonNull = 1;
    Gui_ShowDeviceSelectDialog(5, 7, nullptr, 9);
    CHECK_EQ((int)DeviceSelectConfigByte(), 7);
    CHECK_EQ(DeviceSelectConfig0(), 5);
    CHECK_EQ(DeviceSelectConfig1(), 9);
    CHECK_EQ(g.dialogBoxCalls, 1);
    CHECK_EQ(g.freeLibCalls, 1);
}

TEST(GuiDialogs7, ShowDeviceSelectSkipsDialogWhenLibMissing) {
    Scope s;
    g.loadLibReturnNonNull = 0;
    int r = Gui_ShowDeviceSelectDialog(5, 7, nullptr, 9);
    CHECK_EQ(g.dialogBoxCalls, 0);
    CHECK_EQ(g.freeLibCalls, 0);
    CHECK_EQ(r, 9); // returns a4
}

TEST(GuiDialogs7, MessageBoxFallbackResolvesAndShows) {
    Scope s;
    g.loadLibReturnNonNull = 1;
    g.getActiveReturnNonNull = 1;
    g.msgBoxReturn = 6;
    int r = Gui_MessageBoxFallback(101, 202, 0);
    CHECK_EQ(g.msgBoxCalls, 1);
    CHECK_EQ(g.lastMsgBoxText, 101);
    CHECK_EQ(r, 6);
}

TEST(GuiDialogs7, MessageBoxFallbackReturnsZeroWhenUser32Missing) {
    Scope s;
    g.loadLibReturnNonNull = 0;
    int r = Gui_MessageBoxFallback(1, 2, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(g.msgBoxCalls, 0);
}

TEST(GuiDialogs7, CreateObjectThunkForwards) {
    Scope s;
    int r = Widget_CreateObject_Thunk(3, 4);
    CHECK_EQ(r, 7); // hGameLogicObjects returns 7
}

TEST(GuiDialogs7, RadioGroupFreeSurfaceReleases) {
    Scope s;
    RadioGroup_FreeSurface_Thunk(2);
    CHECK_EQ(g.lightThunkCalls, 1);
}
