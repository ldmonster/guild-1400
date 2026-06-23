#pragma once
// play/input_icon_text.{h,cpp} — the cursor-icon editable-label text injector
// (gilde.exe 0x40fb4c — VIBE_Input_SetIconTextById).
//
// This is LOGIC, not a DirectInput boundary: it splices a localized string
// (dword_8C36B0[textId]) into an object's editable text buffer at a recorded
// insertion point, then re-measures the widget's pixel width and stores it back into
// the live widget record. It is called from the markup window builder
// (VIBE_Window_ParseMarkupAndBuild @0x416720, call @0x417872) and from the widget
// mouse-drag handler (VIBE_Widget_ProcessMouseDrag @0x420db4, call @0x421294).
//
// Original (a1@eax = object index into the stride-348 array unk_695080;
//           a2@edx = string-table index into dword_8C36B0):
//
//   obj   = &unk_695080[348 * a1];               // the editable-object record
//   widx  = obj[+0x130];                          // its widget-record index
//   wrec  = dword_69FFB4 + 740*widx;              // the live widget record (stride 740)
//   under = *(i32*)(wrec + 0x2C);                 // pointer to the underlying-text record
//   if (under == 0 || obj[+0x150] == 0) return;   // no underlying text / no edit buffer
//
//   // obj[+0x150] = edit-buffer base ptr; obj[+0x154] = currently-inserted length.
//   if (obj[+0x154] != 0)                         // remove the previous insertion:
//     // memmove(base, base+len, tailLen)  where
//     //   tailLen = *(i32*)(under+0x2C) + strlen(*(char**)(under+0x2C)) + 1 - base
//     MemMove(base, base + len, tailLen);
//
//   newLen = strlen(dword_8C36B0[a2]);            // obj[+0x154] = newLen
//   // open a gap of newLen at base:
//   //   memmove(base + newLen, base, *(i32*)(under+0x2C)+strlen(...)+1 - base)
//   MemMove(base + newLen, base, tailLen2);
//   memcpy(base, dword_8C36B0[a2], newLen);       // splice the new string in
//
//   w = Property_Get(dword_8C36B0[a2], obj[+0x134]);   // measure (font = obj[+0x134])
//   obj[+0x10] = w;                                     // store measured width
//   *(u16*)(wrec + 20) = (u16)obj[+0x10];              // push low word to the widget rec
//
// The memmove tail-length term `*(i32*)(under+0x2C) + strlen(*(char**)(under+0x2C))
// + 1 - base` is the number of bytes from `base` to the end (incl. NUL) of the
// underlying text region: `under+0x2C` numerically is the region base, and the C-string
// it points to gives the length. We reproduce this arithmetic exactly.
//
// We model the coupled state explicitly (the object record fields the function reads/
// writes, the underlying-text region, the edit buffer) and route Property_Get through
// a hook (VIBE_Property_Get @0x4152cc is a separate still-missing leaf). The memmove
// ORDER, the gap arithmetic, the byte copy and the width writeback are 1:1.

#include "guild/common/types.h"

namespace guild::play {

using guild::i16;
using guild::i32;
using guild::u16;

// The editable-object record fields the injector touches (subset of the stride-348
// unk_695080 record). Backed by a caller-owned byte buffer `editBuf` that plays the
// role of obj[+0x150] (the edit-buffer base); `insertedLen` is obj[+0x154].
struct IconTextObject {
    unsigned char* editBuf = nullptr;  // obj[+0x150] edit-buffer base (0 == skip)
    i32  insertedLen = 0;              // obj[+0x154] currently-inserted byte count
    i32  font = 0;                     // obj[+0x134] font id (passed to Property_Get)
    i16  measuredW = 0;                // obj[+0x10]  measured width (output)
};

// The underlying-text region the tail-length is measured against (wrec+0x2C). The
// region base is `regionBase` (numerically *(i32*)(under+0x2C)); the C-string at
// that base gives the length. In the original these alias the same edit storage.
struct IconTextUnderlying {
    i32 regionBase = 0;                // *(i32*)(under+0x2C) (numeric base offset)
    const char* text = nullptr;        // *(char**)(under+0x2C) (for strlen)
    bool present = true;               // under != 0 gate
};

// Property_Get hook — VIBE_Property_Get @0x4152cc (measure text pixel width for a
// font). Default null -> measuredW stays 0.
using IconTextMeasureFn = i16 (*)(const char* text, int font);

// gilde.exe 0x40fb4c — splice `newText` into `obj`'s edit buffer at base offset 0,
// replacing any previously-inserted substring, then re-measure and store the width.
//
// `editBufBase` is the numeric value of obj[+0x150] (the base offset used by the
// tail-length arithmetic `tail = under.regionBase + strlen(under.text) + 1 - base`).
// Returns the new widget-record low-word width written (the original's last store),
// or 0 if the gate (under absent / no edit buffer) skipped the body.
u16 Input_SetIconTextById(IconTextObject& obj, i32 editBufBase,
                          const IconTextUnderlying& under,
                          const char* newText, IconTextMeasureFn measure);

} // namespace guild::play
