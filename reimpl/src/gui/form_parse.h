#pragma once
// guild::gui — the REAL `.form` resource-file parser recovered from gilde.exe.
//
// The shipped GUI screens live in `Resources/forms.BIN` (a PKZIP archive) as `.form`
// members (e.g. `Bauen/Geb_Bauen.form`). These are NOT the `gilde.gfx` gfx-object
// catalogue that gui/form_loader.cpp's Form_LoadFromBuffer parses — they use a
// distinct, retained-mode "FRM2" layout.
//
// THE PARSER:  gilde.exe 0x41beb8  (mislabeled VIBE_GameTick_Finalize in the IDB) —
//   VIBE_Form_LoadFromResource(a1@ax, a2@dx, name@ebx). It builds the path
//   "<dir>forms\\<name>.form" (when dword_62D29C==0), opens it via the VFS, sniffs
//   the 4-byte header, parses every window record, and *builds the live retained-mode
//   tables* by calling Window_Create / Window_AddChildWindow / Object_AddToWindow /
//   Object_AddTextLabel / Input_AddFieldToWindow / Widget_AddSliderToWindow.
//
// ===========================================================================
// THE `.form` BINARY FORMAT (recovered byte-for-byte from 0x41beb8 + real bytes)
// ===========================================================================
//
// File header (4 bytes), read into a scratch buffer; the format is selected by the
// 4th byte:
//   +0  3 bytes : "FRM"      (ignored by the parser)
//   +3  1 byte  : format digit. The parser computes  fmt = byte[3] - '0':
//                   '2' (0x32) -> fmt==2 : the "FRM2" layout  (record stride 4124)
//                   anything else        : the OLD layout     (record stride 3796)
//
//   FRM2 layout ("FRM2" magic, 321/323 shipped members):
//     +4  u32       windowCount
//     +8  windowCount * 4124-byte window records
//     (total size == 8 + windowCount*4124)
//
//   OLD layout (the other 2 shipped members: help.form, "ToolTip Geldsack.form"):
//     the parser SEEKS BACK TO 0 and re-reads:
//     +0  u32       windowCount
//     +4  windowCount * 3796-byte window records
//     (total size == 4 + windowCount*3796)
//
// Window record (offsets are bytes from the record base; the original views the
// record through a __int16* `v110`, so dword reads are at the byte offsets below):
//   +0   u16   x            (word[0])
//   +2   u16   y            (HIWORD of dword@+0)
//   +6   u16   w            (HIWORD of dword@+4)
//   +4   u16   h            (HIWORD of dword@+2)
//   +8   u32   flags        (dword@+8 -> Window_Create flags)
//   +204 u16   objectCount  (HIWORD of dword@+202)
//   parallel per-object arrays (index o in [0,objectCount)):
//     +10  + 2*o   dword : x16_16   (>>16 = object pixel x)      (v36+5)
//     +106 + 2*o   dword : y16_16   (>>16 = object pixel y)      (v36+53)
//     +208 + 8*o   dword : type     (5/64/65/67/69/0; the type tag)  (v35+52)
//     +3472+ 8*o   dword : aux      (per-type aux/button/flags)       (v35+868)
//     +400 + 64*o  char[64] : object name / property name           (v37 / v107)
//   window trailer (FRM2-specific dword/word offsets):
//     +3664  char[]  : window text (looked up via FindTextArrayIndex)
//     +3728  char[]  : font name   (e.g. "_FONT"); 0-flag -> default "_FONT"
//     +3792  u32     : parentIndex (0 = root window; else 1-based form window slot)
//     +3796  char[]  : palette name (Property_Validate)
//     +3860..3866 u16: child window bounds (v110[1930..1933]) copied to parent slots
//     +3980  u32     : window property (dword@995; a float when flags&... ; clamp)
//   OLD-layout trailer differs (parentIndex @ +3732, child bounds v110[1870..1873],
//   font v110[1864], scroll v110[934]); the object arrays are identical.
//
// Object type tags (the dword at +208+8*o):
//   5  : sprite / image object  -> Object_AddToWindow(win, y, x, Property_Validate(name))
//        aux==18 -> clickable (+68=1,+72=0); aux==8 -> toggle (+68=0,+72=1)
//   64 ('@') : nested-window backing object (also via Object_AddToWindow)
//   65 ('A') : numeric input field -> Input_AddFieldToWindow(x,y, aux>>16, aux>>8, ..)
//   67 ('C') : text label (only if name is a valid text-array id) -> Object_AddTextLabel
//   69 ('E') : slider -> Widget_AddSliderToWindow(x,y, .., Property_Validate(name), ..)
//   0        : inert / layout-only object (no widget created)
//
// ===========================================================================
//
// This module recovers the parse + the table-build 1:1, REUSING the existing leaves
// (Window_Create / Window_AddChildWindow / Object_AddToWindow / Object_AddTextLabel /
// Input_AddFieldToWindow / Widget_AddSliderToWindow — ODR: NOT redefined here).
// Property_Validate / FindTextArrayIndex are renderer/text-cluster edges; they are
// forward-declared and given neutral defaults so the data-model build is exercisable
// (tests can override). The function is named Form_ParseResourceFile — DISTINCT from
// the gfx-catalogue Form_LoadFromBuffer in gui/form_loader.h.

#include "gui/types.h"
#include "guild/common/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::gui {

using guild::u8;
using guild::u16;
using guild::u32;
using guild::i32;

// Record strides + header sizes, recovered from 0x41beb8.
inline constexpr int kFrm2RecordStride = 4124; // 0x101C  (fmt==2)
inline constexpr int kOldRecordStride  = 3796; // 0x0ED4  (else)
inline constexpr int kFrm2HeaderBytes  = 8;    // "FRM2" magic + u32 windowCount
inline constexpr int kOldHeaderBytes   = 4;    // u32 windowCount only

// Max forms the original allocates (scan caps at 48 -> dword_676E90, 171-dword stride).
inline constexpr int kMaxFormSlots = 48;

// Object type tags (dword at record +208+8*o).
inline constexpr i32 kFormObjSprite = 5;   // image/sprite (Object_AddToWindow)
inline constexpr i32 kFormObjWindow = 64;  // '@' nested-window backing object
inline constexpr i32 kFormObjInput  = 65;  // 'A' numeric input field
inline constexpr i32 kFormObjLabel  = 67;  // 'C' text label
inline constexpr i32 kFormObjSlider = 69;  // 'E' slider
inline constexpr i32 kFormObjInert  = 0;   // layout-only, no widget

// Aux selector for sprite objects (record +3472+8*o).
inline constexpr i32 kSpriteAuxClickable = 18; // +68=1, +72=0
inline constexpr i32 kSpriteAuxToggle    = 8;  // +68=0, +72=1

// ---------------------------------------------------------------------------
// Parsed records (a faithful, inspectable view of what the parser builds). The
// parser also drives the live tables; these structures expose the same data for
// tests and for callers that want the parse result without scraping the tables.
struct FormObjectRecord {
    i32  type    = 0;   // record +208+8*o
    i32  aux     = 0;   // record +3472+8*o
    i32  x       = 0;   // (record dword@+10+2*o) >> 16
    i32  y       = 0;   // (record dword@+106+2*o) >> 16
    std::string name;   // record +400+64*o (NUL-terminated)
    int  widgetIdx = -1; // slot returned by the create leaf (-1 if none built)
};

struct FormWindowRecord {
    int  windowSlot = -1;       // slot returned by Window_Create/AddChildWindow
    i32  x = 0, y = 0, w = 0, h = 0;
    u32  flags = 0;             // record dword@+8
    i32  parentIndex = 0;       // record dword@+3792 (FRM2) / +3732 (old); 0 = root
    int  parentWindowSlot = -1; // resolved owning window slot (-1 for root)
    std::string fontName;       // record +3728
    std::string windowText;     // record +3664
    std::vector<FormObjectRecord> objects;
};

struct FormFile {
    bool ok = false;
    bool frm2 = false;          // true: "FRM2" layout; false: old layout
    int  formId = -1;           // allocated form slot (dword_676A60 form id)
    std::string name;           // the resource name copied into the form record
    std::vector<FormWindowRecord> windows;
};

// gilde.exe 0x41beb8 — VIBE_Form_LoadFromResource (the byte-buffer parse half).
//
// Parses an already-loaded `.form` byte buffer (the original opens via the VFS and
// reads field-by-field; the byte sequence is identical). Detects FRM2 vs old layout
// from byte[3], allocates a form slot, and builds every window + object into the live
// retained-mode tables (Window_Create / Window_AddChildWindow / Object_AddToWindow /
// Object_AddTextLabel / Input_AddFieldToWindow / Widget_AddSliderToWindow — all
// REUSED). `name` is copied into the form record (form +139 dwords). Returns the
// parsed view; `.ok` is false on a malformed/short buffer.
//
// The surface/palette/property binding the full original performs (Property_Validate
// of fonts/palettes, FindTextArrayIndex of label text) is a renderer/text-cluster
// edge: those calls go through the forward-declared edges below (neutral defaults),
// so the data-model build (slots, geometry, child linkage, the form window-id table)
// is what is reproduced 1:1.
FormFile Form_ParseResourceFile(const u8* data, std::size_t len, const char* name = "");

// Lower-level header sniff: returns 2 for FRM2, 0 for the old layout (== the original
// `byte[3]-'0'` result when byte[3]=='2', else 0), or -1 if `len < 4`.
int Form_SniffFormat(const u8* data, std::size_t len);

// ===== Forward-declared renderer / text edges (neutral default; tests override) =====
// gilde.exe 0x40dfd4 — VIBE_Property_Validate: resolve a font/property/graphic id by
// name. Default returns -1 ("unresolved"); the build still creates the widget.
int Form_PropertyValidate(const char* name);
// gilde.exe 0x44e0d8 — VIBE_Text_FindTextArrayIndex: resolve a localized text id by
// name. Default returns -1 ("not a text id"); a label with an unresolved name is
// skipped exactly as the original skips it (it guards on index != -1).
int Form_FindTextArrayIndex(const char* name);

} // namespace guild::gui
