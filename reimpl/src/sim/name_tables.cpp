// gilde.exe — first-name / dynasty-name string tables (namespace guild::sim).
// See name_tables.h for the full provenance. This file wires the already-
// reconstructed text-resource loader (guild::gui::text::BuildTextArray /
// guild::io::ZipArchive) to the three fixed name slices the person / new-game
// code reads (dword_8C400C / 8C4320 / 8C4508), so that over the real install the
// RandomModulo(0xBF)/(0x70)/(0x95) draws index real names instead of "".

#include "sim/name_tables.h"

#include "gui/text/textdb.h"
#include "gui/text_load.h"
#include "io/zip_archive.h"

#include <memory>
#include <vector>

namespace guild::sim {

using guild::gui::text::TextDb;
using guild::gui::text::BuildTextArray;

namespace {

// The loaded global text array (dword_8C36B0 model). One per process, mirroring
// the original's single module-global text registry.
std::unique_ptr<TextDb> g_db;

// Did the parse actually span the name id ranges?
bool g_loaded = false;

// Slot count of a slice that is actually present in the db (clamped to the db
// extent), so an out-of-range index returns "" rather than reading garbage.
bool SliceOk(int base, int count) {
    if (!g_db)
        return false;
    // The db is filled [0..baseIndex) with empty placeholders then the entries, so
    // a slot is "present" when its index is < Count(). We only require the *first*
    // name of each slice to be in range to consider the slice usable; per-index
    // bounds are still checked in NameAt.
    (void)count;
    return base < g_db->Count();
}

} // namespace

void NameTables_Reset() {
    g_db.reset();
    g_loaded = false;
}

bool NameTables_Loaded() {
    return g_loaded;
}

const TextDb* NameTables_Db() {
    return g_db.get();
}

bool NameTables_LoadFromResBuffer(const u8* data, std::size_t len) {
    if (!data || len < 12)
        return false;

    auto db = std::make_unique<TextDb>();
    guild::gui::text::TextResFile r = BuildTextArray(data, len, *db);
    if (!r.ok)
        return false;

    g_db = std::move(db);
    // Require the file to actually cover the name ranges (the Personen.res file
    // does: its entries span baseIndex .. lastIndex which contains 599..1066). If
    // a different/short file was supplied the slices simply read "".
    g_loaded = SliceOk(kNameMaleBase, kNameMaleCount) &&
               SliceOk(kNameFemaleBase, kNameFemaleCount) &&
               SliceOk(kNameDynastyBase, kNameDynastyCount);
    return g_loaded;
}

bool NameTables_LoadFromArchive(guild::shim::IFileSystem* fs, const char* binPath) {
    if (!fs || !binPath)
        return false;

    // Faithful runtime path: open the PKZIP textbin archive and extract the named
    // member (VIBE_Zip_OpenArchive + LocateFileByName + OpenCurrentFile), then
    // hand the compiled .res bytes to BuildTextArray — exactly what the engine's
    // VIBE_Text_LoadTextFile does once the VFS has resolved the member.
    guild::io::ZipArchive z;
    if (!z.Open(fs, binPath))
        return false;

    std::vector<u8> res;
    if (!z.ExtractByName(kNameResMember, res, /*caseSensitive=*/false))
        return false;

    return NameTables_LoadFromResBuffer(res.data(), res.size());
}

const char* NameAt(int kind, int index) {
    if (!g_loaded || !g_db || index < 0)
        return "";

    int base = 0;
    int count = 0;
    switch (kind) {
        case kNameMale:    base = kNameMaleBase;    count = kNameMaleCount;    break;
        case kNameFemale:  base = kNameFemaleBase;  count = kNameFemaleCount;  break;
        case kNameDynasty: base = kNameDynastyBase; count = kNameDynastyCount; break;
        default: return "";
    }
    if (index >= count)
        return "";

    int id = base + index;          // dword_8C400C[index] == dword_8C36B0[base+index]
    const char* s = g_db->Text(id); // null when out of range
    return s ? s : "";
}

} // namespace guild::sim
