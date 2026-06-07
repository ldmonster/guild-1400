// End-to-end flow across gui_dialogs7: a headless GUI subsystem lifecycle.
//   boot:   Widget_InitSystem -> Window_SetBackgroundTexture
//   show:   Gui_ShowDeviceSelectDialog -> Gui_MessageBoxFallback
//   run:    Panel_ShowUseObject (frame loop) -> Window_RenderEntityScene
//   teardown: Window_EnableIfVisible -> Window_DestroyAndUnregisterClass
// All cross-module leaves are driven through one recording hooks struct so the
// whole sequence is deterministic and observable without the engine.
#include "test.h"

#include "gui/gui_dialogs7.h"
#include "gui/window.h"
#include "gui/object.h"

#include <cstring>

using namespace guild;
using namespace guild::gui;

namespace {

struct Log {
    int surfaces = 0;
    int releaseTex = 0;
    int fillBack = 0;
    int present = 0;
    int colorFill = 0;
    int frames = 0;
    int frameBudget = 0;
    int queued = 0;
    int joined = 0;
    int enabled = 0;
    int destroyed = 0;
    int unregistered = 0;
    int msgBoxes = 0;
    int libLoaded = 0;
    bool libAvail = true;
    bool active = true;
};
Log L;

int  eState(int) { return 5; }            // nonzero -> composite path
int  eMark(int) { return 0; }
void eMove(int) {}
int  eObjects(int, short) { return 1; }
int  eSurf(void*, int, int) { L.surfaces++; return 300 + L.surfaces; }
void eLight(int, int, int) {}
void eRelease(int) { L.releaseTex++; }
int  eFill(int, int) { L.fillBack++; return 0x900; }
int  eColor(int, int) { L.colorFill++; return 1; }
int  eDecomp(int, int) { return 1; }
void eFinal(int) {}
void eFade(int, int) {}
void ePresent(void*) { L.present++; }
void ePush(int, int, int, int) {}
void eAnim(int, int, int, int, int) {}
void eErr(const char*) {}
int  eText(unsigned, unsigned) { return 0; }
int  eTick(i16, i16, const char*) { return 1; }
void eDrag(int, int) {}
int  eCenter(int) { return 0; }
int  eFrame(int, int, const void*) { L.frames++; return L.frameBudget-- > 0 ? 1 : 0; }
int  eVisible(void*) { return 1; }
int  eEnable(void*, int) { L.enabled++; return 0; }
int  eDestroy(void*) { L.destroyed++; return 1; }
int  eUnreg(const char*, void*) { L.unregistered++; return 1; }
void* eLoad(const char*) { L.libLoaded++; return L.libAvail ? reinterpret_cast<void*>(&L) : nullptr; }
int  eDlg(void*, int, void*, void*, int) { return 0; }
int  eFreeLib(void*) { return 1; }
int  eMsgBox(void*, int, int, int) { L.msgBoxes++; return 11; }
void* eGetActive() { return L.active ? reinterpret_cast<void*>(&L) : nullptr; }
void* eGetPopup(void*) { return reinterpret_cast<void*>(0x55); }
int  eMouse() { return 0; }

GuiDialogs7Hooks MakeHooks() {
    GuiDialogs7Hooks h{};
    h.stateUpdate = eState; h.guiMarkObjectUsed = eMark; h.gameLogicMovement = eMove;
    h.gameLogicObjects = eObjects;
    h.surfaceCreate = eSurf; h.lightSetGrayThunk = eLight; h.surfaceReleaseTexture = eRelease;
    h.renderFillBackBuffer = eFill; h.surfaceColorFill = eColor; h.decompressStateBlob = eDecomp;
    h.decompressionFinalize = eFinal; h.fadeUpdate = eFade; h.renderPresentFrame = ePresent;
    h.coordPush = ePush; h.animationBasic = eAnim; h.errorLogReportMessage = eErr;
    h.textRenderRichString = eText; h.gameTickFinalize = eTick; h.dragCursorSetSprite = eDrag;
    h.formCenterChildWindows = eCenter; h.gameLogicRunFrameLoop = eFrame;
    h.isWindowVisible = eVisible; h.enableWindow = eEnable; h.destroyWindow = eDestroy;
    h.unregisterClassA = eUnreg; h.loadLibraryA = eLoad; h.dialogBoxParamA = eDlg;
    h.freeLibrary = eFreeLib; h.messageBoxA = eMsgBox; h.getActiveWindow = eGetActive;
    h.getLastActivePopup = eGetPopup; h.readMouseRelease = eMouse;
    return h;
}

} // namespace

TEST(GuiDialogs7E2E, FullSubsystemLifecycle) {
    L = Log{};
    ResetGuiDialogs7();
    ResetMessageBoxFallbackCache();
    GuiDialogs7Hooks hooks = MakeHooks();
    const GuiDialogs7Hooks* prev = SetGuiDialogs7Hooks(&hooks);

    // 1. Boot the widget subsystem: 5 scratch surfaces.
    int big = Widget_InitSystem();
    CHECK_EQ(L.surfaces, 5);
    CHECK_EQ(g_widgetSys.surfE, big);

    // 2. Apply a window background texture.
    g_windows[2] = Window{};
    Window_SetBackgroundTexture("title", 2, 0);
    CHECK_EQ(L.fillBack, 1);
    CHECK_EQ(g_windows[2].at<i32>(912), 0x900);

    // 3. Device-select + message box bring-up.
    Gui_ShowDeviceSelectDialog(1, 2, nullptr, 3);
    CHECK_EQ((int)DeviceSelectConfigByte(), 2);
    int mb = Gui_MessageBoxFallback(77, 88, 0);
    CHECK_EQ(L.msgBoxes, 1);
    CHECK_EQ(mb, 11);

    // 4. Run a "use object" panel for two frames, then render the entity scene.
    L.frameBudget = 2;
    int useObj = Panel_ShowUseObject("a torch", nullptr);
    CHECK_EQ(useObj, 0);
    CHECK_EQ(L.frames, 3); // 2 nonzero + final 0
    g_fadeSlots[0] = 1;
    g_fadeSlots[1] = 2;
    Window_RenderEntityScene(0, 0);
    CHECK_EQ(L.present, 1);

    // 5. Teardown: enable a visible window, then destroy + unregister the class.
    int en = Window_EnableIfVisible(reinterpret_cast<void*>(0x20), 0);
    CHECK_EQ(en, 1);
    CHECK_EQ(L.enabled, 1);
    int dr = Window_DestroyAndUnregisterClass();
    CHECK_EQ(dr, 1);
    CHECK_EQ(L.destroyed, 1);
    CHECK_EQ(L.unregistered, 1);

    SetGuiDialogs7Hooks(prev);
}

TEST(GuiDialogs7E2E, MessageBoxResolvesOnce) {
    L = Log{};
    ResetGuiDialogs7();
    ResetMessageBoxFallbackCache();
    GuiDialogs7Hooks hooks = MakeHooks();
    const GuiDialogs7Hooks* prev = SetGuiDialogs7Hooks(&hooks);

    Gui_MessageBoxFallback(1, 1, 0);
    Gui_MessageBoxFallback(2, 2, 0);
    // user32 resolved exactly once (loadLibrary only on the first call).
    CHECK_EQ(L.libLoaded, 1);
    CHECK_EQ(L.msgBoxes, 2);

    SetGuiDialogs7Hooks(prev);
}
