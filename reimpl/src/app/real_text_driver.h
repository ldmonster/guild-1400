#pragma once
// gilde.exe — REAL game-asset TEXT-DB driver (guild::app).
//
// INTEGRATION GLUE, not a translation. Closes the loop from a real "Die Gilde"
// install to a populated localized-text database (gui::text::TextDb):
//
//   1. MountRealGameAssets(fs, gameDir) reads the real Gilde.INI, binds the VFS,
//      and mounts the Resources/*.BIN PKZIP archives (real_boot.{h,cpp}).
//   2. This driver walks the chosen text archive's central directory
//      (textbin_deutsch.BIN / textbin.BIN — ~101 compiled ".res" members), and
//      for each ".res" member extracts its bytes through the reconstructed
//      ArchiveMount/ZipArchive reader and feeds them to the recovered text parser
//      (gui::text::BuildTextArray @0x44bb5c). Each .res declares a baseIndex; the
//      members tile the global text-array contiguously, so the shared TextDb ends
//      up with one indexed entry per string (count == max lastIndex + 1).
//   3. The caller then RESOLVES string ids — by name (TextDb::FindIndex, the
//      case-folded VIBE_Text_FindTextArrayIndex) and by index (TextDb::Text) — and
//      asserts non-empty results.
//
// Everything reached is real reconstructed code over real bytes; the only OS
// boundary is shim::IFileSystem. Nothing here edits the shared spine (wiring.cpp).
//
// Why not Text_LoadTextFile over the bound VFS? The reconstructed VfsOpenFile
// resolves a ".BIN" path to the archive's FIRST member only (it does not index
// members by name), so the per-member load path goes through the ArchiveMount the
// real boot already builds. The same reconstructed .res parser is driven either
// way.
#include "app/real_boot.h"
#include "gui/text/textdb.h"
#include "gui/text_load.h"
#include "shim/IFileSystem.h"

#include <cstddef>
#include <string>
#include <vector>

namespace guild::app {

// One loaded ".res" member: what the parser reported for it.
struct TextResMemberResult {
    std::string member;     // archive member name (normalized, e.g. "TEXT_N_NACHRICHTEN.RES")
    bool        ok = false; // BuildTextArray succeeded
    std::size_t bytes = 0;  // extracted .res blob size
    guild::u32  baseIndex = 0;
    guild::u32  lastIndex = 0;
    int         entryCount = 0;
};

// Result of loading a whole text archive into a TextDb.
struct RealTextDbResult {
    bool        archiveMounted = false; // the text archive was found + mounted
    std::size_t resMembers = 0;         // number of ".res" members seen
    std::size_t resLoaded  = 0;         // number that parsed ok
    int         entryCount = 0;         // db.Count() after all members (== maxLastIndex+1)
    std::vector<TextResMemberResult> members; // per-member detail (file order)
};

// Load every ".res" member of `archiveMember` (a mounted Resources/*.BIN, e.g.
// "Resources/textbin_deutsch.BIN") into `db` via the reconstructed BuildTextArray.
// Reuses the ArchiveMount the real boot already opened (assets.archives). Returns
// the aggregate result; on a missing/unmounted archive `archiveMounted` is false
// and `db` is left untouched.
RealTextDbResult LoadRealTextDb(RealGameAssets& assets,
                                const std::string& archiveMember,
                                gui::text::TextDb& db);

// One end-to-end resolved string id.
struct ResolvedString {
    std::string key;   // the requested name/key (as asked)
    int         index = -1;    // TextDb::FindIndex(key), or -1 if absent
    bool        found = false;
    std::string text;          // TextDb::Text(index), empty if absent
    guild::u8   tag = 0;       // TextDb::Tag(index)
};

// Resolve a set of name keys against a populated TextDb (case-folded name lookup
// + index -> string), collecting the resolved text/tag for each. A key that is not
// found gets index=-1, found=false. Pure read-side helper over the reconstructed
// TextDb lookups.
std::vector<ResolvedString> ResolveStringKeys(const gui::text::TextDb& db,
                                              const std::vector<std::string>& keys);

// Convenience full driver: MountRealGameAssets(fs, gameDir) -> LoadRealTextDb of
// the requested text archive into `db`. The caller owns VfsShutdown() (the bound
// VFS is process-global, per real_boot). `archiveMember` defaults to the German
// text DB. Returns the load result; the populated `db` is the caller's to query.
RealTextDbResult MountAndLoadRealTextDb(
    shim::IFileSystem* fs,
    const std::string& gameDir,
    gui::text::TextDb& db,
    const std::string& archiveMember = "Resources/textbin_deutsch.BIN");

} // namespace guild::app
