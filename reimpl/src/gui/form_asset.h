#pragma once
// guild::gui — the BY-NAME VFS load wrappers for the GUI gfx catalogue
//              (the open-path half of VIBE_Gui_LoadGfxFile @0x41b888).
//
// VIBE_Gui_LoadGfxFile builds its resource path with
//     VIBE_Crt_Sprintf_0(buf, "%sgfx\\%s", prefix, name);
// (refs: aSgfxS == "%sgfx\\%s"), then VIBE_File_OpenStream(buf, "rb") and reads
// the file-object table (see gui/form_loader.{h,cpp}, which already recovers the
// table-baseline init + the 84-byte record parse from a byte buffer).
//
// This module supplies the missing "open BY NAME through the VFS" edge:
//   - build the `"%sgfx\\%s"` path from a directory prefix + a bare resource name,
//   - open it via guild::io::VfsOpenFile (REUSED), slurp it, and hand the bytes to
//     the existing Form_LoadFromBuffer (REUSED — NOT redefined here).
//
// ODR: Form_LoadFromBuffer / Form_LoadFromFile / Form_InitTables / the GfxObject
// table all live in gui/form_loader.{h,cpp}; we only add the name->path->VFS edge.

#include "guild/common/types.h"

#include <string>

namespace guild::gui {

using guild::u8;
using guild::u32;

// Build the gfx resource path exactly as VIBE_Gui_LoadGfxFile's sprintf does:
//   "<prefix>gfx\\<name>"
// `prefix` is the data-directory prefix the original passes in ecx (a3); pass ""
// for the bare "gfx\\<name>" form. The separator is a backslash to match the
// shipped resource layout (the VFS treats '\\' and '/' equivalently).
std::string Gfx_BuildPath(const char* prefix, const char* name);

// gilde.exe 0x41b888 — VIBE_Gui_LoadGfxFile (the open-by-name half).
// Builds "<prefix>gfx\\<name>", opens it through the VFS, reads the whole stream,
// and parses it with Form_LoadFromBuffer (which also runs Form_InitTables). The
// surface/palette/property setup the full original performs afterwards is a
// renderer/property-cluster edge and is out of this data-model slice (deferred;
// see gui/form_loader.h). Returns true on a successful open+parse.
//
// `prefix` defaults to "" (bare "gfx\\<name>"). The original's real call site is
// Render_ApplyGfxSettings -> Gui_LoadGfxFile(prefix, "gilde.gfx").
bool Gui_LoadGfxFile(const char* name, const char* prefix = "");

// Convenience alias matching the task's "Form_LoadByName": a VFS-open wrapper
// around Form_LoadFromBuffer that takes a bare resource name (built into the
// "gfx\\<name>" path). Identical to Gui_LoadGfxFile(name, prefix).
bool Form_LoadByName(const char* name, const char* prefix = "");

} // namespace guild::gui
