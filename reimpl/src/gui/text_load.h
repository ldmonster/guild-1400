#pragma once
// guild::gui::text — the localized text ".res" file loader + the binary parser
//                    that fills the indexed string array (the TextDb).
//
// Recovered originals:
//   VIBE_Text_LoadTextFile   @0x44dba0 — open "textbin_<lang>\\<name>.res" through
//                                        the VFS and read the compiled binary into
//                                        the global text array, names and tags.
//   VIBE_Text_BuildTextArray @0x44bb5c — the master text *compiler*: it reads the
//                                        human-authored ".txt" definition source
//                                        (#define labels, /* */ comments, "strings",
//                                        {rN} random tokens) into the in-memory array
//                                        AND emits the compiled ".res" binary that
//                                        LoadTextFile reads back. The two functions
//                                        agree byte-for-byte on the .res layout; the
//                                        full .txt front-end (2106 instrs, MessageBox
//                                        UI, file-write side) is deferred — what we
//                                        recover here is the *.res binary* that both
//                                        produce/consume, parsed from a VFS buffer
//                                        into the TextDb (the runtime load path).
//
// ===== Recovered .res file format (byte-for-byte) ==========================
// All integers are little-endian u32 (read with 4-byte VIBE_Vfs_ReadStream).
//
//   +0x00  u32  entryCount         number of strings in this file
//   +0x04  u32  baseIndex          global text-array index of this file's first
//                                  string (the slot record's dword@+96; the writer
//                                  stores `v220` here)
//   +0x08  u32  lastIndex          baseIndex + entryCount - 1 (the new total-1;
//                                  the writer stores `dword_62EB24 - 1` here)
//   +0x0C  u32  offset[entryCount] byte offset of each string into the blob
//                                  (writer: dword_8C36B0[i] - blobBase)
//   ...    u8   name[entryCount][80]  per-entry name/key, 80-byte (0x50) stride,
//                                     NUL-padded (byte_8D36B0 stride)
//   ...    u8   tag[entryCount]    per-entry tag byte (byte_767EB0): 0xFF = plain,
//                                  9..18 = a {rN} random-group marker
//   +X     u32  blobSize           total bytes of the packed string blob
//   +X+4   u8   blob[blobSize]     NUL-terminated strings, packed back-to-back; a
//                                  '|' authored separator was already turned into a
//                                  NUL by the compiler, so each entry's offset points
//                                  at a NUL-terminated C string inside the blob.
//
// The reader (LoadTextFile) then sets, for i in [0,entryCount):
//   dword_8D36B0[80*(baseIndex+i)] = name[i]          (the name array)
//   byte_767EB0[baseIndex+i]       = tag[i]           (the tag array)
//   dword_8C36B0[baseIndex+i]      = blob + offset[i]  (the text pointer array)
// and advances the global count dword_62EB24 to lastIndex+1.

#include "gui/text/textdb.h"
#include "guild/common/types.h"

#include <cstddef>
#include <string>

namespace guild::gui::text {

using guild::u8;
using guild::u32;

inline constexpr int kResNameStride = 80; // 0x50  (byte_8D36B0 name stride)

// Result of a .res parse: the strings/names/tags found, in file order, plus the
// declared base index the file targets.
struct TextResFile {
    u32 baseIndex  = 0;            // header +0x04
    u32 lastIndex  = 0;            // header +0x08
    int entryCount = 0;            // header +0x00
    bool ok        = false;        // false if the buffer was malformed/short
};

// gilde.exe 0x44bb5c — VIBE_Text_BuildTextArray (the .res binary -> TextDb half).
// Parse a compiled ".res" buffer (format above) and append its entries to `db` in
// file order, honoring the declared baseIndex (entries before baseIndex are filled
// with empty placeholders so an entry's TextDb index equals baseIndex+i, exactly as
// the original places dword_8C36B0[baseIndex+i]). Returns the parse result; on a
// short/malformed buffer `ok` is false and `db` is left unchanged.
//
// The full original additionally compiles the .txt source and writes the .res; that
// authoring/UI/file-write front-end is deferred (see report). This is the runtime
// data-model half both LoadTextFile and the compiler agree on.
TextResFile BuildTextArray(const u8* data, std::size_t len, TextDb& db);

// gilde.exe 0x44dba0 — VIBE_Text_LoadTextFile (open BY NAME through the VFS).
// Builds the path "textbin_<lang>\\<name>.res" (the original's
// VIBE_Crt_Sprintf_0(buf, "textbin_%s\\%s.res", "german", name)), opens it via
// VfsOpenFile (REUSED), slurps the stream, and BuildTextArray()s it into `db`.
// `lang` defaults to "german" (the shipped aGerman constant). Returns true on a
// successful open+parse.
bool Text_LoadTextFile(const char* name, TextDb& db, const char* lang = "german");

// Build the .res resource path exactly as VIBE_Text_LoadTextFile's sprintf does.
std::string Text_BuildPath(const char* lang, const char* name);

} // namespace guild::gui::text
