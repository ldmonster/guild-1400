#pragma once
// guild::gui — gui_dialogs4: a further slice of the retained-mode GUI's
// interaction / layout / widget-tree leaves, translated 1:1 from gilde.exe and
// wired onto the already-reconstructed Form/Window/Object(Widget) data model
// (src/gui/types.h, object.h, window.h, form.h).
//
// Functions recovered here (all VIBE_Widget_* / VIBE_Form_* / VIBE_Window_*):
//
//   VIBE_Widget_SetFocus           @0x421370  move keyboard focus to a widget; walk
//                                              the owning group to find the focus
//                                              index, run the edit-caret bookkeeping.
//   VIBE_Form_SetObjectValueOrText @0x41e1c4  set the value/text/range of the a1-th
//                                              child object of a form-window (branches
//                                              on the widget type tag 'A'/'E'/...).
//   VIBE_Form_MarkDirtyAndRender   @0x41cae0  resolve a form, mark every window+child
//                                              widget dirty (+52=1), allocate two
//                                              render surfaces.
//   VIBE_Widget_HandleKeyInput     @0x420360  apply the pending keystroke (byte_67225C)
//                                              to the focused edit field: insert /
//                                              backspace / tab-navigate / enter.
//   VIBE_Widget_ProcessMouseDrag   @0x420db4  the per-frame drag / focus state machine
//                                              (mouse-down focus grab, slider step,
//                                              icon spin, tooltip copy).
//   VIBE_Widget_HoverUpdate        @0x41fd48  per-frame hover detection + wheel scroll
//                                              forwarding.
//   VIBE_Widget_AddPersonRow       @0x518efc  add a 3-cell market/person row to a window
//                                              (two gfx cells + a money-formatted price
//                                              text label).  INTEGRATION ANCHOR: wires
//                                              the real world::MoneyFormatWithSeparators
//                                              + gui::Object_AddTextLabel siblings.
//   VIBE_Widget_DrawScrollBar      @0x4121c4  render a scrollbar widget's track+thumb.
//   VIBE_Widget_DrawScrollThumb    @0x40ecb0  render the standalone scroll thumb.
//   VIBE_Widget_DrawCheckbox       @0x4137bc  render a checkbox / radio glyph by state.
//   VIBE_Window_RenderContent      @0x4186d8  paint a window's backing widget: fill,
//                                              texture, outline, tiled animation, frame.
//   VIBE_Window_MainWndProc        @0x5279dc  the Win32 top-level window procedure
//                                              (quit / paint / activate dispatch).
//
// Cross-module leaves with no reconstructed target (the renderer / scene / coord /
// audio / input / DirectInput edges, plus the un-reconstructed market-price lookup)
// are routed through an installable hooks struct with inert defaults defined in
// gui_dialogs4.cpp — the house pattern (CutsceneMiscHooks / GuiDialogs3Hooks). Tests
// install their own hooks. The interaction-state globals this module mutates are
// owned HERE (they are not defined by any sibling).

#include "gui/types.h"

#include <cstdint>

namespace guild::gui {

// ===========================================================================
// Win32 message constants the MainWndProc dispatches on (kept verbatim so the
// numeric comparisons in the original translate 1:1).
// ===========================================================================
inline constexpr unsigned kWmDestroy        = 2;      // 0x02
inline constexpr unsigned kWmActivate        = 0x1C;  // 28
inline constexpr unsigned kWmPaint           = 0x0F;  // 15
inline constexpr unsigned kWmEraseBkgnd      = 0x14;  // 20
inline constexpr unsigned kWmQueryNewPalette = 0x10;  // 16
inline constexpr unsigned kWmReacquireAudio  = 2023;  // 0x7E7-driver reacquire message
inline constexpr long     kWndProcMagicAccept = 1112363332; // 0x42490004 (Msg in (0x209,0x218], !wParam)

// Pending-keystroke scancodes the edit handler recognizes (byte_67225C values).
inline constexpr unsigned kKeyBackspace = 0x0E;
inline constexpr unsigned kKeyTab       = 0x0F;
inline constexpr unsigned kKeyEnter     = 0x1C;

// ===========================================================================
// Hooks: cross-module render / scene / input / coord / price leaves with no
// reconstructed target.  All defaults are inert (return 0 / no-op), which makes
// the deterministic widget-tree mutations observable without the engine.
// ===========================================================================
struct GuiDialogs4Hooks {
    // --- coordinate truncation (VIBE_Coord_ConvertX @0x5c6b08) ---
    // Real sibling util::ConvertX exists; the default forwards to it.
    double (*coordConvertX)(double x);

    // --- price lookup (VIBE_Building_LookupCachedMarketPrice @0x58f6b8) ---
    // NOT reconstructed; default returns 0.
    double (*lookupCachedMarketPrice)(int itemType, unsigned char personId);

    // --- render / scene leaves (all return-int or void) ---
    int  (*stateUpdate)(int stateId);            // VIBE_State_Update @0x40e9e8
    int  (*stateFinalize)(int stateId);          // VIBE_State_Finalize @...
    int  (*stateGetCurrent)(int x, int y, int w, int h); // VIBE_State_GetCurrent
    std::uintptr_t (*coordTransform)(int handle, unsigned which); // VIBE_Coord_Transform (returns a record ptr)
    void (*coordPush)(int a, int b, int c, int d);        // VIBE_Coord_Push
    int  (*animBasic)(int x, int y, int handle, int surf, int frame); // VIBE_Animation_Basic
    int  (*animApply)(int x, int y, int handle, int blob, const char* txt, int flags); // VIBE_Animation_Apply
    void (*animStateUpdate)(int handle, void* buf, unsigned n); // VIBE_AnimationState_Update
    void (*surfaceColorFill)(int x, int y, int w, int h, int surf);        // VIBE_Surface_ColorFillRect
    void (*surfaceRectOutline)(int x0, int y0, int x1, int y1, int r, int g, int b, int surf); // VIBE_Surface_DrawRectOutline
    int  (*surfaceCreate)(void* out, int kind, int srcWidget);             // VIBE_Surface_Create
    void (*lightSetGray)(int a, int b);          // VIBE_Light_SetGrayColorThunk
    int  (*resultHandlerInteraction)(int a, int b, int c, int d, int e, int f, int g, int h); // VIBE_Result_Handler_Interaction
    int  (*decompressStateBlob)(int blob, int scratch); // VIBE_DecompressState_Blob
    void (*decompressFinalize)(int blob);               // VIBE_Decompression_Finalize
    void (*renderDrawTexturedQuad)(int tex, int x, int y, int z, float u, float v, float a); // VIBE_Render_DrawTexturedQuad
    void (*renderWithSurfaceContext)(int surf);  // VIBE_Render_WithSurfaceContext
    int  (*entityAnimationUpdate)(int x, int y, int w, int h, void* surf); // VIBE_Entity_AnimationUpdate
    int  (*resultFinalize)(int a, int b, int c, int d, int e, int f, int g, int h, int i); // VIBE_Result_Finalize

    // --- widget helpers owned elsewhere but un-reconstructed as called here ---
    i16  (*propertyGet)(const char* text, int font);    // VIBE_Property_Get (sibling exists; default forwards)
    int  (*widgetCreateObjectThunk)(int x, int y);      // VIBE_Widget_CreateObject_Thunk
    int  (*sliderComputeStep)(int widgetPtr);           // VIBE_Slider_ComputeStep
    void (*scrollbarDragThumb)();                       // VIBE_Scrollbar_DragThumb
    void (*sliderUpdateFromMouse)(int a, int b);        // VIBE_Slider_UpdateFromMouse
    int  (*inputCharToScancode)(int shift, int a, int scancode, int b); // VIBE_Input_CharToScancode
    int  (*utilParseInt)(const char* s);                // VIBE_Util_ParseInt
    void (*inputClearMouseButtons)(int mask);           // VIBE_Input_ClearMouseButtonsByMask
    void (*inputSetIconTextById)(int id, int frame);    // VIBE_Input_SetIconTextById

    // --- Win32 / audio / DirectInput leaves used by MainWndProc ---
    void (*audioReacquireDigital)(unsigned long wParam, void* hWnd, unsigned tag);
    void (*audioReacquireAll)(int hWnd, unsigned tag);
    void (*audioReleaseAll)();
    void (*inputAcquireMouse)(int acquire, int arg);
    int  (*renderIsSurfaceLost)();
    int  (*renderRetTrue)();
    void (*gameObjectDispatchInteractions)();
    void (*gameLogicInteractions)(int* frame);
    void (*renderPresentFrame)(void* ctx);
    void (*validateRect)(void* hWnd);
    void (*postQuitMessage)(int code);
    long (*defWindowProc)(void* hWnd, unsigned msg, unsigned long wParam, long lParam);
    void (*setWindowPos)(void* hWnd, void* insertAfter, int x, int y, int cx, int cy, unsigned flags);

    // --- form loader / widget teardown leaves (un-reconstructed) ---
    int  (*gameTickFinalize)(i16 a, i16 b, const char* name); // VIBE_GameTick_Finalize @0x41beb8 (form load)
    int  (*widgetDestroyByType)(int widget, int a, int b);    // VIBE_Widget_DestroyByType @0x414f98
};

// Install a hooks struct (nullptr restores the inert defaults). Returns previous.
const GuiDialogs4Hooks* SetGuiDialogs4Hooks(const GuiDialogs4Hooks* hooks);
const GuiDialogs4Hooks* GuiDialogs4Hooks_Default();

// ===========================================================================
// Module-owned interaction-state globals (BSS, zero at load). These are the
// original engine globals no reconstructed sibling defines.
// ===========================================================================
// Focused widget data-record pointer (dword_62D328; a 32-bit pointer in the original,
// stored host-pointer-width here so the record byte arithmetic is exercisable). 0=none.
extern std::uintptr_t g_focusWidget;
extern i32 g_focusFlag;       // dword_62D348  drag-engaged flag mirror
extern i32 g_dragArmed;       // dword_62D34C  a press is armed this frame
extern i32 g_caretWidget;     // dword_62D350  caret widget slot (-1 = none)
extern i32 g_caretBaseW;      // dword_62D33C  caret base width (-1 = none)
extern i32 g_focusIndex;      // dword_75BEBC  index of focused child within its group (-1)
extern i32 g_caretPenX;       // dword_75BEC8  caret pen x
extern i32 g_caretPenY;       // dword_75BEC4  caret pen y
extern i32 g_focusValue;      // dword_75BECC  cached focus value (+296)
extern i32 g_focusValuePrev;  // dword_75BED0
extern i32 g_focusValueAcc;   // dword_75BED4
extern i32 g_groupSlots[18];  // dword_75B9F0  active group-slot table (-1 == empty)

// Drag-origin bookkeeping the per-frame FSM parks/restores (engine globals; this
// module owns its copies, matching the rest of the interaction-state cluster).
// NOTE: in the original these are the SAME engine dwords that widget_interact.cpp
// models under its own C++ names (a pre-existing cross-module modeling fork — see
// gui_dialogs4.cpp). Owned here so this module is internally 1:1 and testable.
extern i32 g_d4_dragOriginX;     // dword_62D0C8  cursor-clamp/drag origin x mirror
extern i32 g_d4_dragOriginY;     // dword_62D0D0  cursor-clamp/drag origin y mirror
extern i32 g_d4_savedClampY0;    // dword_75BEB8  parked cursor clamp y0
extern i32 g_d4_savedClampY1;    // dword_75BEC0  parked cursor clamp y1
extern i32 g_caretCreateBase; // dword_62D2C8  caret-create extra base (ebx arg, +8)

extern i32 g_hoverPrev;       // dword_75BF40
extern i32 g_hoverLast;       // dword_75BEE4
extern i32 g_hoverSlot;       // dword_75BF3C  current hover slot (-1 = none)
extern i32 g_hoverX;          // dword_62D330
extern i32 g_hoverY;          // dword_62D334
extern i32 g_hoverEngaged;    // dword_62D338
extern i32 g_hoverTick;       // dword_75BED8
extern i32 g_thumbStateId;    // dword_62D2AC  scroll-thumb state id
extern i32 g_scrollThumbX;    // dword_69FF98
extern i32 g_scrollThumbY;    // dword_69FF9C

extern i32 g_hitTestSlot4;    // dword_62D22C  the slot under the cursor

// Hover / wheel-scroll forwarding state read by Widget_HoverUpdate (engine globals;
// module-owned copies). g_hoverScrollWin/g_scrollWheelWin index the window table
// (g_windows). g_wheelUp/g_wheelDn are the mouse-wheel up/down latches.
extern i32 g_hoverScrollWin;  // dword_75BF08  window under the hover scroll (-1 = none)
extern i32 g_scrollWheelWin;  // dword_62D294  window under the wheel cursor (-1 = none)
extern i32 g_stateFinalizeReq;// dword_62D248  deferred State_Finalize id (-1 = none)
extern i32 g_wheelUp;         // dword_672254  wheel-up latch
extern i32 g_wheelDn;         // dword_672250  wheel-down latch

// Per-frame mouse / key input state (engine globals; un-reconstructed).
extern i32 g_pendingMouseX;   // dword_672210 (16.16 fixed-point cursor x)
// Live cursor "rolling point" buffer (word_75BF44 | dword_75BF46 | word_75BF4A).
// Widget_HoverUpdate reads the X coord as `*(int*)(buf+4) >> 16` (the misaligned
// dword at 75BF46+2) and the Y coord as `*(int*)(buf+2) >> 16` (dword_75BF46>>16),
// then shifts the buffer: LOWORD(75BF46)=word_75BF4A; word_75BF44=HIWORD(75BF46).
// Modelled byte-exact so the misaligned reads/writes translate 1:1.
//   buf[0..1] = word_75BF44 ; buf[2..5] = dword_75BF46 ; buf[6..7] = word_75BF4A
extern u8  g_cursorPtBuf[8];  // 0x75BF44 .. 0x75BF4B
extern u8  g_pendingKey;      // byte_67225C  pending scancode (0 = none)
extern u8  g_shiftHeldL;      // byte_671D96  left-shift held (group-nav direction)
extern u8  g_shiftHeldR;      // byte_671D8A  right-shift held (group-nav direction)
extern i32 g_pendingMouseUp;  // dword_67222C
extern i32 g_mouseDown;       // dword_672220
extern i32 g_mouseWheelUp;    // dword_672228
extern i32 g_mouseRelease;    // dword_672230
extern i32 g_mouseDownHover;  // dword_672238
extern i32 g_frameTick;       // dword_62EB3C

// MainWndProc activation latches + frame blob (engine globals; un-reconstructed).
extern unsigned char g_wndDeactivated; // byte_63CC14 (1 = currently deactivated)
extern i32 g_wndAudioActive;  // byte_63CC1C
extern i32 g_renderRetryFlag; // byte_649D70
extern std::uintptr_t g_frameBlob;  // dword_62D210 (frame surface pointer)
extern i32 g_frameBlob2;            // dword_62D218
extern i32 g_audioPausedFlag; // dword_62EB4C
// Active render-context record pointer (dword_62D268), read by the textured-quad
// blit branches of DrawCheckbox / Window_RenderContent.  +116 is the context width.
extern std::uintptr_t g_renderContext; // dword_62D268
// Window_RenderContent state edges (engine globals; un-reconstructed siblings).
extern u8  g_renderPhaseFlag;   // byte_62D25C  (==1 => second-pass blit ordering)
extern std::uintptr_t g_tileAnimRec; // dword_62D21C (entity tile-animation record)
extern std::uintptr_t g_animTableBase; // dword_62D204 (84-byte-stride anim table base)
extern i32 g_drawClipLeft;      // dword_64A1B4
extern i32 g_drawClipRight;     // dword_64A1BC

// ===========================================================================
// Reset all module-owned globals + restore the default hooks. Tests call this
// in setup to get a deterministic starting point.
// ===========================================================================
void ResetGuiDialogs4();

// ===========================================================================
// Recovered functions.
// ===========================================================================

// Bridges to cross-module leaves (route through the installed hooks; the
// LayoutBounds bridge forwards to the REAL gui::Widget_LayoutBounds sibling).
int  Widget_LayoutBoundsBridge(int x, int y, int widgetIdx);
int  Widget_DestroyByTypeBridge(int widget, int a, int b);
int  GameTickFinalizeBridge(i16 a, i16 b, const char* name);

// gilde.exe 0x421370 — VIBE_Widget_SetFocus (widgetSlot@eax). Returns the focus byte.
int Widget_SetFocus(int widgetSlot);

// gilde.exe 0x41e1c4 — VIBE_Form_SetObjectValueOrText
//   (childIdx, windowSlotInForm, text, value24, value296/30, value35).
int Form_SetObjectValueOrText(int childIdx, int winSlotInForm, const char* text,
                              int valA, int valB, int valC);

// gilde.exe 0x41cae0 — VIBE_Form_MarkDirtyAndRender (formArgA@ax, formArgB@dx, name@ebx).
// Returns the resolved form id (0 if none).
int Form_MarkDirtyAndRender(i16 a, i16 b, const char* name);

// gilde.exe 0x420360 — VIBE_Widget_HandleKeyInput (no args; reads global key state).
int Widget_HandleKeyInput();

// gilde.exe 0x420db4 — VIBE_Widget_ProcessMouseDrag (no args; per-frame FSM).
int Widget_ProcessMouseDrag();

// gilde.exe 0x41fd48 — VIBE_Widget_HoverUpdate (no args; per-frame hover/scroll).
int Widget_HoverUpdate();

// gilde.exe 0x518efc — VIBE_Widget_AddPersonRow (x@ax, packed@edx, y2@cx, win@ebx, rate).
// `packed` is a packed dword: LOWORD = base row y, HIWORD (>>16) = gfx base & market
// item id.  y2 (@cx) is unused by the body.  INTEGRATION ANCHOR.  Returns cell-2 slot.
int Widget_AddPersonRow(i16 x, int packed, i16 y2, int winSlot, unsigned char rate);

// gilde.exe 0x4121c4 — VIBE_Widget_DrawScrollBar (widgetSlot@eax, surf@edx).
int Widget_DrawScrollBar(int widgetSlot, int surf);

// gilde.exe 0x40ecb0 — VIBE_Widget_DrawScrollThumb (widgetPtr@eax, surf@edx).
// widgetPtr is a record pointer (32-bit in the original); passed host-width here.
int Widget_DrawScrollThumb(std::uintptr_t widgetPtr, int surf);

// gilde.exe 0x4137bc — VIBE_Widget_DrawCheckbox (widgetPtr@eax).
int Widget_DrawCheckbox(std::uintptr_t widgetPtr);

// gilde.exe 0x4186d8 — VIBE_Window_RenderContent (backingWidgetPtr@eax, surf@edx).
int Window_RenderContent(std::uintptr_t widgetPtr, void* surf);

// gilde.exe 0x5279dc — VIBE_Window_MainWndProc.
long Window_MainWndProc(int a1, int a2, void* hWnd, unsigned msg,
                        unsigned long wParam, long lParam);

} // namespace guild::gui
