#pragma once
// guild::gui::text — the runtime "text file" slot table and the label table.
//
// At load time the engine tracks each loaded localized resource
// ("textbin_<lang>/<name>.res") in a fixed 128-entry table of 112-byte (0x70)
// records, in parallel
// with the global TextDb arrays (dword_8C36B0 strings / byte_8D36B0 names /
// byte_767EB0 tags). Each slot owns the heap blob holding that file's packed
// string bytes, so the slot table is what the loader/reloader/saver operate on.
//
// Recovered slot record (byte_77BEB0, 0x70 stride, 128 slots):
//   +0x00  char name[96]   the resource base name (the "%s" in textbin_..\%s.res)
//   +0x60  u32  baseIndex  dword_77BF10[28*slot] — global index of entry 0
//   +0x64  u32  lastIndex  dword_77BF14[28*slot] — global index of the last entry
//   +0x68  u32  blobPtr    dword_77BF18[28*slot] — heap pointer to the string blob
//   +0x6C  u32  blobSize   dword_77BF1C[28*slot] — blob byte count
// (the four trailing dwords are the same record viewed through the 28-dword
// stride: 28*4 == 112 == the record size.)
//
// Recovered originals modeled here:
//   VIBE_Text_FindTextFileSlot @0x44d8f8 — case-insensitive name -> slot, or -1
//   VIBE_Text_FreeTextFile     @0x44d940 — free one slot's blob by name
//   VIBE_Text_FreeAllTextFiles @0x44d8a4 — free every slot's blob, clear records
//   VIBE_Text_LookupLabelEntry @0x44add4 — label name -> text id via the label
//                                          name/value tables (unk_77F6B0 names,
//                                          dword_76BEB0 values, up to 0x3FFF)
//   VIBE_Text_ReloadTextFile   @0x44d970 — re-read a slot's .res from the VFS
//   VIBE_Text_SaveTextFile     @0x44de8c — write a slot back out as a .res
//
// File IO goes through the IFileSystem-backed VFS shim (guild::io::Vfs*), never
// the OS directly, exactly as the original routed through VIBE_Vfs_*.

#include "gui/text/textdb.h"
#include "guild/common/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::gui::text {

using guild::i32;
using guild::u8;
using guild::u32;

inline constexpr int kTextFileSlots   = 128;   // FindTextFileSlot bound (v6 < 128)
inline constexpr int kTextFileStride  = 112;   // 0x70 record stride
inline constexpr int kLabelStride     = 80;    // unk_77F6B0 name stride
inline constexpr int kLabelMaxEntries = 0x3FFF; // LookupLabelEntry bound

// One loaded resource's record + the heap blob it owns.
struct TextFileSlot {
    std::string      name;       // +0x00  byte_77BEB0[112*slot]
    u32              baseIndex;  // +0x60  dword_77BF10[28*slot]
    u32              lastIndex;  // +0x64  dword_77BF14[28*slot]
    std::vector<u8>  blob;       // +0x68/+0x6C  dword_77BF18 ptr / dword_77BF1C size
    bool             used;       // record live? (name[0] != 0 in the original)

    TextFileSlot() : baseIndex(0), lastIndex(0), used(false) {}
};

// The 128-entry slot table (byte_77BEB0 + parallel dword_77BF1x arrays).
class TextFileTable {
public:
    TextFileTable() : slots_(kTextFileSlots) {}

    // VIBE_Text_FindTextFileSlot @0x44d8f8 — first slot whose name matches `name`
    // case-insensitively AND is live (byte_77BEB0[112*slot] != 0). Returns the
    // slot index or -1 (after scanning all 128). Original walks records with a
    // 112-byte stride and tests `byte_77BEB0[112*slot]` for the live flag.
    int FindSlot(const char* name) const;

    // VIBE_Text_FreeTextFile @0x44d940 — look the slot up by name; if found, free
    // its blob (dword_77BF18[28*slot]) and clear that pointer. Returns the freed
    // pointer-equivalent (>=0 ok / -1 when no such slot), mirroring the original's
    // `result` (which is -1 from FindTextFileSlot when absent).
    int FreeTextFile(const char* name);

    // VIBE_Text_FreeAllTextFiles @0x44d8a4 — for every live slot, free the blob,
    // null the pointer, and zero the 112-byte record (clearing name + indices).
    void FreeAllTextFiles();

    // Direct slot access (for the loader/reloader/saver and tests).
    int          Count() const { return static_cast<int>(slots_.size()); }
    TextFileSlot&       At(int i)       { return slots_[i]; }
    const TextFileSlot& At(int i) const { return slots_[i]; }

    // Allocate (or reuse) a slot by name and mark it live. Returns the index.
    int Acquire(const char* name);

private:
    std::vector<TextFileSlot> slots_;
};

// The label name/value tables (unk_77F6B0 names @80-stride, dword_76BEB0 values).
// Populated by the .txt authoring path; modeled here so LookupLabelEntry is
// testable in isolation.
class LabelTable {
public:
    // Append a (name -> value) pair (the order the authoring path adds them).
    void Add(const std::string& name, i32 value);

    int Count() const { return static_cast<int>(names_.size()); }

    // VIBE_Text_LookupLabelEntry @0x44add4 — case-insensitive scan over the label
    // name table (max 0x3FFF entries); returns the parallel value (dword_76BEB0[i])
    // on the first match, else -1. The original stops at the table cap or a null
    // record pointer; we stop at Count() which matches the populated extent.
    i32 Lookup(const char* name) const;

private:
    std::vector<std::string> names_;   // unk_77F6B0
    std::vector<i32>         values_;  // dword_76BEB0
};

// gilde.exe 0x44d970 — VIBE_Text_ReloadTextFile. Re-open this slot's .res through
// the VFS, re-read the header (entryCount/baseIndex/lastIndex), the per-entry
// name records (80 bytes each into byte_8D36B0[80*(base+i)]), the per-entry tag
// bytes (byte_767EB0[base+i]), the packed blob, and rebuild the string-pointer
// table (dword_8C36AC[base+1+i] = blob + offset[i]). `lang` is the original's
// aGerman. Returns true on success (slot found + opened). `db` receives the
// rebuilt entries at [baseIndex, lastIndex]; the slot's blob is replaced.
bool ReloadTextFile(TextFileTable& table, TextDb& db, const char* name,
                    const char* lang = "german");

// gilde.exe 0x44de8c — VIBE_Text_SaveTextFile. Write slot `slot` back out as a
// "<root>\german\textbin_<lang>\<name>.res": header (count/base/last), the per-
// entry offset table (dword_8C36B0[i] - blobPtr), the 80-byte name records, the
// tag bytes, the blob size and the blob. Returns true on success. `db` supplies
// the strings/names/tags for [baseIndex, lastIndex]; the on-disk byte layout
// matches what ReloadTextFile/BuildTextArray read back.
bool SaveTextFile(const TextFileTable& table, const TextDb& db, int slot,
                  const char* root, const char* lang = "german");

} // namespace guild::gui::text
