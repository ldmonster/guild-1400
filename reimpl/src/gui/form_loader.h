#pragma once
// guild::gui — the .form / .gfx resource LOADER + the form/window/object table
//              initialiser (the data half of VIBE_Gui_LoadGfxFile @0x41b888).
//
// "Die Gilde" keeps its whole retained-mode GUI catalogue in a single binary
// resource (the game ships `gilde.gfx`, opened as "%sgfx\\%s"). The loader does
// two distinct things, both recovered here byte-for-byte:
//
//   1. PARSE the resource file:
//        +0x00  u32   objectCount          (capped at 2048; else
//                                            "d2_Open:Too many Objects in GFX-File...")
//        +0x04  objectCount × 84-byte gfx-object records  -> the "d2:fileobj" table
//                                            (dword_62D204, 84-byte / 0x54 stride).
//      After the read it walks the records and, for any record whose flag byte
//      (+68) has bit 0x1 set AND whose dword at +56 is non-zero, registers a
//      runtime scene-state for it (VIBE_State_Helper — a renderer edge, deferred).
//
//   2. INITIALISE the three retained-mode tables to their "all slots free, each
//      slot stamped with its own index" baseline:
//        - 48  Form   records: form.dword[0] (684-byte stride) = formIndex.
//        - 96  Window records: window.dword[0] (238-dword stride) = windowIndex.
//        - 512 Widget records: widget.dword[0] (740-byte stride) = widgetIndex.
//      (These index stamps are the recycled-id values the alloc/destroy paths
//       read back; gui/window.cpp's Window_Destroy stores `slot` into window+0,
//       and Widget free recycles widget+0 the same way.)
//
// The surface creation, palette/property binding, and font setup that the rest of
// VIBE_Gui_LoadGfxFile performs are renderer/property-cluster edges and are NOT in
// this module (they have no data-model footprint here). What IS recovered is the
// file format, the object-count cap, the 84-byte record table population, and the
// table baseline init — i.e. "the .form file parse -> form/window/object records".

#include "gui/types.h"
#include "guild/common/types.h"

#include <cstddef>

namespace guild::io { struct VfsHandle; }

namespace guild::gui {

// ---------------------------------------------------------------------------
// The gfx-object ("d2:fileobj") record — 84-byte / 0x54 stride, base dword_62D204.
// Only the fields the GUI cluster actually reads are named; the rest is raw so the
// stride matches the on-disk record exactly. (The renderer cluster owns the full
// meaning of most fields; the GUI reads the width/height metrics and the +56/+68
// "has a scene-state" flags.)
//   +56  (dword)  scene/source handle  (non-zero + flag bit 0x1 => State_Helper)
//   +68  (byte)   flags  (bit 0x1 = needs a runtime scene-state registered)
//   +78  (word)   width   (HIWORD of dword@+78 in the 16.16 reads; markup uses it)
//   +82  (word)   height  (HIWORD of dword@+82; Widget_CreateSprite copies it)
inline constexpr int kGfxObjStrideBytes = 84;   // 0x54
inline constexpr int kMaxGfxObjects     = 2048; // dword_62D208 cap

struct GfxObject {
    u8 raw[kGfxObjStrideBytes];

    GfxObject();
    template <typename T> T&       at(int off)       { return *reinterpret_cast<T*>(raw + off); }
    template <typename T> const T& at(int off) const { return *reinterpret_cast<const T*>(raw + off); }
    // Unaligned by-value load — byte-identical to the original's unaligned x86 read.
    template <typename T> T ld(int off) const {
        T v; std::memcpy(&v, raw + off, sizeof(T)); return v;
    }

    i32& sceneHandle() { return at<i32>(56); } // +56
    u8&  flags()       { return at<u8>(68); }  // +68  (bit 0x1)
    // +78/+82 are unaligned 16.16 dwords (offset % 4 == 2). Read by value through an
    // unaligned load (the original does an unaligned `mov`); >>16 gives the pixels.
    i32  width()  const { return ld<i32>(78); } // +78  (16.16; >>16 = pixels)
    i32  height() const { return ld<i32>(82); } // +82  (16.16; >>16 = pixels)
};
static_assert(sizeof(GfxObject) == kGfxObjStrideBytes, "GfxObject must be 84 bytes");

// The loaded gfx-object table (dword_62D204 "d2:fileobj") and its count
// (dword_62D208). Sized to the original cap; only [0, g_gfxObjectCount) are valid.
extern GfxObject g_gfxObjects[kMaxGfxObjects]; // dword_62D204
extern int       g_gfxObjectCount;             // dword_62D208

// Flag bit on GfxObject::flags (+68).
inline constexpr u8 kGfxFlagHasState = 0x1;

// Number of form/window/widget slots the initialiser stamps (recovered loop
// bounds from VIBE_Gui_LoadGfxFile: 48 forms, 96 windows, 512 widgets).
inline constexpr int kFormInitCount   = 48;
inline constexpr int kWindowInitCount = 96;
inline constexpr int kWidgetInitCount = 512;

// Reset the gfx-object table + count (BSS is zero in the original; provided so
// tests start clean). Folded into ResetGuiState by the loader TU.
void ResetGfxObjects();

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (the table-baseline init half).
// Stamps each Form/Window/Widget slot's dword[0] with its own index, marking the
// whole catalogue "all free" exactly as the original's three init loops do. Call
// once before parsing a resource (Form_LoadFromBuffer does this for you).
void Form_InitTables();

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (the file-parse half), reading from an
// already-loaded byte buffer instead of the stream (the original calls
// VIBE_File_OpenStream + VIBE_File_Read; here the caller supplies the bytes, which
// is how the resource arrives after the VFS read).
//
// Layout consumed, byte-for-byte:
//   get u32 objectCount @+0  (little-endian)
//   if objectCount > 2048  -> log "...Too many Objects..." and return false (-> 0).
//   for each of objectCount records: copy 84 bytes into g_gfxObjects[i].
//   then walk records: (+68 & 0x1) && +56 != 0  -> RegisterGfxState(i)  (edge).
// On success g_gfxObjectCount is set and the function returns true. `len` guards
// against a short buffer (returns false if it cannot satisfy the declared count).
// Pass `initTables=true` (default) to run Form_InitTables() first, matching the
// original which always inits the tables in the same call.
bool Form_LoadFromBuffer(const u8* data, std::size_t len, bool initTables = true);

// Convenience: open `path` through the VFS (guild::io::VfsOpenFile, REUSED), read
// the whole stream into a temporary buffer, and Form_LoadFromBuffer it. Mirrors the
// original's open("%sgfx\\%s","rb") + read flow. Returns false on open/parse error.
bool Form_LoadFromFile(const char* path);

// ===== Forward-declared renderer edge (stubbed in tests) ===================
// gilde.exe 0x40e014 — VIBE_State_Helper: register a runtime scene-state for the
// gfx object at index `gfxIndex` so the renderer can animate/draw it. Pure renderer
// side effect; the data-model parse calls it but tests provide a counting stub.
void RegisterGfxState(int gfxIndex);

} // namespace guild::gui
