// guild::play — REAL save/load for the play session. See session_save.h for the
// recovered original flow (addresses) and the scope notes.
//
// REUSED (extern, never redefined — ODR):
//   io::SaveLoadHeaderAndThumbnail / SaveLoadScalarBlock / SaveVersion*  (io/save)
//   io::LoadPersonIndexTable / SaveLoadPersonTable / LoadGlobalCounters /
//     LoadCityRecords / LoadBuildingSlotTables                (io/save_world_load)
//   io::SaveWriteGameFilePartial / SaveWriteGameStateSection /
//     AmtLoadAemter                                           (io/save_world_write)
//   io::SaveWriteScenarioBlock / SaveWriteMapTileTable /
//     SaveWriteBuildingTable / SaveWriteBuildingSlotTables /
//     SaveWritePersonTable                                    (io serializers)
//   io::SaveBrowserEnumerateSaveFiles / kSaveExt              (io/save_browser)
//   io::VfsInit / VfsOpenFile / VfsOpenMemoryStream / Vfs*    (io/vfs)
//   io::VfsTreeInit / VfsRoot / VfsTreeShutdown               (io/vfs_tree)
//   play::RoundTripZeroWorld                                  (play/save_roundtrip)
//   play::HashFullWorld                                       (play/world_digest)
//   sim::g_persons / g_personIds / g_objects / g_sysGameTime  (sim)
//   world::g_officeHolders                                    (world/office)
//   crt::Srand                                                (crt/rand)
#include "play/session_save.h"

#include <cstdio>
#include <cstring>
#include <functional>

#include "crt/rand.h"
#include "io/save.h"
#include "io/save_browser.h"
#include "io/save_building.h"
#include "io/save_person.h"
#include "io/save_serial3.h"
#include "io/save_tables.h"
#include "io/save_world_load.h"
#include "io/save_world_write.h"
#include "io/vfs.h"
#include "io/vfs_tree.h"
#include "play/save_roundtrip.h"
#include "play/world_digest.h"
#include "shim/IFileSystem.h"
#include "sim/command_apply5.h"   // g_sysGameTime == qword_13CE852
#include "sim/entity.h"
#include "world/office.h"         // g_officeHolders == byte_B59848

namespace guild::play {

// --- the original's save-dir layout (recovered strings) ---------------------
const char kSaveBrowseDir[]   = "gamedata/saves";                        // @0x624f30
const char kSaveSlotPathFmt[] = "Gamedata/saves/GILDE_SAVEGAME_%i.SAV";  // @0x624f6c
const char kQuickSaveName[]   = "QUICKSAVE";                             // @0x624ef0
const char kAutoSaveName[]    = "AUTOSAVE";                              // @0x624efc

std::string SaveSlotPath(int slot) {
    char buf[96];
    // the original's "Gamedata/saves/GILDE_SAVEGAME_%i.SAV" name, under the
    // real-cased install dir (the VFS root maps to Resources/).
    std::snprintf(buf, sizeof buf,
                  "Resources/gamedata/Saves/GILDE_SAVEGAME_%i.SAV", slot);
    return buf;
}
std::string QuickSavePath() {        // "Gamedata\Saves\Quicksave.SAV" @0x6252b8
    return "Resources/gamedata/Saves/Quicksave.SAV";
}
std::string AutoSavePath() {         // "Saves\Autosave.SAV" @0x623551
    return "Resources/gamedata/Saves/Autosave.SAV";
}

namespace {

// ---------------------------------------------------------------------------
// The session capture: every side table the partial stream carries, retained so
// SaveLiveWorld writes the REAL bytes back (io::LoadWorld discards most of them
// into function-local scratch). One capture per process — the play session has
// exactly one live world (the original's globals are equally singular).
// ---------------------------------------------------------------------------
struct SessionCapture {
    bool                        valid;
    io::SaveHeader              hdr;                       // header @0x5a7af0
    guild::u8                   thumb[io::kThumbnailBytes];// 0xE100 thumbnail
    io::SaveScalarBlock         scalars;                   // @0x5a76d6..
    guild::u8                   tiles[io::kSceneTileScanBytes]; // dword_13CE290
    guild::u32                  tileCount;                 // dword_6498C0
    guild::u8                   counters[io::kBuildCounterStride
                                         * io::kBuildCounterCount]; // word_13C3110
    guild::u8                   extra96[io::kExtra96Stride * 256];  // dword_1234600
    guild::u32                  extra96Count;                       // dword_1234604
    io::GameStateHeaderPreamble preamble;                  // word_63CC5C/dword_6498E8..
    guild::u8                   slots[io::kBst_CitySlotTableStride
                                      * io::kBst_CitySlotTableCount]; // dword_13C3B50
    guild::u8                   cityInfo[io::kBst_CityInfoStride
                                         * io::kBst_CityInfoRecCount]; // byte_13CD6A0
};
SessionCapture g_cap;

// Per-record plantmap pool (the original heap-allocates one 0x600 buffer per
// kind-30 object on load — VIBE_Save_LoadPersonTable @0x5a8190). A fixed pool
// keyed by load order keeps each record's plant bytes addressable for a later
// save AND keeps the +113 pointer column reproducible within a process run.
guild::u8  g_plantPool[guild::sim::kObjectCapacity][io::kPlantBytes];
guild::u32 g_plantNext = 0;

guild::u8* PlantAlloc(void* /*ctx*/) {
    guild::u32 i = g_plantNext < (guild::u32)guild::sim::kObjectCapacity
                       ? g_plantNext++ : (guild::u32)guild::sim::kObjectCapacity - 1;
    return g_plantPool[i];
}

inline guild::u8* ObjBase()  { return reinterpret_cast<guild::u8*>(&sim::g_objects[0]); }
inline guild::u8* PersBase() { return reinterpret_cast<guild::u8*>(&sim::g_persons[0]); }

// ---------------------------------------------------------------------------
// Section offsets of one parsed stream (VfsTell after each table) — used by the
// source-fidelity verifier.
// ---------------------------------------------------------------------------
struct SectionOffsets {
    long afterHeader   = -1;
    long afterScalars  = -1;
    long afterTiles    = -1;
    long afterObjects  = -1;
    long afterCounters = -1;
    long afterPersons  = -1;
    long afterSlots    = -1;
    long afterAemter   = -1;
};

// ---------------------------------------------------------------------------
// The load driver — mirrors VIBE_Save_LoadGameFile @0x5a7604 (partial prefix),
// capturing every side table into g_cap and populating the live sim arrays.
// The caller has already blanked the world (RoundTripZeroWorld).
// ---------------------------------------------------------------------------
bool ParsePartialStream(io::VfsHandle* h, SessionLoadInfo& info,
                        SectionOffsets* offs) {
    std::memset(&g_cap, 0, sizeof g_cap);
    g_plantNext = 0;

    // 1. header + thumbnail (sets the version word from the file).
    if (!io::SaveLoadHeaderAndThumbnail(h, g_cap.hdr, g_cap.thumb))
        return false;
    info.version = io::SaveVersionGet();
    if (info.version > io::kSaveVersionLoadMax
        || info.version < io::kSaveVersionLoadMin)     // @0x5a775a gate
        return false;
    if (offs) offs->afterHeader = io::VfsTell(h);

    // 2. scalar block (version-gated). The live game clock (qword_13CE852 ==
    //    sim::g_sysGameTime) is restored from it, mirroring the original's
    //    in-place read into the global.
    if (!io::SaveLoadScalarBlock(h, g_cap.scalars))
        return false;
    static_assert(sizeof(sim::g_sysGameTime) == 14, "GameTime is qword_13CE852[14]");
    std::memcpy(&sim::g_sysGameTime, g_cap.scalars.gameTime, 14);
    if (offs) offs->afterScalars = io::VfsTell(h);

    // 3. map-tile / scene-node index table (dword_13CE290).
    if (!io::LoadPersonIndexTable(h, g_cap.tiles, &g_cap.tileCount))
        return false;
    info.tileCount = g_cap.tileCount;
    if (offs) offs->afterTiles = io::VfsTell(h);

    // 4. object / building array + trailing extra-96 table.
    if (!io::SaveLoadPersonTable(h, ObjBase(), info.version, &PlantAlloc, nullptr,
                                 g_cap.extra96, &g_cap.extra96Count))
        return false;
    info.objectCount = 0;
    for (guild::u32 o = 0; o != io::kObjScanBytes; o += io::kObjStride)
        if (ObjBase()[o])
            ++info.objectCount;
    if (offs) offs->afterObjects = io::VfsTell(h);

    // 5. building / counter table (word_13C3110, 16 x 164).
    if (!io::LoadGlobalCounters(h, g_cap.counters, info.version))
        return false;
    if (offs) offs->afterCounters = io::VfsTell(h);

    // 6. person / scene records (word_12CE910) — populates g_persons.
    if (!io::LoadCityRecords(h, PersBase(), info.version,
                             &g_cap.preamble.marker63CC5C, &info.personCount,
                             &g_cap.preamble.id6498E8, &g_cap.preamble.id6498EC,
                             g_cap.preamble.handlerIds))
        return false;
    for (int i = 0; i < sim::kPersonCapacity; ++i)
        if (sim::g_persons[i].marker != (guild::i16)-1)
            sim::g_personIds[i] = sim::g_persons[i].id;
    sim::g_personArrayLoaded = true;
    if (offs) offs->afterPersons = io::VfsTell(h);

    // 7. building-slot tables (5 city-slot tables + 4 city-info records).
    if (!io::LoadBuildingSlotTables(h, g_cap.slots, g_cap.cityInfo, info.version))
        return false;
    if (offs) offs->afterSlots = io::VfsTell(h);

    // 8. Amt office-holder table — read right after the slot tables when the
    //    file version >= 0x10045 (@0x5a7873 chain: `dword_649D4C >= 0x10045 &&
    //    !VIBE_Amt_LoadAemter(v5)`); older files carry it in the full tail only.
    if (info.version >= io::kVerAmtAemter) {
        if (!io::AmtLoadAemter(h, reinterpret_cast<guild::u8*>(world::g_officeHolders)))
            return false;
        info.aemterLoaded = true;
    }
    if (offs) offs->afterAemter = io::VfsTell(h);

    // 9. VIBE_Save_PostLoadInitScene @0x5a7ef8 — render/scene refresh (live-state
    //    only); deferred with the universe/render modules (as in io::LoadWorld).
    // 10. partial gate (byte_13CEC94 & 2).
    info.partial = (g_cap.hdr.flagByte & 2) != 0;
    g_cap.valid = true;
    return true;
}

// ---------------------------------------------------------------------------
// Version-gated scalar-block emitter — the exact inverse of SaveLoadScalarBlock
// (@0x5a76d6.. read gates). ONLY the source-fidelity verifier uses this: the
// original writer (@0x5a3501..) is ungated because it always runs at 0x10045;
// re-emitting an OLDER stream for the byte-compare needs the loader's gates.
// ---------------------------------------------------------------------------
bool EmitScalarBlockGated(io::VfsHandle* h, const io::SaveScalarBlock& b,
                          guild::u32 version) {
    auto WR = [&](const void* p, guild::u32 n) {
        return io::VfsWriteStream(p, n, h, 1) == n;
    };
    if (!WR(&b.g649890, 4) || !WR(&b.g632244, 4) || !WR(b.gameTime, 14)
        || !WR(&b.season, 1) || !WR(&b.g6498E4, 4) || !WR(&b.g64771C, 4)
        || !WR(&b.g647720, 4) || !WR(&b.g647724, 4))
        return false;
    if (version >= io::kVerOptUnk6477A8 && !WR(&b.unk6477A8, 4))  // >=0x10022
        return false;
    if (!WR(b.gB56450, 24))
        return false;
    if (version >= io::kVerGlobal649894 && !WR(&b.g649894, 4))    // >=0x10030
        return false;
    if (version >= io::kVerGlobal632240 && !WR(&b.g632240, 4))    // >=0x1003D
        return false;
    return true;
}

// ---------------------------------------------------------------------------
// Version-gated person/scene-record emitter — the exact inverse of
// io::LoadCityRecords (@0x5a8d3c read gates). ONLY the source-fidelity verifier
// uses this (the original writer @0x5a4938 is ungated — it always runs at
// 0x10045, where this emitter degenerates to the same field set). The counter
// biases are subtracted as on the wire; the link slots hold the on-disk ids.
// NOTE (information destroyed by the loader, not recoverable): at version
// < 0x1003E the loader stamps *(rec+400) = 4 after reading the on-disk dword,
// so this emitter writes the post-load value (4). The shipped seeds carry 4
// there (verified by the section byte-compare passing).
// ---------------------------------------------------------------------------
bool EmitCityRecordGated(io::VfsHandle* h, const guild::u8* r, guild::u32 v,
                         std::vector<long>* maskOut) {
    auto WR = [&](const void* p, guild::u32 n) {
        return io::VfsWriteStream(p, n, h, 1) == n;
    };
    auto WRf = [&](int off, guild::u32 n) { return WR(r + off, n); };
    if (!WRf(0, 2))                                  // leading marker word
        return false;
    if (v >= 0x1003E && !WRf(520, 4))                // else loader stamps -1
        return false;
    if (!WRf(2, 1) || !WRf(4, 4) || !WRf(8, 1) || !WRf(9, 1) || !WRf(10, 2)
        || !WRf(12, 1) || !WRf(13, 1) || !WRf(16, 4) || !WRf(20, 4) || !WRf(24, 4)
        || !WRf(28, 4) || !WRf(32, 4) || !WRf(36, 4))
        return false;
    if (v >= 0x1003B && (!WRf(40, 2) || !WRf(44, 4)))
        return false;
    if (!WRf(48, 0x10))
        return false;
    if (v >= 0x10031 && !WRf(64, 0x10))
        return false;
    if (!WRf(80, 2))
        return false;
    guild::i32 cA;                                   // -1342 on the wire
    std::memcpy(&cA, r + 84, 4);
    cA -= io::kCounterBiasA;
    if (!WR(&cA, 4))
        return false;
    if (!WRf(88, 1) || !WRf(92, 0x20))
        return false;
    if (v >= 0x10024 && !WRf(124, 4))
        return false;
    if (!WRf(128, 5) || !WRf(136, 0xA8))
        return false;
    if (!WRf(356, 1) || !WRf(357, 1) || !WRf(358, 1) || !WRf(359, 1)
        || !WRf(360, 1) || !WRf(361, 1))
        return false;
    if (!WRf(364, 4) || !WRf(368, 4))                // link id slots
        return false;
    if (v < 0x10020) {                               // +372: word then, dword now
        if (!WRf(372, 2))
            return false;
    } else if (!WRf(372, 4)) {
        return false;
    }
    if (!WRf(380, 4) || !WRf(384, 1))
        return false;
    guild::i32 cB;                                   // -1468 on the wire
    std::memcpy(&cB, r + 396, 4);
    cB -= io::kCounterBiasB;
    if (!WR(&cB, 4))
        return false;
    if (v < 0x1003E && maskOut)                       // the loader stamped 4 here;
        maskOut->push_back(io::VfsTell(h));           // the on-disk value is gone
    if (!WRf(400, 4))                                // post-load value (see note)
        return false;
    if (!WRf(404, 4) || !WRf(408, 4) || !WRf(412, 4) || !WRf(416, 4)
        || !WRf(420, 4) || !WRf(424, 4) || !WRf(428, 4) || !WRf(432, 1)
        || !WRf(433, 1) || !WRf(436, 0x10) || !WRf(456, 4) || !WRf(460, 4)
        || !WRf(464, 0x10) || !WRf(480, 4) || !WRf(484, 4) || !WRf(488, 4))
        return false;
    if (v >= 0x1002A && !WRf(492, 4))
        return false;
    if (v >= 0x10021 && !WRf(453, 1))
        return false;
    if (!WRf(388, 4))                                // link id slot (+97 deref)
        return false;
    if (v >= 0x10021 && !WRf(496, 0x18))
        return false;
    if (v >= 0x10036) {
        if (!WRf(524, 4) || !WRf(528, 1) || !WRf(529, 1) || !WRf(530, 1)
            || !WRf(531, 1) || !WRf(532, 1))
            return false;
    }
    return true;
}

// Gated mirror of the whole gamestate section (preamble + live records); the
// preamble's 8 handler ids exist on the wire only at >= 0x10017 (the loader's
// gate; every loadable version >= 0x10026 carries them).
bool EmitGameStateSectionGated(io::VfsHandle* h,
                               const io::GameStateHeaderPreamble& p,
                               const guild::u8* personBase, guild::u32 v,
                               std::vector<long>* maskOut) {
    auto WR = [&](const void* q, guild::u32 n) {
        return io::VfsWriteStream(q, n, h, 1) == n;
    };
    guild::u32 live = 0;
    for (guild::u32 o = 0; o != io::kPersScanBytes; o += io::kPersStride) {
        guild::i16 marker;
        std::memcpy(&marker, personBase + o, 2);
        if (marker != (guild::i16)-1)
            ++live;
    }
    if (!WR(&p.marker63CC5C, 2) || !WR(&live, 4)
        || !WR(&p.id6498E8, 4) || !WR(&p.id6498EC, 4))
        return false;
    if (v >= io::kVerHandlerIds) {                   // >=0x10017
        for (int i = 0; i < 8; ++i)
            if (!WR(&p.handlerIds[i], 4))
                return false;
    }
    for (guild::u32 o = 0; o != io::kPersScanBytes; o += io::kPersStride) {
        const guild::u8* rec = personBase + o;
        guild::i16 marker;
        std::memcpy(&marker, rec, 2);
        if (marker == (guild::i16)-1)
            continue;
        if (!EmitCityRecordGated(h, rec, v, maskOut))
            return false;
    }
    return true;
}

} // namespace

// ===========================================================================
// LoadLiveWorld
// ===========================================================================
SessionLoadInfo LoadLiveWorld(guild::shim::IFileSystem& fs, const char* path,
                              std::uint32_t seed) {
    SessionLoadInfo info;
    if (!path)
        return info;
    io::VfsInit(&fs, false);

    // World reset (Building_ResetAllBuildings / CharAction_QueueFreeAll /
    // Object_DestroySpawnedEntities @0x5a7627.. — modeled as the play-layer
    // determinism rig: zero every folded table + ResetEntityArrays + Srand).
    RoundTripZeroWorld(seed);

    io::VfsHandle* h = io::VfsOpenFile(path, "rb");
    if (!h)
        return info;
    bool parsed = ParsePartialStream(h, info, nullptr);
    io::VfsCloseStream(h);

    info.ok = parsed && info.partial;
    if (parsed && !info.partial) {
        // A FULL (.SAV) file: its tail (Gesetz/MapTiles/GameGlobals/Avatar/
        // Object/Amt/History/ActionQueues/Hotkeys/Characters/Mission) is not
        // reconstructed — refuse rather than half-load (rule 8), and blank the
        // partially-populated world.
        RoundTripZeroWorld(seed);
        g_cap.valid = false;
    }
    if (!parsed)
        g_cap.valid = false;
    return info;
}

// ===========================================================================
// SaveLiveWorld
// ===========================================================================
bool SaveLiveWorld(guild::shim::IFileSystem& fs, const char* path,
                   const char* saveName, guild::u8 slotTag) {
    if (!path)
        return false;
    io::VfsInit(&fs, false);

    io::PartialSaveEnv env{};

    // Header: the load-time capture, re-labeled. The original copies the save
    // name into the live name buffer (byte_122F198 / word_13CED78) and stamps
    // byte_649D50 with the slot tag before WriteGameFile (@0x56aae6/@0x56aaf9);
    // flags=2 selects the partial path and is written into header +4.
    io::SaveHeader hdr = g_cap.hdr;
    hdr.flagByte = 2;
    std::memset(hdr.name, 0, sizeof hdr.name);
    std::memset(hdr.name96, 0, sizeof hdr.name96);
    if (saveName) {
        std::strncpy(hdr.name, saveName, sizeof hdr.name - 1);
        std::strncpy(hdr.name96, saveName, sizeof hdr.name96 - 1);
    }
    hdr.byte649D50 = slotTag;
    env.header    = hdr;
    env.thumbnail = g_cap.thumb;    // captured thumbnail (zeros if none; the live
                                    // writer regrabs the framebuffer — out of scope)

    // Scalar block: the capture, with the LIVE game clock (the original writes
    // the live qword_13CE852 == sim::g_sysGameTime).
    env.scalars = g_cap.scalars;
    std::memcpy(env.scalars.gameTime, &sim::g_sysGameTime, 14);

    env.tileBase     = g_cap.tiles;
    env.objBase      = ObjBase();
    env.extra96Base  = g_cap.extra96;
    env.extra96Count = g_cap.extra96Count;
    env.counterBase  = g_cap.counters;
    env.preamble     = g_cap.preamble;
    env.personBase   = PersBase();
    env.slotBase     = g_cap.slots;
    env.cityInfoBase = g_cap.cityInfo;
    env.officeBase   = reinterpret_cast<const guild::u8*>(world::g_officeHolders);

    // VIBE_Vfs_OpenFile(path, "wb") @0x5a34a3.
    io::VfsHandle* h = io::VfsOpenFile(path, "wb");
    if (!h)
        return false;
    bool ok = io::SaveWriteGameFilePartial(h, env);
    io::VfsCloseStream(h);
    return ok;
}

// ===========================================================================
// EnumerateSaves
// ===========================================================================
int EnumerateSaves(guild::shim::IFileSystem& fs, std::vector<SaveListEntry>& out,
                   const char* treeRoot, const char* openDirReal) {
    out.clear();

    // Build the directory tree the browser scans (the original's VFS tree over
    // the Resources root; case-insensitive like the Windows original).
    if (!io::VfsTreeInit(&fs, treeRoot, /*caseInsensitive=*/true))
        return 0;

    // The original caller holds a fixed record buffer (a 16896-byte stack block
    // in VIBE_Menu_RunSaveGame == 32 records); the enumerator itself is
    // unbounded. 256 records cover any sane save dir.
    static io::SaveBrowserRecord recs[256];
    std::memset(recs, 0, sizeof recs);
    int n = io::SaveBrowserEnumerateSaveFiles(kSaveBrowseDir, io::VfsRoot(),
                                              io::kSaveExt, recs, 256);
    io::VfsTreeShutdown();
    if (n < 0)
        n = 0;
    if (n > 256)
        n = 256;

    // Per-file header metadata (the load-bearing part of LoadSlotMetadata
    // @0x569d00: it reads each save's header to get the in-game name/slot).
    io::VfsInit(&fs, false);
    guild::u32 savedVersion = io::SaveVersionGet();
    for (int i = 0; i < n; ++i) {
        SaveListEntry e;
        // The original opens record+265 (the enumerator's full "basePath/name" with
        // extension), not the +9 name field which EnumerateSaveFiles @0x569530
        // truncates at its '.' (@0x5695f6). Derive the loose file name (with its
        // extension) from the +265 full path's basename.
        const char* fp = recs[i].fullPath;
        const char* slash = std::strrchr(fp, '/');
        e.fileName   = slash ? slash + 1 : fp;
        e.browsePath = recs[i].fullPath;
        e.openPath   = std::string(openDirReal) + "/" + e.fileName;
        if (io::VfsHandle* h = io::VfsOpenFile(e.openPath.c_str(), "rb")) {
            io::SaveHeader hdr{};
            if (io::SaveLoadHeaderAndThumbnail(h, hdr, nullptr)) {
                e.headerOk = true;
                char nameZ[33];
                std::memcpy(nameZ, hdr.name, 32);
                nameZ[32] = 0;
                e.saveName = nameZ;
                e.version  = hdr.magic;
                e.slotTag  = hdr.byte649D50;
                // VIBE_SaveBrowser_FindSaveSlot @0x569c50 reserved-name rule.
                e.reservedSlot0 = (e.saveName == kQuickSaveName)
                               || (e.saveName == kAutoSaveName);
            }
            io::VfsCloseStream(h);
        }
        out.push_back(e);
    }
    io::SaveVersionSet(savedVersion);
    return n;
}

// ===========================================================================
// Last-load capture accessors (wave-3, ADDITIVE — see session_save.h).
// ===========================================================================
std::string SessionLoadedCityName() {
    if (!g_cap.valid)
        return std::string();
    // SaveHeader +0x05 name[32] is NUL-padded by the writer; force-terminate a
    // pathological 32-non-NUL-byte name (defensive copy only — no byte changes
    // for well-formed files).
    char nameZ[33];
    std::memcpy(nameZ, g_cap.hdr.name, 32);
    nameZ[32] = '\0';
    return std::string(nameZ);
}

guild::u8 SessionLoadedRateByte(bool* ok) {
    if (ok)
        *ok = g_cap.valid;
    return g_cap.valid ? g_cap.scalars.season : 0;
}

// ===========================================================================
// SessionWorldHash
// ===========================================================================
std::uint64_t SessionWorldHash(std::uint32_t seed) {
    crt::Srand(seed);          // the base digest folds the live CRT RNG state
    return HashFullWorld();
}

// ===========================================================================
// CompareSaveSectionsAgainstCity — section-by-section byte fidelity against the
// source stream. NOTE: loads the city into the live world as a side effect
// (it drives the same capture-load the session uses).
// ===========================================================================
SectionCompareResult CompareSaveSectionsAgainstCity(guild::shim::IFileSystem& fs,
                                                    const char* ctyPath) {
    SectionCompareResult r;
    if (!ctyPath)
        return r;
    io::VfsInit(&fs, false);

    // Slurp the (transparently gunzipped) source stream.
    std::vector<guild::u8> src;
    {
        io::VfsHandle* h = io::VfsOpenFile(ctyPath, "rb");
        if (!h)
            return r;
        if (io::VfsSeek(h, 0, 2 /*SEEK_END*/) != 0) { io::VfsCloseStream(h); return r; }
        long len = io::VfsTell(h);
        if (len <= 0) { io::VfsCloseStream(h); return r; }
        io::VfsSeek(h, 0, 0 /*SEEK_SET*/);
        src.resize((std::size_t)len);
        bool got = io::VfsReadStream(src.data(), 1, h, (guild::u32)len)
                   == (guild::u32)len;
        io::VfsCloseStream(h);
        if (!got)
            return r;
    }
    r.sourceBytes = src.size();

    // Capture-load it from a memory stream, recording the section spans.
    RoundTripZeroWorld(0x4711);
    SessionLoadInfo info;
    SectionOffsets offs;
    long endPos = -1;
    {
        io::VfsHandle* mh = io::VfsOpenMemoryStream(src.data(),
                                                    (guild::u32)src.size(), "rb");
        if (!mh)
            return r;
        r.loaded = ParsePartialStream(mh, info, &offs) && info.partial;
        endPos = io::VfsTell(mh);
        io::VfsCloseStream(mh);
    }
    r.version = info.version;
    if (!r.loaded)
        return r;
    r.trailingBytes = (endPos >= 0 && (std::size_t)endPos <= src.size())
                          ? src.size() - (std::size_t)endPos : 0;

    // Re-emit each section at the SOURCE version and byte-compare its span.
    std::vector<guild::u8> scratch(4u * 1024u * 1024u);
    auto emitAndCompare = [&](long secStart, long secEnd, long skipLead,
                              const std::function<bool(io::VfsHandle*)>& emit) -> bool {
        if (secStart < 0 || secEnd < secStart || (std::size_t)secEnd > src.size())
            return false;
        io::VfsHandle* w = io::VfsOpenMemoryStream(scratch.data(),
                                                   (guild::u32)scratch.size(), "wb");
        if (!w)
            return false;
        bool wrote = emit(w);
        long len = io::VfsTell(w);
        io::VfsCloseStream(w);
        if (!wrote || len != secEnd - secStart)
            return false;
        return std::memcmp(scratch.data() + skipLead,
                           src.data() + secStart + skipLead,
                           (std::size_t)(len - skipLead)) == 0;
    };

    // Header tail: SaveWriteScenarioBlock stamps version 0x10045 (the original
    // writer's fixed magic — kSaveVersionWriter), so the 4-byte version word is
    // skipped; every other header field exists at all loadable versions >=
    // 0x10039 (the shipped cities are 0x1003B). Restore the source version word
    // afterwards (the header writer overwrote it).
    r.headerTail = (info.version >= io::kVerName136)
                && emitAndCompare(0, offs.afterHeader, 4, [](io::VfsHandle* w) {
                       return io::SaveWriteScenarioBlock(w, g_cap.hdr, g_cap.thumb);
                   });
    io::SaveVersionSet(info.version);

    // Scalar block (gated re-emit — the loader's exact inverse).
    r.scalars = emitAndCompare(offs.afterHeader, offs.afterScalars, 0,
                               [&](io::VfsHandle* w) {
                                   return EmitScalarBlockGated(w, g_cap.scalars,
                                                               info.version);
                               });

    // Map-tile index table @0x5a3fa4 (no version gates).
    r.tiles = emitAndCompare(offs.afterScalars, offs.afterTiles, 0,
                             [](io::VfsHandle* w) {
                                 return io::SaveWriteMapTileTable(w, g_cap.tiles);
                             });

    // Object table @0x5a4134: the on-disk field set changed at 0x10032 (discard
    // dword removed) and 0x10043 (+153 lightmap added); the writer — like the
    // original — emits only the current field set, so the byte-compare is only
    // meaningful for sources >= 0x10043. Named skip below that (the shipped
    // cities are 0x1003B); the save->reload round trip covers the object table
    // at 0x10045.
    if (info.version >= 0x10043) {
        r.objectsCompared = true;
        r.objects = emitAndCompare(offs.afterTiles, offs.afterObjects, 0,
                                   [](io::VfsHandle* w) {
                                       return io::SaveWritePersonTable(
                                           w, ObjBase(), g_cap.extra96,
                                           g_cap.extra96Count);
                                   });
    }

    // Building / counter table @0x5a45bc (gated at the source version).
    r.counters = emitAndCompare(offs.afterObjects, offs.afterCounters, 0,
                                [&](io::VfsHandle* w) {
                                    return io::SaveWriteBuildingTable(
                                        w, g_cap.counters, nullptr, info.version);
                                });

    // Person / scene records: the loader's gates re-applied (the 0x10045 writer
    // @0x5a4938 is ungated; EmitGameStateSectionGated degenerates to it there).
    // At source versions < 0x1003E the loader stamps *(rec+400) = 4 AFTER reading
    // the on-disk dword (io::LoadCityRecords @0x5a8d3c), destroying it — the real
    // engine itself cannot reproduce that dword on a resave, so the compare masks
    // exactly those 4 bytes per record and reports the count.
    {
        long secStart = offs.afterCounters, secEnd = offs.afterPersons;
        std::vector<long> mask;
        io::VfsHandle* w = io::VfsOpenMemoryStream(scratch.data(),
                                                   (guild::u32)scratch.size(), "wb");
        bool wrote = w && EmitGameStateSectionGated(w, g_cap.preamble, PersBase(),
                                                    info.version, &mask);
        long len = w ? io::VfsTell(w) : -1;
        if (w)
            io::VfsCloseStream(w);
        if (wrote && secStart >= 0 && len == secEnd - secStart
            && (std::size_t)secEnd <= src.size()) {
            bool eq = true;
            std::size_t m = 0;
            for (long i = 0; i < len && eq; ++i) {
                if (m < mask.size() && i == mask[m]) {     // skip the masked dword
                    i += 3;
                    ++m;
                    continue;
                }
                eq = scratch[(std::size_t)i] == src[(std::size_t)(secStart + i)];
            }
            r.persons = eq;
            r.personsMaskedDwords = mask.size();
        }
    }

    // Building-slot tables @0x5a5c1c (gated +748 tail at the source version).
    r.slots = emitAndCompare(offs.afterPersons, offs.afterSlots, 0,
                             [&](io::VfsHandle* w) {
                                 return io::SaveWriteBuildingSlotTables(
                                     w, g_cap.slots, g_cap.cityInfo, info.version);
                             });

    return r;
}

} // namespace guild::play
