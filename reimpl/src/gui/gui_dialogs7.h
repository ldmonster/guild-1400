#pragma once
// guild::gui — gui_dialogs7: the remaining untranslated slice of the retained-mode
// GUI's VIBE_Window_* / VIBE_Widget_* / VIBE_Form_* / VIBE_Panel_* / VIBE_Dialog_* /
// VIBE_Gui_* / VIBE_RadioGroup_* builders, translated 1:1 from gilde.exe. Wave 14's
// gui_dialogs3..6 already consumed the large panel/dialog "run" loops; this file
// recovers the leftover system-init / render-surface / Win32-window-class /
// idle-loop / tavern-dispatch helpers.
//
// Functions recovered here (addr — original VIBE_ symbol):
//
//   0x4200f8  VIBE_Widget_InitSystem               allocate the GUI's three scratch
//                                                  surfaces + clear the 3 sub-tables.
//   0x40ec08  VIBE_Widget_InitFromState            re-seed a widget record from its
//                                                  Object3D state (anim ptr, bounds).
//   0x41999c  VIBE_Widget_DrawBackgroundSprite     draw a window's full-rect bg sprite.
//   0x419a3c  VIBE_Window_SetBackgroundTexture     load/release a window's bg texture.
//   0x41523c  VIBE_Window_RenderEntityScene        clear + composite the 32 fade slots.
//   0x567170  VIBE_Panel_ShowUseObject             "use object" notification panel loop.
//   0x551374  VIBE_Form_RunIdleLoop                spin the per-frame loop until exit.
//   0x503f44  VIBE_Window_ShowProgressForm         render the 2-line progress form.
//   0x527830  VIBE_Window_EnableIfVisible          EnableWindow() guarded by visibility.
//   0x527868  VIBE_Window_DestroyAndUnregisterClass DestroyWindow + UnregisterClassA.
//   0x4333dc  VIBE_Gui_ShowDeviceSelectDialog      DialogBoxParamA device picker.
//  0x1428c20  VIBE_Gui_MessageBoxFallback          dynamic-import user32 MessageBoxA.
//   0x41287c  VIBE_RadioGroup_FreeSurface_Thunk    release a radio-group surface block.
//   0x413214  VIBE_Widget_CreateObject_Thunk       forward to the object factory.
//   0x4199ac  VIBE_Gui_Nop2 / 0x413570 VIBE_Gui_Nop  empty stubs (kept 1:1).
//
// Cross-module / sibling leaves (renderer surfaces, the per-frame game loop, the
// 3D scene / object query helpers, Win32 user32 imports, the engine input-state
// globals, etc.) are routed through an installable GuiDialogs7Hooks struct with
// inert defaults defined in gui_dialogs7.cpp — the house pattern (GuiDialogs6Hooks
// / CutsceneMiscHooks). Tests install their own hooks. The already-reconstructed
// siblings Form_SelectWindow / Form_CenterChildWindows / Form_Destroy /
// Window_RenderUpdates / Window_RemoveChildren are REUSED directly via their real
// headers, NOT hooked. The widget/window pools (g_widgets / g_windows) and the
// force-quit latch (g_forceQuitLatch, owned by gui_dialogs5.cpp) are reused via
// extern.

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Module-owned engine tables this slice walks/writes (BSS, zero at load).
// ===========================================================================
// VIBE_Widget_InitSystem scratch-surface handles + the three id sub-tables.
//   dword_75BEE0                main scratch surface (1x1)
//   dword_75BF10/14/18 (12*i)   per-slot surface / two id columns (3 slots)
//   dword_75BEF4/F8/FC          three extra surface/flag handles
//   dword_75B9E4/E8    (12*i)   a second 6-row (id,flag) table
struct WidgetSystemTables {
    i32 mainSurface;          // dword_75BEE0
    i32 surf3[3];             // dword_75BF10 (12-byte stride: surface column)
    i32 idA3[3];              // dword_75BF14 (id column, -1 on init)
    i32 idB3[3];              // dword_75BF18 (id column, -1 on init)
    i32 surfE;                // dword_75BEFC
    i32 flagF4;               // dword_75BEF4 (-1 on init)
    i32 flagF8;               // dword_75BEF8 (-1 on init)
    i32 rowSurf[7];           // dword_75B9E4 (12-byte stride, -1 on init), to +72/12=6 -> 7 slots
    i32 rowFlag[7];           // dword_75B9E8 (0 on init)
};
extern WidgetSystemTables g_widgetSys;
extern i32  g_widgetSysCfg0;   // dword_62D308 (cleared by InitSystem)
extern i16  g_widgetSysCfg1;   // word_62D310  (cleared by InitSystem)

// VIBE_Panel_ShowUseObject / VIBE_Form_RunIdleLoop frame-loop control latches.
//   dword_672230  right-click / cancel edge  (read)
//   dword_631614  force-quit latch           (== g_forceQuitLatch in gui_dialogs5)
//   dword_75BF38  last-clicked object id      (set to -1 by ShowUseObject)
//   dword_631614, dword_672230 are reused below.
extern i32 g_useObjLastClicked;   // dword_75BF38 (model: ShowUseObject sets -1)

// VIBE_Window_RenderEntityScene fade-slot table.
//   dword_672280[32]  composite/fade object ids (0 = empty)
//   dword_62D210      back surface ; dword_62D218 second surface
extern i32 g_fadeSlots[32];       // dword_672280

// Engine control-state globals owned elsewhere — REUSED here via extern.
extern i32 g_forceQuitLatch;      // dword_631614 (owned by gui_dialogs5.cpp)

// ===========================================================================
// Hooks: cross-module / sibling leaves with inert defaults.
// Signatures mirror the raw decompiled ABI so each call translates 1:1.
// ===========================================================================
struct GuiDialogs7Hooks {
    // --- widget state / object factory (not reconstructed) -----------------
    int  (*stateUpdate)(int id);                 // VIBE_State_Update @0x40e9e8
    int  (*guiMarkObjectUsed)(int objId);        // VIBE_Gui_MarkObjectUsed @0x412ea4
    void (*gameLogicMovement)(int objId);        // VIBE_GameLogic_Movement @0x412f18
    int  (*gameLogicObjects)(int a, short b);    // VIBE_GameLogic_Objects @0x412fa0

    // --- renderer surfaces / compositing (not reconstructed) ---------------
    int  (*surfaceCreate)(void* desc, int kind, int extra);     // VIBE_Surface_Create @0x42311c
    void (*lightSetGrayThunk)(int a, int b, int descPtr);       // VIBE_Light_SetGrayColorThunk @0x5c6af0
    void (*surfaceReleaseTexture)(int surf);                    // VIBE_Surface_ReleaseTexture @0x5b5404
    int  (*renderFillBackBuffer)(int win, int kind);            // VIBE_Render_FillBackBuffer @0x5b5410
    int  (*surfaceColorFill)(int surf, int color);             // VIBE_Surface_ColorFill @0x423b6c
    int  (*decompressStateBlob)(int surf, int arg);             // VIBE_DecompressState_Blob @0x423500
    void (*decompressionFinalize)(int surf);                    // VIBE_Decompression_Finalize @0x4235dc
    void (*fadeUpdate)(int obj, int surf);                      // VIBE_Fade_Update @0x41f1cc
    void (*renderPresentFrame)(void* flags);                    // VIBE_Render_PresentFrame @0x4349e4
    void (*coordPush)(int x0, int y0, int x1, int y1);          // VIBE_Coord_Push @0x5d8ae8
    void (*animationBasic)(int x, int y, int state, int surf, int pal); // VIBE_Animation_Basic @0x5d85b8
    void (*errorLogReportMessage)(const char* msg);             // VIBE_ErrorLog_ReportMessage @0x438da8

    // --- text rendering (not reconstructed) --------------------------------
    int  (*textRenderRichString)(unsigned a, unsigned b);       // VIBE_Text_RenderRichString @0x59d6e8

    // --- panel chrome / form-build sibling leaves --------------------------
    int  (*gameTickFinalize)(i16 a, i16 b, const char* formName); // VIBE_GameTick_Finalize @0x41beb8
    void (*dragCursorSetSprite)(int a, int b);                  // VIBE_DragCursor_SetSprite @0x41fcbc
    int  (*formCenterChildWindows)(int formId);                 // VIBE_Form_CenterChildWindows (real sibling, hookable)
    int  (*gameLogicRunFrameLoop)(int a, int b, const void* self); // VIBE_GameLogic_RunFrameLoop @0x4c09a0

    // --- Win32 imports (user32) --------------------------------------------
    int  (*isWindowVisible)(void* hWnd);                        // IsWindowVisible
    int  (*enableWindow)(void* hWnd, int bEnable);              // EnableWindow
    int  (*destroyWindow)(void* hWnd);                          // DestroyWindow
    int  (*unregisterClassA)(const char* cls, void* hInst);     // UnregisterClassA
    void*(*loadLibraryA)(const char* name);                     // LoadLibraryA
    int  (*dialogBoxParamA)(void* hMod, int tmpl, void* parent, void* proc, int param); // DialogBoxParamA
    int  (*freeLibrary)(void* hMod);                            // FreeLibrary
    int  (*messageBoxA)(void* hWnd, int text, int caption, int type); // MessageBoxA (dynamic import)
    void*(*getActiveWindow)();                                  // GetActiveWindow
    void*(*getLastActivePopup)(void* hWnd);                     // GetLastActivePopup

    // --- engine control-state reads ----------------------------------------
    int  (*readMouseRelease)();   // dword_672230
};

// Install a hooks struct (nullptr restores the inert defaults). Returns previous.
const GuiDialogs7Hooks* SetGuiDialogs7Hooks(const GuiDialogs7Hooks* hooks);
const GuiDialogs7Hooks* GuiDialogs7Hooks_Default();
const GuiDialogs7Hooks& GuiDialogs7HooksActive();

// Reset module-owned tables + restore default hooks (deterministic test start).
void ResetGuiDialogs7();

// ===========================================================================
// Recovered functions (1:1 with the originals).
// ===========================================================================

// gilde.exe 0x4200f8 — VIBE_Widget_InitSystem ().
// Clears g_widgetSysCfg0/1, then creates the 1x1 main scratch surface, three 128x128
// slot surfaces (with their two id columns set to -1), one 256x256 surface, and
// clears the second 6-row (id=-1,flag=0) table. Returns the last surface handle.
int Widget_InitSystem();

// gilde.exe 0x40ec08 — VIBE_Widget_InitFromState (widgetIdx@eax, objId@edx).
// Re-seeds widget[widgetIdx] from its Object3D state record: if the type byte (+24)
// is < 64 it nudges the object via GameLogic_Movement, refreshes the anim/state ptr
// (+12) via State_Update, recomputes the world+screen bounds from the data record,
// and (for type<64) marks the object used. Returns the high word of dword_69FFBC.
short Widget_InitFromState(int widgetIdx, int objId);

// gilde.exe 0x41999c — VIBE_Widget_DrawBackgroundSprite (objId@eax).
// If objId == -1 or its state resolves to 0, NOP2. Otherwise pushes the current
// window's full-rect clip, composites the resolved state sprite, and restores clip.
void Widget_DrawBackgroundSprite(int objId);

// gilde.exe 0x419a3c — VIBE_Window_SetBackgroundTexture (name@eax, winSlot@edx, a3@ecx).
// Releases any existing bg texture for window[winSlot]; if `name` is non-empty,
// loads it via Render_FillBackBuffer into +912 (logging on failure) and sets the
// +916 scale dword to 1.0f (0x3F000000).
void Window_SetBackgroundTexture(const char* name, int winSlot, int a3);

// gilde.exe 0x41523c — VIBE_Window_RenderEntityScene (a1@ecx, a2).
// Clears the back surface, composites the 32 fade slots, presents the frame, then
// re-clears the back + second surface. Returns the last ColorFill result.
int Window_RenderEntityScene(int a1, int a2);

// gilde.exe 0x567170 — VIBE_Panel_ShowUseObject (text@eax, a2@edi).
// Builds the panel\useobj form, selects/centers it, renders `text`, clears the drag
// cursor, then spins the per-frame loop (forcing the force-quit latch each frame
// once the cancel edge fires) until it exits; destroys the form. Returns Form_Destroy.
int Panel_ShowUseObject(const char* text, char* a2);

// gilde.exe 0x551374 — VIBE_Form_RunIdleLoop (a1@ebx, a2@edi).
// Spins GameLogic_RunFrameLoop, setting the force-quit latch whenever the cancel
// edge (dword_672230) is set, until the loop returns 0. Returns 0.
int Form_RunIdleLoop(int a1, char* a2);

// gilde.exe 0x503f44 — VIBE_Window_ShowProgressForm ().
// Selects window slot 2 of the cached progress form, renders the "$C" rich string
// twice, and flushes via Window_RenderUpdates. Returns Window_RenderUpdates.
int Window_ShowProgressForm();

// gilde.exe 0x527830 — VIBE_Window_EnableIfVisible (hWnd@stdcall, bEnable).
// Returns 1 immediately for the main window or an invisible window; otherwise calls
// EnableWindow(hWnd, bEnable). Always returns 1.
int Window_EnableIfVisible(void* hWnd, int bEnable);

// gilde.exe 0x527868 — VIBE_Window_DestroyAndUnregisterClass ().
// DestroyWindow(mainHWnd) then UnregisterClassA("Die Gilde", hInstance).
int Window_DestroyAndUnregisterClass();

// gilde.exe 0x4333dc — VIBE_Gui_ShowDeviceSelectDialog (a1@eax, a2@cl, a3@ebx, a4).
// Loads the resource DLL, stashes the (a1,a2,a4) config globals, shows the device
// picker dialog template 0x68 (if the DLL loaded) and frees the DLL.
int Gui_ShowDeviceSelectDialog(int a1, char a2, void* a3, int a4);

// Observers for the stashed device-select config (not in the original; test aid).
unsigned char DeviceSelectConfigByte();
i32 DeviceSelectConfig0();
i32 DeviceSelectConfig1();
// Reset the MessageBoxFallback resolve-once latch (test aid).
void ResetMessageBoxFallbackCache();

// gilde.exe 0x1428c20 — VIBE_Gui_MessageBoxFallback (text, caption, type).
// Lazily dynamic-imports user32 MessageBoxA/GetActiveWindow/GetLastActivePopup,
// resolves the owner window, and shows the message box. Returns the dialog result.
int Gui_MessageBoxFallback(int text, int caption, int type);

// gilde.exe 0x41287c — VIBE_RadioGroup_FreeSurface_Thunk (group@eax).
// Releases the 140-byte surface descriptor block at byte_676580[140*group].
void RadioGroup_FreeSurface_Thunk(int group);

// gilde.exe 0x413214 — VIBE_Widget_CreateObject_Thunk (a1@ecx, a2@dx).
// Tail-call into the object factory (GameLogic_Objects).
int Widget_CreateObject_Thunk(int a1, short a2);

// gilde.exe 0x4199ac — VIBE_Gui_Nop2 () / 0x413570 — VIBE_Gui_Nop ().
void Gui_Nop2();
void Gui_Nop();

} // namespace guild::gui
