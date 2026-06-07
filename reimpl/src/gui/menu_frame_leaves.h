#pragma once
// =============================================================================
// guild::gui — SUPPORTING LEAVES that gui::Menu_RunMainMenu (0x529d08) and the
// main-menu sub-screens call each frame.  This module reconstructs the genuinely
// -missing 1:1 leaves (the ones not already recovered elsewhere in src/):
//
//   0x41f0e8  VIBE_Fade_Register             (alloc + register a 100-byte fade record)
//   0x41f18c  VIBE_Fade_Unregister           (release a fade slot + its texture)
//   0x4134f0  VIBE_Window_RenderEntityList   (draw the live entity list + fades, present)
//   0x41b494  VIBE_Object_CreateTextLabel    (create a 'C' text-label widget)
//   0x41e614  VIBE_Object_SetColor           (write a widget's colour word @+112)
//   0x41dd98  VIBE_Object_SetVisibleRecursive(set widget visible flag, recurse children)
//   0x412970  VIBE_InitStateReader           (radio-group keyboard/mouse state reader)
//
// SKIPPED — already reconstructed as REAL functions elsewhere (see the report):
//   0x41523c VIBE_Window_RenderEntityScene  -> gui/gui_dialogs7.cpp:Window_RenderEntityScene
//   0x41beb8 VIBE_GameTick_Finalize         -> gui/form_parse.cpp:Form_ParseResourceFile
//                                              (the IDB mislabels it; it is the .form loader)
//   0x41b494 ... (see above; this one is NEW here)
//   0x41e4cc VIBE_Form_SelectWindow         -> gui/form.cpp:Form_SelectWindow
//   0x41da04 VIBE_Form_Destroy              -> gui/form_lifecycle.cpp:Form_Destroy
//   0x41d964 VIBE_Window_PositionAtCoord_Thunk -> gui/gui_dialogs3.cpp:Window_PositionAtCoord_Thunk
//   0x41287c VIBE_RadioGroup_FreeSurface_Thunk -> gui/gui_dialogs7.cpp:RadioGroup_FreeSurface_Thunk
//
// REUSED real siblings (NOT redefined; ODR-safe):
//   g_fadeSlots[32]   (gui/gui_dialogs7.h, dword_672280)   — fade-slot array
//   g_widgets[]       (gui/object.h,       dword_69FFB4)    — widget records
//   g_windows[]       (gui/window.h,       dword_67EB80)    — window records
//   g_defaultFont     (gui/window.h,       dword_62D2B0)    — current label colour/font
//   g_radioGroups[]   (gui/radiogroup.h,   dword_676584)    — radio group count/sel/buttons
//   Widget_AllocSlot  (gui/object.h,       0x412dac)        — widget slot allocator
//   Property_Get      (gui/widget_create.h,0x4152cc)        — text-array width lookup
//   Selection_Update  (gui/radiogroup.h,   0x41290c)        — radio exclusive select
//
// HOST/DDraw edges (memory alloc, texture release, render present, scene draw,
// the coord-warp) go through an installable MenuFrameLeavesHooks with INERT
// defaults defined in the .cpp, so the library links and everything is testable
// headless.
// =============================================================================

#include "gui/types.h"
#include "guild/common/types.h"

namespace guild::gui {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;
using guild::u32;

// ---------------------------------------------------------------------------
// Fade record (d2:fadeinfo, 100 bytes — base offsets from Register/Update).
//   +0    (byte)  flags  (bit0 fade-in dir, bit1 fade-out dir, bit2 done,
//                         bit3 teardown)
//   +44   rect x   +48 rect y   +52 rect w   +56 rect h   (set by Update math)
//   +68   (dword)  rect x       +72 rect y       +76 rect w       +80 rect h
//   +84   (dword)  duration ticks   (a3 -> +20*4=+80? see below)
//   +88   (dword)  start tick   +92 last tick
//   +96   (dword)  texture handle  (Render_FillBackBuffer result)
// Register writes (dword-index form): [17]=+68 x, [18]=+72 y, [19]=+76? see ctor;
//   [20]=+80, [21]=+84, [24]=+96 texture; +88/+92 = now.  flags byte @ +0.
// We store the record as an exact 100-byte blob with typed accessors.
inline constexpr int kFadeRecordBytes = 0x64; // 100
inline constexpr int kFadeSlotCount   = 32;   // dword_672280[32]

// Fade flag bits (the +0 byte).
inline constexpr u8 kFadeDirIn   = 0x01; // bit0
inline constexpr u8 kFadeDirOut  = 0x02; // bit1
inline constexpr u8 kFadeDone    = 0x04; // bit2  ("the bit-4 done flag" == value 0x4)
inline constexpr u8 kFadeTeardown= 0x08; // bit3

struct FadeRecord {
    u8 raw[kFadeRecordBytes];
    template <typename T> T&       at(int off)       { return *reinterpret_cast<T*>(raw + off); }
    template <typename T> const T& at(int off) const { return *reinterpret_cast<const T*>(raw + off); }

    u8&  flags()    { return at<u8>(0);   }   // +0
    i32& rectX()    { return at<i32>(68);  }  // +68  (dword [17])
    i32& rectY()    { return at<i32>(72);  }  // +72  (dword [18])
    i32& rectW()    { return at<i32>(76);  }  // +76  (dword [19])
    i32& rectH()    { return at<i32>(80);  }  // +80  (dword [20])
    i32& duration() { return at<i32>(84);  }  // +84  (dword [21])
    i32& startTick(){ return at<i32>(88);  }  // +88
    i32& lastTick() { return at<i32>(92);  }  // +92
    i32& texture()  { return at<i32>(96);  }  // +96  (dword [24])
};

// ---------------------------------------------------------------------------
// Host / renderer edges.  Inert defaults defined in the .cpp.
struct MenuFrameLeavesHooks {
    // VIBE_Memory_AllocDebug(size, "d2:fadeinfo") — returns a 100-byte block. The
    // default hands back a real owned FadeRecord so the data model is exercisable.
    FadeRecord* (*allocFade)(int size, const char* tag);
    // VIBE_Memory_FreeDebug(block) — release a fade record. Default frees an owned block.
    void (*freeFade)(FadeRecord* block);
    // VIBE_Surface_ReleaseTexture(textureHandle) — DDraw texture release. Default no-op.
    void (*releaseTexture)(i32 textureHandle);
    // VIBE_Render_FillBackBuffer(mode, pixels, color) — snapshot the back buffer into a
    // texture; returns the new texture handle. Default returns 0.
    i32 (*fillBackBuffer)(int mode, const u32* pixels, int color);

    // ---- Window_RenderEntityList edges ----
    // VIBE_GameLogic_Entities(0, 0, a1) — walk + draw the live entity/object list.
    void (*gameLogicEntities)(int a, int b, i32 arg);
    // VIBE_Result_Handler_Interaction(0,0, srcW, srcH, src, 0,0, dst) — surface blit.
    void (*resultHandlerInteraction)(int a, int b, i32 w, i32 h, i32 src,
                                     int c, int d, i32 dst);
    // VIBE_Fade_Update(slot, back) — advance one fade (real sibling lives in render
    // cluster; routed here so the iteration order is observable).
    void (*fadeUpdate)(i32 fadeSlot, i32 backSurface);
    // VIBE_Render_PresentFrame(flags) — flip the composed frame.
    void (*renderPresentFrame)(void* flags);
    // VIBE_Gui_Nop() — the trailing no-op in RenderEntityList.
    void (*guiNop)();

    // ---- InitStateReader edges ----
    // VIBE_Coord_ConvertX/Y warp — move the hardware cursor to a widget centre when the
    // arrow keys change the radio selection. Default no-op.
    void (*coordWarp)(int x, int y);
};

// Install a hooks vtable (nullptr restores inert defaults). Returns the previous.
const MenuFrameLeavesHooks* SetMenuFrameLeavesHooks(const MenuFrameLeavesHooks* hooks);

// =====================================================================
// Input-state globals consumed by VIBE_InitStateReader (0x412970).  These are the
// keyboard/mouse latches the original keeps in BSS; modeled here as this module's
// faithful state (no existing shared home in src/).  Reset by ResetMenuFrameLeaves().
//   byte_67225C   last key scancode      (-56 == UP/0xC8, -48 == DOWN/0xD0, 28 == ENTER)
//   dword_672228  click-edge flag        (set to 1 when ENTER fires a button)
//   dword_62D22C  current selected widget id
//   unk_67220E    mouse x (16.16; >>16 = pixel x)
//   dword_672210  mouse y (16.16)
//   dword_69FFC0  last latched mouse x ;  dword_69FFC4  last latched mouse y
//   dword_62D2FC  focus latch
//   dword_62D328  external selection override (0 == none)
//   dword_62D294  active-popup gate (-1 when no popup)
//   dword_672250  DOWN-arrow held ; dword_672254  UP-arrow held
//   dword_67221C  wrap-allowed flag ; dword_75BF38  hover-help/tooltip id
// =====================================================================
struct MenuInputState {
    i32 lastKey;          // byte_67225C (stored as int; the original compares signed bytes)
    i32 clickFlag;        // dword_672228
    i32 selectedWidget;   // dword_62D22C
    i32 mouseX1616;       // unk_67220E
    i32 mouseY1616;       // dword_672210
    i32 lastMouseX;       // dword_69FFC0
    i32 lastMouseY;       // dword_69FFC4
    i32 focusLatch;       // dword_62D2FC
    i32 selectionOverride;// dword_62D328
    i32 popupGate;        // dword_62D294
    i32 downHeld;         // dword_672250
    i32 upHeld;           // dword_672254
    i32 wrapAllowed;      // dword_67221C
    i32 hoverHelpId;      // dword_75BF38
};
extern MenuInputState g_menuInput;

// byte_676580[140*g] bit0 — per-radio-group "this button is interactive" gate read by
// InitStateReader. Overlaps the surface-descriptor block that gui_dialogs7.cpp models;
// here we expose only the leading flags byte the state reader consults.
extern u8 g_radioGroupFlags[8]; // byte_676580 record +0, per group

// Key scancodes the state reader recognises (signed-byte values as the original stores).
inline constexpr int kKeyUp    = -56; // 0xC8  (move selection up)
inline constexpr int kKeyDown  = -48; // 0xD0  (move selection down)
inline constexpr int kKeyEnter = 28;  // 0x1C  (activate selected button)

// Reset all module-owned state (fade slots, input latches, group flags).
void ResetMenuFrameLeaves();

// ---------------------------------------------------------------------------
// gilde.exe 0x41f0e8 — VIBE_Fade_Register
// __userpurge(a1@eax, a2@edx, a3@ecx, a4@ebx, a5, a6, a7) with the field writes:
//   +68(x)=a1, +72(y)=a2, +80(h)=a3, +76(w)=a4, +0(flags)=a7, +84(dur)=a6,
//   +96(texture)=Render_FillBackBuffer(1, a5/pixels, a3/h), +88/+92(start/last)=now.
// Finds the first free entry in g_fadeSlots[32]; if full (>=32 used) returns 0.
// Allocates a 100-byte d2:fadeinfo record, fills it, snapshots the back buffer into a
// texture, stamps start/last tick = now, stores the record pointer into the free slot,
// and returns the record handle (a synthetic non-null id), or 0 when the table is full.
//   x,y       : rect origin (+68/+72)   [a1/a2]
//   h         : rect height (+80)       [a3] (also the FillBackBuffer colour arg)
//   w         : rect width  (+76)       [a4]
//   flags     : the +0 flags byte       [a7] (bit0 in / bit1 out)
//   duration  : +84                     [a6]
//   pixels    : back-buffer pixels      [a5]
//   now       : current global tick (dword_62EB44) for start/last
i32 Fade_Register(i32 x, i32 y, i32 h, i32 w, u8 flags, i32 duration,
                  const u32* pixels, i32 now);

// gilde.exe 0x41f18c — VIBE_Fade_Unregister
// Finds `handle` in g_fadeSlots[32]; clears that slot, releases the record's texture
// (+96) and frees the record. Returns 0 if it was slot 0 or found; else result*4 (the
// not-found sentinel == 32*4 == 128).
i32 Fade_Unregister(i32 handle);

// gilde.exe 0x4134f0 — VIBE_Window_RenderEntityList
// One menu/idle frame: draw the entity list, blit it onto the work surface, run every
// active fade, present, blit back, and the trailing nop.
void Window_RenderEntityList(i32 arg);

// gilde.exe 0x41b494 — VIBE_Object_CreateTextLabel
// Allocates a widget slot, owns a 257-byte text buffer (d2:ShowTxt), copies `text` in,
// stamps it as a type-'C' (67) label at (x,y) with colour word (+112) = g_defaultFont,
// width = Property_Get(text)+96, and the standard label clip/screen bounds. Returns the
// widget slot, or -1 on alloc failure.
int Object_CreateTextLabel(i16 x, i16 y, const char* text);

// gilde.exe 0x41e614 — VIBE_Object_SetColor
// Writes the colour word (+112) of widget `idx`. For a type-64 ('@' window-backing)
// widget the colour also propagates to the owning window's title-colour word; for a
// type-65 ('A') widget it propagates to the object's colour dword. Returns a byte
// (the original's al — the resolved value).
u8 Object_SetColor(i32 idx, i32 color);

// gilde.exe 0x41dd98 — VIBE_Object_SetVisibleRecursive
// Sets widget `idx`'s visible flag (+52 = (vis==0)) — i.e. +52 holds the HIDDEN flag,
// so passing vis!=0 shows it (flag=0), vis==0 hides it (flag=1). For a type-64
// window-backing widget it recurses into every child object of the owning window.
// Returns the last child count touched (the original's eax).
int Object_SetVisibleRecursive(int idx, int vis);

// gilde.exe 0x412970 — VIBE_InitStateReader
// Reads keyboard/mouse for radio group `group`: tracks the focused button, on UP/DOWN
// steps the selection (wrapping), warps the cursor to the new button centre, and on
// ENTER fires the selected button (sets g_menuInput.clickFlag=1, selectedWidget=button,
// hoverHelpId). Mutates the group's selected index via Selection_Update. Returns the
// resolved widget handle / control value (the original's eax).
int InitStateReader(int group);

} // namespace guild::gui
