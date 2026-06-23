#pragma once
// guild::play — REAL save/load for the play session (namespace guild::play).
//
// The play session loads a world from a `.cty` / `.SAV` stream and must be able
// to write it back in the ORIGINAL on-disk format. The original's save flow
// (recovered from gilde.exe):
//
//   VIBE_Menu_RunSaveGame   @0x56a804  — the save screen; writes slot files
//       "Gamedata/saves/GILDE_SAVEGAME_%i.SAV"  (string @0x624f6c, slots 2..15)
//       via VIBE_Save_WriteGameFile @0x5a348c (flags=1, FULL save).
//   VIBE_Save_DoQuickSave   @0x56d984  — "Gamedata\Saves\Quicksave.SAV"
//       (string @0x6252b8), header slot tag byte_649D50 = 1.
//   Autosave                           — "Saves\Autosave.SAV" (string @0x623551).
//   VIBE_Map_LoadCityFile   @0x528bd0  — writes "%s/%s.CTY" under
//       "gamedata/cities" (string @0x63ccb4) via WriteGameFile flags=2: the
//       PARTIAL save — exactly the stream io::LoadWorld consumes (the shipped
//       `.cty` city seeds ARE partial saves). VIBE_Net_LoadAndSyncSession
//       @0x56da74 uses the same partial path for live network session snapshots.
//   The save browser (VIBE_SaveBrowser_LoadSlotMetadata @0x569d00) scans
//       "gamedata/saves" (string @0x624f30) for ".SAV" (string @0x624f08)
//       through VIBE_SaveBrowser_EnumerateSaveFiles @0x569530; reserved names
//       "QUICKSAVE" @0x624ef0 / "AUTOSAVE" @0x624efc map to slot 0
//       (VIBE_SaveBrowser_FindSaveSlot @0x569c50).
//
// This module delivers the session-level API over those reconstructions:
//
//   LoadLiveWorld   — the load driver (mirrors VIBE_Save_LoadGameFile @0x5a7604,
//       PARTIAL path) that, unlike io::LoadWorld, CAPTURES every side table the
//       stream carries (header+thumbnail, scalar block, map tiles, counters,
//       extra-96, person-preamble ids, building-slot tables, Aemter) so a later
//       save can write the REAL bytes back. Populates the live sim arrays.
//   SaveLiveWorld   — serialize the live world to a file in the original format
//       via io::SaveWriteGameFilePartial (the @0x5a348c partial path, version
//       0x10045, header flag bit 1 set). The exact inverse of the loader.
//   EnumerateSaves  — the original save-dir scan (".SAV" filter over
//       "gamedata/saves") + per-file header metadata (save name / slot tag).
//
// SCOPE (named, not faked — rule 8): the FULL `.SAV` tail (Gesetz / MapTiles /
// GameGlobals / Avatar / ObjectTable / AmtTable / History / ActionQueues /
// Hotkeys / CityAndPersonTables / Mission) is not driven here — its load mirror
// in io::LoadWorld is equally deferred, so a partial stream is the complete
// faithful session snapshot today (the original itself uses it for .cty/.NET
// session snapshots). LoadLiveWorld REFUSES a non-partial file rather than
// half-loading it. The scene-object sidecar (VIBE_WorldIo_SaveSceneObjects via
// VIBE_Save_RelinkPersonExtraData @0x5a3f14) writes a separate stream and is
// deferred with the universe/scene module.
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"

namespace guild::shim { class IFileSystem; }

namespace guild::play {

// --- the original's save-dir layout (recovered strings) ---------------------
extern const char kSaveBrowseDir[];    // "gamedata/saves"            @0x624f30
extern const char kSaveSlotPathFmt[];  // "Gamedata/saves/GILDE_SAVEGAME_%i.SAV" @0x624f6c
extern const char kQuickSaveName[];    // "QUICKSAVE"                 @0x624ef0
extern const char kAutoSaveName[];     // "AUTOSAVE"                  @0x624efc

// Real-install paths for the same layout. The original resolves its VFS-relative
// "gamedata/saves" against the Resources root (GfxPath); on disk that is
// "<gameDir>/Resources/gamedata/Saves" (note the real install's capital S).
std::string SaveSlotPath(int slot);    // "Resources/gamedata/Saves/GILDE_SAVEGAME_<slot>.SAV"
std::string QuickSavePath();           // "Resources/gamedata/Saves/Quicksave.SAV"
std::string AutoSavePath();            // "Resources/gamedata/Saves/Autosave.SAV"

// --- load ---------------------------------------------------------------------
struct SessionLoadInfo {
    bool          ok = false;        // fully parsed, version-valid, partial
    guild::u32    version = 0;       // the file's magic (0x10026..0x10045)
    bool          partial = false;   // header flag bit 1 (the .cty/session case)
    bool          aemterLoaded = false; // version >= 0x10045 Aemter block consumed
    std::uint32_t objectCount = 0;   // live object slots populated
    std::uint32_t personCount = 0;   // live person/scene records populated
    std::uint32_t tileCount = 0;     // map-tile index records
};

// Load `path` (a .cty city seed or a partial .SAV written by SaveLiveWorld) into
// the live sim arrays through the reconstructed loaders, capturing every side
// table for a faithful re-save. Binds the VFS to `fs` (io::VfsInit) and blanks
// the world first (the play-layer determinism rig + Srand(seed)). A non-partial
// (full .SAV) file is refused after parsing its partial prefix (ok=false,
// partial=false) — never half-loaded.
SessionLoadInfo LoadLiveWorld(guild::shim::IFileSystem& fs, const char* path,
                              std::uint32_t seed = 0x4711);

// --- save ---------------------------------------------------------------------
// Serialize the live world to `path` in the original partial-save format at
// version 0x10045 (the writer's fixed version): header (flag bit 1, `saveName`
// into the name fields, `slotTag` into byte_649D50) + scalar block + map tiles +
// object table + counters + person records + building-slot tables + Aemter.
// Side tables come from the last LoadLiveWorld capture (zeroed if none).
// Binds the VFS to `fs`; the file is created via the VFS "wb" path (the
// original's VIBE_Vfs_OpenFile(path, "wb") @0x5a34a3). Returns true on success.
bool SaveLiveWorld(guild::shim::IFileSystem& fs, const char* path,
                   const char* saveName = "Savegame", guild::u8 slotTag = 1);

// --- enumerate ------------------------------------------------------------------
struct SaveListEntry {
    std::string fileName;   // loose file name incl. extension (browser record +9)
    std::string browsePath; // the browser's "basePath/name" (truncated at '.')
    std::string openPath;   // real-cased VFS path to open the file
    bool        headerOk = false;  // header parsed
    std::string saveName;   // header +5 name (e.g. "QUICKSAVE", user save name)
    guild::u32  version = 0;       // header magic
    guild::u8   slotTag = 0;       // header byte_649D50 (quick=1, slots 2..15)
    bool        reservedSlot0 = false; // name is QUICKSAVE/AUTOSAVE (slot 0 rule)
};

// Scan the save directory the way the original browser does: build the VFS tree
// over `treeRoot` (default "Resources", case-insensitive like the Windows
// original), enumerate "gamedata/saves" for ".SAV" via
// io::SaveBrowserEnumerateSaveFiles @0x569530, then read each file's header
// metadata (the load-bearing part of VIBE_SaveBrowser_LoadSlotMetadata @0x569d00;
// its window/label GUI glue is out of scope). `openDirReal` is the real-cased
// on-disk dir the files are opened from. Returns the number of saves found.
int EnumerateSaves(guild::shim::IFileSystem& fs, std::vector<SaveListEntry>& out,
                   const char* treeRoot = "Resources",
                   const char* openDirReal = "Resources/gamedata/Saves");

// --- last-load capture accessors (wave-3, ADDITIVE) ------------------------------
// The header city/scenario name of the LAST successful LoadLiveWorld (SaveHeader
// +0x05 name[32]). For the shipped .cty city seeds this IS the city name (e.g.
// AUGSBURG.cty carries "Augsburg" — the same field app/wiring.cpp publishes as
// cityName_), which is how the load-game session derives the 3D city scene when
// the partial .SAV embeds no scene stream (the engine's own saves embed the
// VIBE_WorldIo_SaveSceneObjects sidecar read by VIBE_Save_PostLoadInitScene
// @0x5a7ef8 -> Scene_LoadFromStream @0x5e7e38 — that WRITE side is a named gap,
// see progress/session-save.md). Returns "" when no capture is live.
std::string SessionLoadedCityName();

// byte_6477A1 ("season"/city money-rate byte) of the LAST LoadLiveWorld's scalar
// block (@0x5a76d6.. — the original stores it straight into the global). This is
// the byte's only live home in the reimpl today; `ok` (optional) reports whether
// a capture is live. Shipped .cty seeds carry 0 here (verified on AUGSBURG.cty);
// the fresh-city rate at the new-game purse seeding is 100 — see the
// InitOrLoadSession reconstruction (src/app/session_init.cpp, the 0x533fXX..
// purse block: cityRate = 100).
guild::u8 SessionLoadedRateByte(bool* ok = nullptr);

// --- determinism witness --------------------------------------------------------
// Re-anchor the CRT RNG (the base digest folds the live RNG state) and fold the
// full world. Within one process run, two identical worlds (e.g. before a save
// and after its reload) hash equal.
std::uint64_t SessionWorldHash(std::uint32_t seed);

// --- source-fidelity verification (e2e helper) -----------------------------------
// Byte-compare a re-serialization of the captured world against the source city
// stream, section by section, at the SOURCE file's version (re-emitting through
// version-gated mirrors of the reconstructed loaders). The shipped city seeds
// are version 0x1003B (BERLIN: 0x1003E), so two sections are EXPECTED to be
// skipped there and are reported as such, never faked:
//   * the header's leading 4-byte version word (the original writer always
//     stamps 0x10045 — kSaveVersionWriter; the rest of the header IS compared),
//   * the object table (its on-disk field set changed at 0x10032 — a discard
//     dword disappeared — and 0x10043 — the +153 lightmap block appeared; the
//     reconstructed writer, like the original @0x5a4134, emits only the current
//     field set). Compared only when the source version >= 0x10043.
// TRAILING DATA: the shipped .cty seeds carry a large block AFTER the
// building-slot tables (~0.5..0.6 MB; leading bytes "..l:MegaCam" + float
// vectors): the WorldIo scene-object dump that VIBE_Save_RelinkPersonExtraData
// @0x5a3f14 appends via VIBE_WorldIo_SaveSceneObjects @0x5e65b8. The shipping
// loader's PARTIAL path (VIBE_Save_LoadGameFile @0x5a7604) returns without
// reading it — it belongs to the scene/universe module (its READ side is
// deferred there), so it is reported via `trailingBytes` and not reproduced.
struct SectionCompareResult {
    bool loaded = false;       // the source parsed end-to-end
    guild::u32 version = 0;    // source version
    // per-section byte equality (see header comment for the two expected skips)
    bool headerTail = false;   // header bytes [4 .. end-of-header)
    bool scalars = false;      // scalar block (version-gated re-emit)
    bool tiles = false;        // map-tile index table
    bool objects = false;      // object table (only meaningful if objectsCompared)
    bool objectsCompared = false;
    bool counters = false;     // building/counter table
    bool persons = false;      // person/scene preamble + records
    // At source versions < 0x1003E the loader stamps *(rec+400) = 4 AFTER the
    // read (io::LoadCityRecords @0x5a8d3c), destroying the on-disk dword — the
    // real engine itself cannot reproduce it on a resave. The persons compare
    // masks exactly those 4 bytes per record; the count is reported here.
    std::size_t personsMaskedDwords = 0;
    bool slots = false;        // building-slot tables + city-info
    std::size_t trailingBytes = 0; // unparsed tail (the scene-object sidecar)
    std::size_t sourceBytes = 0;   // gunzipped source stream length
    bool ok() const {
        return loaded && headerTail && scalars && tiles && counters && persons
            && slots && (!objectsCompared || objects);
    }
};
SectionCompareResult CompareSaveSectionsAgainstCity(guild::shim::IFileSystem& fs,
                                                    const char* ctyPath);

} // namespace guild::play
