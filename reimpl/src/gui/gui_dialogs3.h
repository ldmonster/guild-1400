#pragma once
// guild::gui — gui_dialogs3: a further slice of the retained-mode GUI leaves the
// dialog / window / widget builders forward to.  This module recovers a set of
// previously-untranslated geometry / hit-test / clip-stack / widget-construction
// leaves 1:1 from gilde.exe and wires them onto the already-reconstructed
// Form/Window/Object/Widget data model (src/gui/types.h, object.h, window.h).
//
// Functions recovered here (all VIBE_Gui_* / VIBE_Form_* / VIBE_Window_* /
// VIBE_Widget_* builders/layout/clip leaves):
//
//   VIBE_Gui_ClipRectToBuffers       @0x40e94c  clamp a rect to the active draw
//                                               extent and push it onto the two
//                                               clip-rectangle output buffers.
//   VIBE_Gui_HitTestObject           @0x414d98  topmost widget under a point.
//   VIBE_Gui_HitTestWindow           @0x414ec8  topmost window-backing widget under
//                                               a point (the window-frame hit test).
//   VIBE_Window_PositionAtCoord      @0x41d7e0  anchor a window's backing widget to a
//                                               screen corner (the centred-popup math).
//   VIBE_Window_PositionAtCoord_Thunk@0x41d964  form-id wrapper over the above.
//   VIBE_Window_ConsumeClickFlag     @0x41b870  read-and-clear the pending-click flag.
//   VIBE_Widget_BlitClippedRows      @0x412668  copy a widget's bitmap rows into the
//                                               window surface, clipped to its band.
//   VIBE_Widget_CreateRawBitmap      @0x412560  allocate a type-'G' raw-bitmap widget
//                                               and copy the caller's pixel rows in.
//   VIBE_Widget_AddRawBitmapToWindow @0x412618  window-relative wrapper of the above.
//   VIBE_Form_RefreshIfVisible       @0x41cc5c  re-apply a form's button animations if
//                                               the form is loaded and shown.
//   VIBE_Gui_FreeString_Thunk        @0x411eac  copy a <=63-char string into widget+152.
//   VIBE_Widget_SetTooltipText       @0x421a24  copy a string into the global tooltip
//                                               buffer (byte_75BE38).
//
// Cross-module leaves with no reconstructed target (renderer / scene / form-anim
// edges) are routed through an installable hooks struct with inert defaults defined
// in gui_dialogs3.cpp (the house pattern — tests install their own hooks).

#include "gui/types.h"

namespace guild::gui {

// ===========================================================================
// Recovered globals owned by this module (BSS, zero at load).
// ===========================================================================

// Active draw-surface extent for the clip-rect clamp (VIBE_Gui_ClipRectToBuffers):
//   dword_64A1B8 = clip top    (minimum y)         -> g_drawClipTop
//   dword_64A1C0 = clip bottom (maximum y, exclusive in the +h sense) -> g_drawClipBottom
extern i32 g_drawClipTop;     // dword_64A1B8
extern i32 g_drawClipBottom;  // dword_64A1C0

// The two clip-rectangle output buffers.  Each is a flat array of 16-byte rects
// {x,y,w,h} (the original stores raw int32s); the live process points
// dword_62D2D0[k] at the surface's rect list and dword_62D2E0[k] at its current
// length.  We model the two buffers as fixed arrays here.
inline constexpr int kClipRectBuffers = 2;
inline constexpr int kClipRectCap     = 256; // generous; the original list is unbounded
struct ClipRect {                            // 16 bytes, matches the original layout
    i32 x;  // +0
    i32 y;  // +4
    i32 w;  // +8
    i32 h;  // +12
};
extern ClipRect g_clipRectBuf[kClipRectBuffers][kClipRectCap]; // dword_62D2D0[k] targets
extern i32      g_clipRectLen[kClipRectBuffers];               // dword_62D2E0[k] lengths

// Hit-test scratch / result globals:
//   dword_62D22C = last hit window/widget index (cleared to the slot on a hit)
//   dword_62D290 = last hit object id payload (object-state path; -1 when none)
extern i32 g_hitTestSlot;     // dword_62D22C
extern i32 g_hitTestPayload;  // dword_62D290

// Pending-click flag pair (VIBE_Window_ConsumeClickFlag):
//   dword_62D238 = pending click flag (set by the input dispatch)
//   dword_62D23C = last consumed click flag (snapshot of the above on consume)
extern i32 g_pendingClick;    // dword_62D238
extern i32 g_lastClick;       // dword_62D23C

// Anchor math globals (VIBE_Window_PositionAtCoord):
//   dword_69FFA4 = screen-centre x reference
//   flt_62D224   = horizontal anchor divisor (a float used as a fixed-point scale)
//   dword_69FFBC = screen extent (HIWORD = max y for the vertical clamp)
extern i32   g_centreRefX;    // dword_69FFA4
extern float g_anchorDivisor; // flt_62D224
// g_screenClipExt (dword_69FFBC) is owned by widget_create.h; extern-reused here.

// Global tooltip text buffer (VIBE_Widget_SetTooltipText target).
inline constexpr int kTooltipBufBytes = 256;
extern char g_tooltipText[kTooltipBufBytes]; // byte_75BE38

// Reset every global this module owns to the zeroed load-time state (for tests).
void ResetGuiDialogs3();

// ===========================================================================
// Installable hooks for the renderer / scene / form-animation edges that have no
// reconstructed target yet.  Defaults are inert (no-ops / identity); tests install
// their own to observe the call sequence.
// ===========================================================================
struct GuiDialogs3Hooks {
    // VIBE_Memory_AllocDebug @0x438f10 — debug heap alloc(size, tag). The
    // CreateRawBitmap path allocates 2*w*h bytes for the pixel copy; we return a
    // real heap block so the qmemcpy that follows is observable in tests.
    void* (*AllocDebug)(int size, const char* tag) = nullptr;

    // VIBE_Form_BroadcastClickResult @0x41ccc4 — propagate a click result to the
    // form's windows (renderer/input edge).  RefreshIfVisible calls it first.
    void (*FormBroadcastClickResult)(int formId) = nullptr;
    // VIBE_Form_SetButtonAnimations @0x41cd74 — advance the form's button anim state
    // from the (cursorX,cursorY) strings.  RefreshIfVisible's middle edge.
    void (*FormSetButtonAnimations)(int formId, const char* a2, const char* a3) = nullptr;
    // VIBE_DecompressGameState @0x41ceb4 — the form-refresh present edge.
    void (*FormPresent)(int formId) = nullptr;
};

// Install a hooks struct (nullptr restores the inert defaults). Returns the previous.
const GuiDialogs3Hooks* SetGuiDialogs3Hooks(const GuiDialogs3Hooks* hooks);

// ===========================================================================
// gilde.exe 0x40e94c — VIBE_Gui_ClipRectToBuffers (flags@eax, y@edx, h@ecx, x@ebx)
// Clamp the rect (y..y+h) to the active draw extent [g_drawClipTop, g_drawClipBottom],
// and — when `flags` bit 0 is set — round the x start up by 2 (and snap width even,
// dropping the flag bit).  The clamped {x,y,w,h} is then appended to each of the two
// clip-rectangle output buffers, and each buffer's length is incremented.  Returns 8
// (2 buffers * 4 — the original returns `index*4` after writing both).
// ===========================================================================
int Gui_ClipRectToBuffers(int flags, int y, int h, int x);

// ===========================================================================
// gilde.exe 0x414d98 — VIBE_Gui_HitTestObject (x@ax, y@dx)
// Scans the widget pointer cache from the top (slot 511) downward for the first
// non-disabled widget whose screen rect contains (x,y) and whose owning object's
// state flag (+408) is clear.  On a hit, records the widget id in g_hitTestSlot and
// returns the widget's data payload (+12->owner+620 for window-backed, or +8 value);
// for an object-state widget (owner state byte == 64) records and returns the +116
// object id via g_hitTestPayload.  Returns -1 (and g_hitTestSlot = -1) on a miss.
// `x`/`y` are screen coords (the original shifts them <<16 then >>16, i.e. integer).
int Gui_HitTestObject(i16 x, i16 y);

// ===========================================================================
// gilde.exe 0x414ec8 — VIBE_Gui_HitTestWindow (x@ax, y@dx)
// Walks the widget array from the high-water index (g_widgetHighWater, object.h) down
// for the first window-backing widget (type byte +24 == 64) whose frame rect contains
// (x,y), is enabled (+4), and whose interior band [+30,+32] contains y with no modal
// block (+56).  On a hit returns the widget's +8 id (and sets g_hitTestSlot to its
// slot index); -1 on a miss.  (g_widgetHighWater is dword_62D24C, owned by object.h.)
int Gui_HitTestWindow(i16 x, i16 y);

// ===========================================================================
// gilde.exe 0x41d7e0 — VIBE_Window_PositionAtCoord (winSlot@eax, corner@dl)
// Compute the anchor x for window `winSlot`: when (corner&1) is set, centre it
// horizontally about g_centreRefX using g_anchorDivisor; otherwise use the window's
// stored x (word @ +8 of the 952-byte record).  Clamp x so the window stays on
// screen ( <= g_screenClipExt-h ), convert via Coord_ConvertX, and hand the result to
// Widget_LayoutBounds on the window's backing widget.  Returns LayoutBounds' result.
int Window_PositionAtCoord(int winSlot, char corner);

// gilde.exe 0x41d964 — VIBE_Window_PositionAtCoord_Thunk (formId@eax, corner@dl)
// Resolves the form's window 0 (g_forms[formId].windowId(0)) and calls the above.
int Window_PositionAtCoord_Thunk(int formId, char corner);

// ===========================================================================
// gilde.exe 0x41b870 — VIBE_Window_ConsumeClickFlag ()
// Snapshots g_pendingClick into g_lastClick, clears g_pendingClick, and returns the
// snapshot (the click flag that was pending).  __stdcall(5 ignored args) in the orig.
// ===========================================================================
int Window_ConsumeClickFlag();

// ===========================================================================
// gilde.exe 0x412668 — VIBE_Widget_BlitClippedRows (widgetIdx@eax, blit@edx)
// Copy a widget's raw bitmap (rows of `2*w` bytes at widget+120) into a destination
// surface described by `blit` ({ +16: dst stride in pixels, +28: dst base ptr }),
// clipped vertically to the widget's [+30 .. min(+34top, owner clip)] band.  Each row
// is `qmemcpy`'d.  Returns the (last) row byte count (`2*w`).
// `dst` is the byte buffer the surface's +28 points at; `dstStridePx` is its +16.
int Widget_BlitClippedRows(int widgetIdx, int dstStridePx, unsigned char* dst);

// ===========================================================================
// gilde.exe 0x412560 — VIBE_Widget_CreateRawBitmap (x@ax, y@dx, w@ebx, h@ecx, pixels@stack)
// Allocate a widget slot (Widget_AllocSlot, REUSED) and initialise it as a raw-bitmap
// control (type byte 71 'G'): x/y/w at +16/+18/+20, h(count) at +22, id (+8) = -1,
// default clip from g_screenClipExt (+34 = LOWORD, +30 = HIWORD), then allocate
// 2*h*w bytes (AllocDebug hook, tag "d2:raw") at +120 and qmemcpy `pixels` in.
// Returns the new widget slot.  (The decompiler mislabels the 4th register arg as a
// stray local; the disasm confirms cx -> +22 and the alloc size is 2 * (+22) * w.)
int Widget_CreateRawBitmap(i16 x, i16 y, i16 w, i16 h, const void* pixels);

// gilde.exe 0x412618 — VIBE_Widget_AddRawBitmapToWindow (x@ax, y@dx, w@ebx, h@ecx, pixels, winSlot)
// Window-relative wrapper: CreateRawBitmap at (winX+x, winY+y) using the owning window's
// origin (word@+4 / word@+6), then attach to the window.
int Widget_AddRawBitmapToWindow(i16 x, i16 y, i16 w, i16 h, const void* pixels, int winSlot);

// ===========================================================================
// gilde.exe 0x41cc5c — VIBE_Form_RefreshIfVisible (formId@eax, cursorX@edx, cursorY@ebx)
// If the form is not loaded (valid()/+100 clear) or not shown (shownFlag()/+102 clear),
// return 0.  If either string arg is null, return 1 (loaded+shown, nothing to do).
// Otherwise broadcast the click result, re-apply button animations from the two
// strings, present, and return 1.  The three edges go through the hooks struct.
int Form_RefreshIfVisible(int formId, const char* cursorX, const char* cursorY);

// ===========================================================================
// gilde.exe 0x411eac — VIBE_Gui_FreeString_Thunk (widgetIdx@eax, src@edx)
// Copy up to 63 chars of `src` into the widget's +152 string field, NUL/zero-padding
// the remainder of the 63-byte span (VIBE_Util_StrNCopyPad semantics).  Returns the
// destination pointer (here: the byte offset is internal; we return the dst pointer
// as the original returns the dst address).  Exposed returning the copied length-ish
// dst for testability.
char* Gui_FreeString_Thunk(int widgetIdx, const char* src);

// ===========================================================================
// gilde.exe 0x421a24 — VIBE_Widget_SetTooltipText (src@eax)
// Copy `src` (including its NUL) into the global tooltip buffer g_tooltipText, two
// bytes per iteration (the original unrolls the copy).  Returns the final byte (0).
char Widget_SetTooltipText(const char* src);

// Shared NUL-terminating, zero-padding copy (VIBE_Util_StrNCopyPad @0x5d9360 semantics):
// copy up to `n` bytes of `src` into `dst`, stopping at a NUL in `src`, then zero-fill
// the rest of the `n`-byte span.  Reimplemented locally (the original is file-local in
// several modules; no single owning symbol to extern-reuse).  Returns `dst`.
char* StrNCopyPadLocal(char* dst, const char* src, int n);

} // namespace guild::gui
