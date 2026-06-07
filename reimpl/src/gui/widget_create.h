#pragma once
// guild::gui — the widget-CREATION leaves the dialog/panel/markup builders forward to.
//
// Every higher-level screen builder (messagebox, trade panel, building dialog, the
// markup parser ...) ultimately calls one of a handful of "create a control and link it
// into a window" leaves. This module recovers those leaves 1:1 from gilde.exe and wires
// them onto the already-translated retained-mode core (Form/Window/Object/Widget):
//
//   VIBE_Object_AddTextLabel        @0x41b288  type 'C' (67)  static text label
//   VIBE_Object_AddButtonLabel      @0x41b598  type 'F' (70)  text button
//   VIBE_Object_SetText             @0x41b0ec  rewrite a label's text + width
//   VIBE_Object_SetEditText         @0x41b75c  rewrite an edit field's text (model part)
//   VIBE_Widget_CreateSprite        @0x4120a8  type 9         sprite/image control
//   VIBE_Widget_AddSpriteToWindow   @0x41217c  sprite + attach-to-window  (Object_AddSprite)
//   VIBE_Widget_CreateSlider        @0x410180  type 'E' (69)  slider (field-init model part)
//   VIBE_Widget_AddSliderToWindow   @0x410604  slider + attach-to-window
//   VIBE_Input_RegisterField        @0x40fe04  type 'A' (65)  numeric/text input record
//   VIBE_Input_AddFieldToWindow     @0x410030  input field + attach-to-window
//   VIBE_Input_RegisterIcon         @0x40f800  type 'A' (65)  icon/checkbox input record
//   VIBE_Input_AddIconToWindow      @0x40fa18  icon + attach-to-window  (Object_AddCheckbox)
//   VIBE_Object_RecomputeSize       @0x41b164  recompute a control's width from its text
//   VIBE_Object_SetUserData         @0x41dad4  stash a 32-bit user value at widget +36
//   VIBE_Object_SetValueOrText      @0x41dfec  set value/text/slider-range (Object_SetObjectValueOrText)
//   VIBE_Widget_SetButtonState      @0x41f054  push a type-4 button's pressed/checked state
//
// Each "create" leaf: bounds-checks the window (enabled + <384 children), allocates a
// 740-byte widget slot (VIBE_Widget_AllocSlot, REUSED), recovers the widget's
// type-specific field layout BYTE-FOR-BYTE at the original offsets, links the widget into
// the window's child id-list (REUSING WindowChildList) and the Z-order
// (ZOrder_InsertObject, REUSED), and grows the window content-height.
//
// Glyph/sprite blit and the renderer-side geometry transforms are forward-declared edges
// (Property_Get / Surface_Create / Coord_Transform / GameObject_AttachToWindow / ...);
// the model — allocation, field init, child linkage — is what is translated here. Tests
// provide minimal stubs for the blit edges.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------------------
// Recovered widget type tags (the original compares the byte at widget +24 directly).
//   'C' 0x43 = static text label   (AddTextLabel)
//   'F' 0x46 = text button         (AddButtonLabel)
//   'E' 0x45 = slider / edit field (CreateSlider, SetValueOrText edit branch)
//   'A' 0x41 = animated/input-field-backed object (RegisterField / RegisterIcon)
//    9  0x09 = sprite / image       (CreateSprite)
//    4  0x04 = checkbox-style toggle button targeted by SetButtonState
inline constexpr u8 kTypeButton  = 0x46; // 'F'
inline constexpr u8 kTypeSprite  = 0x09;
inline constexpr u8 kTypeToggle  = 0x04;

// ---------------------------------------------------------------------------------------
// Screen-extent / default-clip globals adjacent to the widget array base.
//   dword_69FFB0  default control height word (copied into widget +22 on label/button)
//   dword_69FFB8  >>16 = screen clip width  (resize clamp)
//   dword_69FFBC        screen clip extent  (default clip x1/y1 dword; HIWORD = x1)
// They are zero in the on-disk BSS and are filled in at runtime when the display mode is
// chosen; tests set them explicitly.
extern i32 g_defaultCtrlH;   // dword_69FFB0
extern i32 g_screenClipW;    // dword_69FFB8
extern i32 g_screenClipExt;  // dword_69FFBC

// Input-field record table (unk_695080 / dword_695080): 87-dword (348-byte) stride,
// capacity 128 (slot 0 is the "no field" sentinel; the original scans from slot 1).
inline constexpr int kInputFieldStrideDwords = 87;
inline constexpr int kMaxInputFields         = 128;
struct InputField {                       // 348 bytes
    i32 dw[kInputFieldStrideDwords];
    InputField();
    template <typename T> T& at(int byteOff) { return *reinterpret_cast<T*>(reinterpret_cast<u8*>(dw) + byteOff); }
};
extern InputField g_inputFields[kMaxInputFields]; // unk_695080
void ResetInputFields();

// Per-button toggle state table (byte_1406420.. 17-byte stride) for SetButtonState.
inline constexpr int kButtonStateStride = 17;
inline constexpr int kMaxButtonStates   = 256;
struct ButtonState {                      // 17 bytes
    u8 b[kButtonStateStride];
    ButtonState();
};
extern ButtonState g_buttonStates[kMaxButtonStates]; // dword_1406420
void ResetButtonStates();

// Reset every leaf-owned table (input fields + button states + screen-clip globals).
void ResetWidgetCreate();

// ===== Forward-declared renderer / property edges (stubbed in tests) ====================
// gilde.exe 0x4152cc — VIBE_Property_Get: measure the pixel width of `text` in `font`.
i16  Property_Get(const char* text, int font);
// gilde.exe 0x40dfd4 — VIBE_Property_Validate: resolve a font/property id by name.
int  Property_Validate(const char* name);
// gilde.exe 0x40e57c — VIBE_GameObject_AttachToWindow: link a freshly-created widget into
// window `winSlot` (Z-order + child-id list); the model half is Widget_LinkToWindow below.
void GameObject_AttachToWindow(int widgetIdx, int winSlot);

// Graphics-metric / scene-state edges (renderer cluster data, table dword_62D204, 84-byte
// stride; VIBE_State_Update / VIBE_Coord_Transform). The slider/sprite SIZE math reads
// packed 16.16 metrics through these; tests seed them. Neutral defaults return 0/null.
i16   GfxMetricWord(int gfxId, int byteOff);      // word @ table[gfxId] + byteOff
i32   GfxMetricDword(int gfxId, int byteOff);     // dword @ table[gfxId] + byteOff
i16   SliderTrackExtent(int gfxBase, int flags);  // cross-axis thumb-track extent
void* SceneStateFor(int gfxId);                   // VIBE_State_Update(gfxId)
int   GlyphAdvance(void* state, int which);       // glyph metric advance (sprite size)
int   ButtonBankFrameCount(int rec);              // frame count for a button-state record

// ===== Text-label / button leaves =======================================================

// gilde.exe 0x41b288 — VIBE_Object_AddTextLabel (x@ax, y@dx, winSlot@ecx, text@ebx)
// Creates a type-'C' text label in window `winSlot`: copies `text` into the widget's
// 0x101-byte text buffer (+116), inherits the window font (+112), positions it at
// (winX+x, winY+y-scroll), sizes it to Property_Get(text)+96, links it as a child and into
// the Z-order, and grows content-height. Returns the new widget slot, or -1.
int Object_AddTextLabel(i16 x, i16 y, int winSlot, const char* text);

// gilde.exe 0x41b598 — VIBE_Object_AddButtonLabel (x@ax, y@dx, winSlot@ecx, text@ebx)
// Same as AddTextLabel but type 'F' (button); text goes to +124, clip x0/y0 are zeroed and
// clip x1/y1 default to the screen-extent global. Returns the new widget slot, or -1.
int Object_AddButtonLabel(i16 x, i16 y, int winSlot, const char* text);

// gilde.exe 0x41b0ec — VIBE_Object_SetText (widgetIdx@eax, text@edx)
// Rewrites a label/button widget's text buffer (+116) and, unless its +88/+92 override
// flags are set, recomputes its width (+20) = Property_Get(text). Returns +20 or a handle.
int Object_SetText(int widgetIdx, const char* text);

// ===== Sprite leaves ====================================================================

// gilde.exe 0x4120a8 — VIBE_Widget_CreateSprite (x@ax, y@dx, gfxId@ebx, mode@ecx)
// Creates a type-9 sprite widget for graphics id `gfxId`: height (+22) comes from the
// graphics-metric table (84-byte stride, word @+82), +12 = scene-state handle, +72 button
// flag = 1, +444 = 3, font (+112) from the window default. `mode` is forwarded to
// Object_RecomputeSize. Returns the new widget slot.
int Widget_CreateSprite(i16 x, i16 y, int gfxId, int mode);

// gilde.exe 0x41217c — VIBE_Widget_AddSpriteToWindow (x@ax, y@dx, gfxId@ebx, winSlot@ecx)
// Window-relative wrapper: CreateSprite at (winX+x, winY+y) then attach to the window.
// (This is the "Object_AddSprite" leaf the panel builders forward to.)
int Widget_AddSpriteToWindow(i16 x, i16 y, int gfxId, int winSlot);
inline int Object_AddSprite(i16 x, i16 y, int gfxId, int winSlot) { return Widget_AddSpriteToWindow(x, y, gfxId, winSlot); }

// ===== Slider leaves ====================================================================

// gilde.exe 0x410180 — VIBE_Widget_CreateSlider (x@ax,y@dx,value@ecx,range@ebx,max,gfxBase,flags)
// Creates a type-'E' (69) slider widget. The MODEL half (field init) is translated here:
//   +16/+18 x,y   +120/+124 value   +128 max   +132 flags   +136 range   +140 step(=max/2)
//   +144 gfxBase  +72 = 1 (clickable)   +24 = 0x45 'E'   +8 = 0   +28/+32 = 0 clip
// plus the +20/+22 thumb-track size derived from the slider's gfx metrics. The surface
// allocation + sprite-sheet blit loop is DEFERRED (renderer cluster). Returns the slot.
int Widget_CreateSlider(i16 x, i16 y, int value, int range, int maxVal, int gfxBase, i16 flags);

// gilde.exe 0x410604 — VIBE_Widget_AddSliderToWindow (...,winSlot)
// Bounds-checks the window, creates the slider at (winX+x, winY+y-scroll), attaches it.
int Widget_AddSliderToWindow(i16 x, i16 y, int value, int range, int maxVal, int gfxBase, i16 flags, int winSlot);

// ===== Input-field / icon (checkbox) leaves =============================================

// gilde.exe 0x40fe04 — VIBE_Input_RegisterField (x@eax, y@edx, step@ecx, value@ebx, flags@cl)
// Allocates an input-field record (87-dword), seeds it (max=999, font=_FONT, digit width),
// then allocates a type-'A' (65) widget mirroring the record's x/y/w/h and pointing +12 at
// the record / +116 at its index. Returns the widget slot, or -1 when 127 fields exist.
int Input_RegisterField(int x, int y, int step, int value, u8 flags);

// gilde.exe 0x410030 — VIBE_Input_AddFieldToWindow (x@eax,y@edx,step@ecx,value@ebx,flags,winSlot)
// Window-relative wrapper: RegisterField at (winX+x, winY+y-scroll[+584]) then link into the
// window's child list + Z-order, inheriting clip bounds. Returns the widget slot, or -1.
int Input_AddFieldToWindow(int x, int y, int step, int value, u8 flags, int winSlot);

// gilde.exe 0x40f800 — VIBE_Input_RegisterIcon (x@eax, y@edx, flags@cl, gfxId@ebx)
// Allocates an input-field record sized from a gfx icon (metric table 84-byte stride) and a
// type-'A' (65) widget. Returns the widget slot, or -1.
int Input_RegisterIcon(int x, int y, u8 flags, int gfxId);

// gilde.exe 0x40fa18 — VIBE_Input_AddIconToWindow (x@eax,y@edx,flags@cl,gfxId@ebx,winSlot)
// Window-relative wrapper (the "Object_AddCheckbox" leaf): RegisterIcon at window-relative
// coords, link into the window child list + Z-order, grow the scroll thumb. Returns slot.
int Input_AddIconToWindow(int x, int y, u8 flags, int gfxId, int winSlot);
inline int Object_AddCheckbox(int x, int y, u8 flags, int gfxId, int winSlot) { return Input_AddIconToWindow(x, y, flags, gfxId, winSlot); }

// ===== Sizing / value setters ===========================================================

// gilde.exe 0x41b164 — VIBE_Object_RecomputeSize (widgetIdx@eax)
// Recomputes a control's width (+20): for a type-9 sprite from its two-glyph metrics, else
// from Property_Get(text) + anim margin. Also pads the text buffer to 63 bytes. The glyph
// metric reads go through the (stubbed) Coord_Transform edge. Returns the new width.
int Object_RecomputeSize(int widgetIdx);

// gilde.exe 0x41dad4 — VIBE_Object_SetUserData (localId@eax, value@edx)
// Resolves the current form's localId-th object (form object-base table) and stores `value`
// at widget +36 (the generic value slot). Returns the widget array base handle.
int Object_SetUserData(int localId, int value);

// gilde.exe 0x41dfec — VIBE_Object_SetValueOrText (widgetIdx, text/value, a3, a4, a5)
// The general value/text setter. The MODEL branches translated here:
//   type 'E' (69) edit/slider: +124=a3, +128=... , +120=a4, and if flags(+132)&0x10: +140=a5
//   button (+68/+72 set):      +36 = (u8)value, +40 mirror
// The type-'A' (65) anim branches (text copy into +40, the +296 slider-range quantiser) are
// included; the anim-record edges go through the (stubbed) data pointer. Returns a status.
char Object_SetObjectValueOrText(int widgetIdx, int textOrValue, int a3, int a4, int a5);

// gilde.exe 0x41b75c — VIBE_Object_SetEditText (widgetIdx@eax, text@edx)
// Rewrites a type-'E'-with-window-text-buffer edit field's inline copy (+124) and width
// (+20). The owning window's shared text-buffer splice (the +44/+120/+188 memmove dance)
// is DEFERRED; the model part (the inline +124 text copy + width) is translated. Returns 0.
char Object_SetEditText(int widgetIdx, const char* text);

// gilde.exe 0x41f054 — VIBE_Widget_SetButtonState (widgetIdx@eax, state@dl)
// For a type-4 toggle widget, pushes its pressed/checked state into the per-button state
// table (17-byte stride, indexed by widget +116): state 0 clears, state 1 sets pressed +
// frame from the bank record. Returns a handle/offset. No-op for non-type-4 widgets.
int Widget_SetButtonState(int widgetIdx, char state);

// ---------------------------------------------------------------------------------------
// Shared helper (the common tail of every "AddXToWindow" leaf): link `widgetIdx` into
// window `winSlot` — inherit the backing widget's clip/render pointers, append to the
// child id-list, insert into the Z-order, bump the child count. Mirrors the identical
// epilogue the original leaves share.
void Widget_LinkToWindow(int widgetIdx, int winSlot);

} // namespace guild::gui
