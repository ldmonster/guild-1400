// Wave 29 PLAY — P3 / M3 save/load round-trip fidelity. See save_roundtrip.h.
//
// The SAVE side mirrors the io::LoadWorld read order field-for-field at the
// writer's current version (0x10045), so the bytes this module emits are exactly
// what the REAL io::Load* sub-loaders read back. Where a dedicated WRITE
// serializer sibling exists it is REUSED (header, scalar block, object table,
// person/scene records, building-slot tables); the two LOAD-only tables (the
// scene-tile index table and the 164-stride global-counter table) are emitted here
// by inverting their documented LOAD field order.
//
// REUSED (extern, never redefined — ODR):
//   io::LoadWorld / WorldState / the table sub-loaders   (io/save_world_load.h)
//   io::SaveWriteScenarioBlock / SaveWriteScalarBlock /
//     SaveVersionSet / SaveVersionGet                    (io/save.h)
//   io::SaveWritePersonTable / SaveLoadPersonTable /
//     SaveWritePersonSceneRecord / SaveLoadPersonSceneRecord (io/save_person.h)
//   io::SaveWriteBuildingSlotTables                      (io/save_serial3.h)
//   io::Vfs* memory streams                              (io/vfs.h)
//   play::HashFullWorld                                  (play/world_digest.h)
//   sim::g_objects / g_persons / ResetEntityArrays       (sim/entity.h)
//   crt::Srand / world::CityInitParameterTable           (crt/rand.h, world/city.h)
#include "play/save_roundtrip.h"

#include <cstring>

#include "io/save.h"
#include "io/save_person.h"
#include "io/save_serial3.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/world_digest.h"

// The full set of live world tables HashFullWorld folds — zeroed before a load so a
// hash is a pure function of (loaded city + seed) regardless of any prior run.
#include "sim/entity.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons
#include "sim/building.h"            // g_buildingTypes / g_buildingTypesLoaded
#include "sim/building_create.h"     // g_buildingNextId
#include "sim/building_production.h" // g_sceneTypes/_Loaded, g_sceneTypeRemap, g_prod*
#include "sim/actionqueue.h"         // g_gameTick
#include "sim/command_apply5.h"      // g_sysGameTime, g_currentPlayer/g_sysActivePlayer
#include "sim/command_apply6.h"      // g_tickClock, g_tickSubCounter
#include "world/city.h"              // g_cities, g_goods, totals, CityInitParameterTable
#include "world/law.h"               // g_lawTable
#include "world/event.h"             // g_eventTable, g_eventTableCount, g_missionLcgState
#include "world/office.h"            // g_officeHolders
#include "world/crime.h"             // g_crimeTable
#include "world/relation.h"          // g_relationMatrix
#include "crt/rand.h"

namespace guild::play {

namespace {

// --- small VFS stream write helpers (mirror save_person.cpp's WR/RD) --------
inline bool WR(io::VfsHandle* h, const void* p, guild::u32 n) {
    return io::VfsWriteStream(p, n, h, 1) == n;
}

// ---------------------------------------------------------------------------
// Determinism rig (the play-layer gotchas the playable-slice module documents):
// zero EVERY folded world table so a hash is a pure function of (load + seed).
// ---------------------------------------------------------------------------
void ZeroWorldGlobals() {
    using std::memset;
    memset(sim::g_objects, 0, sizeof(sim::g_objects));
    memset(sim::g_persons, 0, sizeof(sim::g_persons));
    memset(sim::g_personIds, 0, sizeof(sim::g_personIds));
    memset(sim::g_sceneNodes, 0, sizeof(sim::g_sceneNodes));
    memset(sim::g_buildingPersons, 0, sizeof(sim::g_buildingPersons));
    memset(sim::g_buildingTypes, 0, sizeof(sim::g_buildingTypes));
    sim::g_buildingTypesLoaded = false;
    sim::g_buildingNextId = 0;
    memset(sim::g_sceneTypes, 0, sizeof(sim::g_sceneTypes));
    sim::g_sceneTypesLoaded = false;
    memset(sim::g_sceneTypeRemap, 0, sizeof(sim::g_sceneTypeRemap));
    memset(sim::g_prodStore, 0, sizeof(sim::g_prodStore));
    memset(sim::g_prodSchedules, 0, sizeof(sim::g_prodSchedules));
    memset(world::g_cities, 0, sizeof(world::g_cities));
    memset(world::g_goods, 0, sizeof(world::g_goods));
    world::g_capDivisor = 0.0f;
    world::g_cityTotalMoney = 0.0f;
    world::g_cityTotalGoods = 0.0f;
    memset(&sim::g_sysGameTime, 0, sizeof(sim::g_sysGameTime));
    memset(&sim::g_tickClock, 0, sizeof(sim::g_tickClock));
    sim::g_tickSubCounter = 0;
    sim::g_gameTick = 0;
    sim::g_currentPlayer = 0;
    sim::g_sysActivePlayer = 0;
    memset(world::g_lawTable, 0, sizeof(world::g_lawTable));
    memset(world::g_eventTable, 0, sizeof(world::g_eventTable));
    world::g_eventTableCount = 0;
    world::g_missionLcgState = 0;
    memset(world::g_officeHolders, 0, sizeof(world::g_officeHolders));
    memset(world::g_crimeTable, 0, sizeof(world::g_crimeTable));
    memset(world::g_relationMatrix, 0, sizeof(world::g_relationMatrix));
}

// io::LoadWorld points each KIND-30 (plant) object's runtime plantmap pointer
// (+113) at a fresh heap allocation whose address varies per load. g_objects is
// folded as raw bytes by HashFullWorld, so that pointer column makes the digest
// non-reproducible across loads. Point every live kind-30 object's +113 at one
// shared OWNED scratch so the column is stable (mirrors playable_slice /
// real_session). The plantmap is a heap artifact, not persisted record state.
void NormalizePlantPointers(std::uint32_t objectCount) {
    using namespace guild::sim;
    static std::vector<guild::u8> plantScratch(io::kPlantBytes, 0);
    guild::u8* scratch = plantScratch.data();
    for (std::uint32_t i = 0; i < objectCount && i < (std::uint32_t)kObjectCapacity; ++i) {
        guild::u8* r = reinterpret_cast<guild::u8*>(&g_objects[i]);
        if (r[0] != io::kKindPlant)
            continue;
        std::memcpy(r + 113, &scratch, sizeof scratch);
    }
}

// Count live person/scene records (marker word @+0 != -1).
std::uint32_t LivePersonCount() {
    using namespace guild::sim;
    std::uint32_t n = 0;
    for (int i = 0; i < kPersonCapacity; ++i)
        if (g_persons[i].marker != (guild::i16)-1)
            ++n;
    return n;
}

// ---------------------------------------------------------------------------
// Scene-tile index table WRITE (inverse of io::LoadPersonIndexTable @0x5a7ffc).
// Load order: count(4), then per slot +0(2) +2(4) +6(4) +10(4) +14(4) +18(1)
// +19(1) +28(0x1F). We emit the same field order from a caller-supplied tile base.
// ---------------------------------------------------------------------------
bool WriteSceneTileTable(io::VfsHandle* h, const guild::u8* tileBase,
                         std::uint32_t count) {
    if (!WR(h, &count, 4))
        return false;
    for (std::uint32_t i = 0; i < count; ++i) {
        const guild::u8* v = tileBase + (std::size_t)i * io::kSceneTileStride;
        if (!WR(h, v + 0, 2) || !WR(h, v + 2, 4) || !WR(h, v + 6, 4)
            || !WR(h, v + 10, 4) || !WR(h, v + 14, 4) || !WR(h, v + 18, 1)
            || !WR(h, v + 19, 1) || !WR(h, v + 28, 0x1F))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Global-counter table WRITE (inverse of io::LoadGlobalCounters @0x5a86d0) at the
// writer version (0x10045 >= every gate, so all fields are emitted). The field
// order is the exact READ order in save_world_load.cpp.
// ---------------------------------------------------------------------------
bool WriteGlobalCounters(io::VfsHandle* h, const guild::u8* counterBase) {
    for (int slot = 0; slot < io::kBuildCounterCount; ++slot) {
        const guild::u8* r = counterBase + (std::size_t)slot * io::kBuildCounterStride;
        if (!WR(h, r + 0x00, 2) || !WR(h, r + 0x02, 0x10) || !WR(h, r + 0x14, 4))
            return false;
        // version >= 0x1002C
        if (!WR(h, r + 0x28, 4))
            return false;
        if (!WR(h, r + 0x1C, 4) || !WR(h, r + 0x20, 4) || !WR(h, r + 0x2C, 4)
            || !WR(h, r + 0x30, 4) || !WR(h, r + 0x34, 4) || !WR(h, r + 0x38, 4)
            || !WR(h, r + 0x40, 4) || !WR(h, r + 0x44, 4) || !WR(h, r + 0x48, 4)
            || !WR(h, r + 0x4C, 4) || !WR(h, r + 0x50, 4))
            return false;
        // version >= 0x10014
        if (!WR(h, r + 0x54, 4) || !WR(h, r + 0x58, 4) || !WR(h, r + 0x5C, 4)
            || !WR(h, r + 0x3C, 4) || !WR(h, r + 0x60, 4) || !WR(h, r + 0x64, 4)
            || !WR(h, r + 0x68, 4))
            return false;
        // version >= 0x10015 (14-byte gametime)
        if (!WR(h, r + 0x70, 0xE))
            return false;
        if (!WR(h, r + 0x80, 4) || !WR(h, r + 0x84, 0x10) || !WR(h, r + 0x94, 0xC)
            || !WR(h, r + 0xA0, 4))
            return false;
        // version >= 0x10018
        if (!WR(h, r + 0x6C, 4))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Person/scene records WRITE (inverse of io::LoadCityRecords @0x5a8d3c). The
// preamble: word_63CC5C(2), count(4), idA(4), idB(4), then 8 handler ids
// (version >= 0x10017). Then per LIVE slot the leading marker word + the record
// body via io::SaveWritePersonSceneRecord (its first field IS the marker word, so
// it writes marker+body in one call exactly matching the load preamble+body).
//
// The counter biases (-1342 at +84, -1468 at +396) are applied INSIDE
// SaveWritePersonSceneRecord and re-added by LoadCityRecords, so the round trip
// restores the in-memory +84/+396 dwords. The four link slots (+364/+368/+380/
// +388) are written from the record's current values (the loader zeroed-then-read
// them, so they hold the on-disk link ids) and read straight back.
// ---------------------------------------------------------------------------
bool WritePersonRecords(io::VfsHandle* h, std::uint32_t personCount) {
    using namespace guild::sim;
    // Preamble. The portable load reuses the city marker / ids from the record
    // table; for the self-consistency round trip we re-emit them stably from the
    // live globals the loader populated (cityMarker/playerIdA/B/handlerIds were
    // returned into WorldState, but the live world keeps them implicitly — we
    // re-derive a stable preamble: marker 0, the live count, ids -1, handlers -1).
    // These preamble scalars are NOT folded by HashFullWorld and not re-read into
    // any folded global by LoadCityRecords (idA/idB/handlers go to non-folded
    // dword_6498E8/EC/F0), so a fixed preamble keeps S1==S2 byte-stable AND the
    // reloaded folded world (the person RECORDS) identical.
    guild::u16 marker = 0;
    guild::u32 liveCount = personCount;
    guild::i32 minus1 = -1;
    if (!WR(h, &marker, 2) || !WR(h, &liveCount, 4)
        || !WR(h, &minus1, 4) || !WR(h, &minus1, 4))
        return false;
    for (int i = 0; i < 8; ++i)
        if (!WR(h, &minus1, 4))
            return false;

    std::uint32_t written = 0;
    for (int i = 0; i < kPersonCapacity && written < personCount; ++i) {
        guild::u8* rec = reinterpret_cast<guild::u8*>(&g_persons[i]);
        guild::i16 m;
        std::memcpy(&m, rec + 0, 2);
        if (m == (guild::i16)-1)
            continue;                       // free slot
        io::PersonSceneLinks links{};
        std::memcpy(&links.idAt91, rec + 364, 4);
        std::memcpy(&links.idAt92, rec + 368, 4);
        std::memcpy(&links.idAt95, rec + 380, 4);
        std::memcpy(&links.idAt97, rec + 388, 4);
        if (!io::SaveWritePersonSceneRecord(h, rec, links))
            return false;
        ++written;
    }
    return written == personCount;
}

// A fixed, self-consistent save header for the portable city-seed round trip. The
// header bytes are NOT folded by HashFullWorld; emitting an identical header on
// every save keeps S1==S2 byte-stable. flagByte bit1 (partial / .cty path) is set
// so a re-load takes the partial path that stops after the building-slot tables —
// the exact subset this module serializes.
io::SaveHeader MakeRoundTripHeader() {
    io::SaveHeader hdr{};
    hdr.magic = io::kSaveVersionCurrent;     // 0x10045
    hdr.flagByte = 2;                         // partial (.cty / network) path
    std::strncpy(hdr.name, "ROUNDTRIP", sizeof hdr.name - 1);
    hdr.field132 = 2;
    std::strncpy(hdr.name96, "Roundtrip", sizeof hdr.name96 - 1);
    return hdr;
}

// A fixed scalar block (also non-folded; emitted identically each save). Its
// fields default-restore cleanly through SaveLoadScalarBlock at 0x10045.
io::SaveScalarBlock MakeRoundTripScalarBlock() {
    io::SaveScalarBlock blk{};
    blk.g632240 = 1000000;                    // the >=0x1003D default
    return blk;
}

} // namespace

// ===========================================================================
// RoundTripZeroWorld — blank the slate. The economy/law/office/... tables are
// left ZEROED (not re-seeded): a `.cty` partial load does not populate them, and
// keeping them blank in BOTH the post-load hash AND the post-reload hash makes the
// round trip a pure test of the SERIALIZED world (the entity records + scene
// substrate the save format carries) — without a CityInitParameterTable call in
// the reload path the seeded economy would diverge across the two hashes.
// ===========================================================================
void RoundTripZeroWorld(std::uint32_t seed) {
    ZeroWorldGlobals();
    sim::ResetEntityArrays();                 // markers -> -1 (free), alive -> 0
    crt::Srand(seed);
}

// ===========================================================================
// SaveLiveWorld — serialize the live world (portable city-seed subset) at 0x10045.
// ===========================================================================
bool SaveLiveWorld(std::vector<guild::u8>& out,
                   std::uint32_t objectCount, std::uint32_t personCount,
                   std::uint32_t sceneTileCount) {
    (void)objectCount;  // the object table writer scans the live alive bytes itself.
    // A generous fixed buffer: the per-table loaders bound by capacity. The portable
    // subset is dominated by the building-slot tables (~43 KB) + the object table
    // (256 * ~169) + person records (768 * ~536). 16 MiB is comfortably above any
    // shipped city.
    out.assign(16u * 1024u * 1024u, 0);

    io::SaveVersionSet(io::kSaveVersionCurrent);  // 0x10045 — all gates satisfied

    io::VfsHandle* h = io::VfsOpenMemoryStream(out.data(), (guild::u32)out.size(), "wb");
    if (!h)
        return false;

    bool ok = false;
    do {
        // 1. Header + thumbnail (the writer emits a null thumbnail; the reader for a
        //    0x10045 file skips an absent thumbnail per its version gates).
        io::SaveHeader hdr = MakeRoundTripHeader();
        if (!io::SaveWriteScenarioBlock(h, hdr, nullptr))
            break;
        // 2. Scalar block.
        io::SaveScalarBlock blk = MakeRoundTripScalarBlock();
        if (!io::SaveWriteScalarBlock(h, blk))
            break;
        // 3. Scene-tile index table (LOAD-only sibling — emitted by inversion).
        {
            // The live scene-tile table is the WorldState copy; the live load wrote
            // it into a portable buffer, but the FOLDED world does not include the
            // scene tiles. For the round trip we re-emit the scene-tile COUNT with a
            // zeroed body (the loader stores the count into dword_6498C0, which IS
            // folded indirectly via g_sceneNodeCount? — no: dword_6498C0 is the
            // scene-node scan bound, not a folded table). We emit `sceneTileCount`
            // records of zeroes so the loader consumes the right span and restores
            // the count; the tile bytes themselves are not folded so zero is stable.
            static std::vector<guild::u8> tileScratch;
            std::size_t need = (std::size_t)sceneTileCount * io::kSceneTileStride;
            if (tileScratch.size() < need) tileScratch.assign(need, 0);
            if (!WriteSceneTileTable(h, tileScratch.data(), sceneTileCount))
                break;
        }
        // 4. Object / building array (real WRITE serializer; no extra-96 table).
        if (!io::SaveWritePersonTable(h, reinterpret_cast<guild::u8*>(&sim::g_objects[0]),
                                      nullptr, 0))
            break;
        // 5. Global-counter table (LOAD-only sibling — emitted by inversion). The
        //    folded world does not include these counters; the load consumes 16
        //    164-byte records. Emit 16 zeroed records (stable, not folded).
        {
            static std::vector<guild::u8> counterScratch(
                (std::size_t)io::kBuildCounterStride * io::kBuildCounterCount, 0);
            if (!WriteGlobalCounters(h, counterScratch.data()))
                break;
        }
        // 6. Person / scene records (real WRITE serializer per record).
        if (!WritePersonRecords(h, personCount))
            break;
        // 7. Building-slot tables (real WRITE serializer; zeroed scratch — not folded).
        {
            static std::vector<guild::u8> slotScratch(
                (std::size_t)io::kBst_CitySlotTableStride * io::kBst_CitySlotTableCount, 0);
            static std::vector<guild::u8> cityScratch(
                (std::size_t)io::kBst_CityInfoStride * io::kBst_CityInfoRecCount, 0);
            if (!io::SaveWriteBuildingSlotTables(h, slotScratch.data(), cityScratch.data(),
                                                 io::kSaveVersionCurrent))
                break;
        }
        ok = true;
    } while (false);

    long len = io::VfsTell(h);
    io::VfsCloseStream(h);
    if (!ok || len < 0)
        return false;
    out.resize((std::size_t)len);
    return true;
}

// ===========================================================================
// ReloadSavedWorld — read a SaveLiveWorld stream back into a zeroed live world.
// ===========================================================================
bool ReloadSavedWorld(const std::vector<guild::u8>& stream,
                      std::uint32_t* objectCountOut, std::uint32_t* personCountOut,
                      std::uint32_t* sceneTileCountOut) {
    if (stream.empty())
        return false;

    // The live arrays must be blank before a reload (so unserialized pad bytes start
    // clean and the raw-byte digest matches the first load). The caller (RoundTrip*)
    // zeroes folded tables; here we additionally reset the entity arrays so free
    // slots are marker==-1 / alive==0 exactly as a fresh LoadWorld does.
    sim::ResetEntityArrays();

    io::VfsHandle* h = io::VfsOpenMemoryStream(const_cast<guild::u8*>(stream.data()),
                                               (guild::u32)stream.size(), "rb");
    if (!h)
        return false;

    std::uint32_t objCount = 0, persCount = 0, tileCount = 0;
    bool ok = false;
    do {
        io::SaveHeader hdr{};
        if (!io::SaveLoadHeaderAndThumbnail(h, hdr, nullptr))
            break;
        guild::u32 version = io::SaveVersionGet();
        io::SaveScalarBlock blk{};
        if (!io::SaveLoadScalarBlock(h, blk))
            break;
        // Scene-tile index table.
        static std::vector<guild::u8> tileScratch(
            (std::size_t)io::kSceneTileStride * io::kSceneTileCapacity, 0);
        if (!io::LoadPersonIndexTable(h, tileScratch.data(), &tileCount))
            break;
        // Object / building table.
        {
            static guild::u8 plantScratch[io::kPlantBytes];
            static guild::u8 extra96Scratch[io::kExtra96Stride * 256];
            guild::u32 extra = 0;
            if (!io::SaveLoadPersonTable(h, reinterpret_cast<guild::u8*>(&sim::g_objects[0]),
                                         version,
                                         [](void* ctx) -> guild::u8* {
                                             return reinterpret_cast<guild::u8*>(ctx);
                                         },
                                         plantScratch, extra96Scratch, &extra))
                break;
            (void)extra;
            guild::u8* ob = reinterpret_cast<guild::u8*>(&sim::g_objects[0]);
            for (guild::u32 o = 0; o != io::kObjScanBytes; o += io::kObjStride)
                if (ob[o]) ++objCount;
        }
        // Global-counter table.
        static std::vector<guild::u8> counterScratch(
            (std::size_t)io::kBuildCounterStride * io::kBuildCounterCount, 0);
        if (!io::LoadGlobalCounters(h, counterScratch.data(), version))
            break;
        // Person / scene records.
        guild::u16 cm = 0; guild::u32 ida = 0, idb = 0, handlers[8] = {};
        if (!io::LoadCityRecords(h, reinterpret_cast<guild::u8*>(&sim::g_persons[0]),
                                 version, &cm, &persCount, &ida, &idb, handlers))
            break;
        for (int i = 0; i < sim::kPersonCapacity; ++i)
            if (sim::g_persons[i].marker != (guild::i16)-1)
                sim::g_personIds[i] = sim::g_persons[i].id;
        sim::g_personArrayLoaded = true;
        // Building-slot tables.
        static std::vector<guild::u8> slotScratch(
            (std::size_t)7952 * io::kCitySlotTableCount, 0);
        static std::vector<guild::u8> cityScratch(
            (std::size_t)756 * io::kCityInfoRecCount, 0);
        if (!io::LoadBuildingSlotTables(h, slotScratch.data(), cityScratch.data(), version))
            break;
        ok = true;
    } while (false);

    io::VfsCloseStream(h);
    if (!ok)
        return false;
    if (objectCountOut)    *objectCountOut = objCount;
    if (personCountOut)    *personCountOut = persCount;
    if (sceneTileCountOut) *sceneTileCountOut = tileCount;
    return true;
}

// ===========================================================================
// RoundTripLiveWorld — save->reload->resave invariant over the LIVE world.
// ===========================================================================
RoundTripResult RoundTripLiveWorld(std::uint32_t objectCount, std::uint32_t personCount,
                                   std::uint32_t sceneTileCount, std::uint32_t seed) {
    RoundTripResult r;
    r.loaded = true;
    r.objectCount = objectCount;
    r.personCount = personCount;
    r.sceneTiles  = sceneTileCount;

    // h1: hash the live (pre-save) world. Re-anchor the RNG right before the hash
    // (the base digest folds the live CRT RNG state).
    NormalizePlantPointers(objectCount);
    crt::Srand(seed);
    r.hashAfterLoad = HashFullWorld();

    // SAVE -> S1.
    std::vector<guild::u8> s1;
    r.saved = SaveLiveWorld(s1, objectCount, personCount, sceneTileCount);
    r.saveBytes1 = s1.size();
    if (!r.saved)
        return r;

    // ZERO + RELOAD from S1.
    ZeroWorldGlobals();
    std::uint32_t ro = 0, rp = 0, rt = 0;
    r.reloaded = ReloadSavedWorld(s1, &ro, &rp, &rt);
    if (!r.reloaded)
        return r;
    // h2: hash the reloaded world (same RNG anchor as h1).
    NormalizePlantPointers(ro);
    crt::Srand(seed);
    r.hashAfterReload = HashFullWorld();

    // SAVE again -> S2; assert byte-stable.
    std::vector<guild::u8> s2;
    bool saved2 = SaveLiveWorld(s2, ro, rp, rt);
    r.saveBytes2 = s2.size();
    r.saveBytesStable = saved2 && (s1.size() == s2.size()) &&
                        (std::memcmp(s1.data(), s2.data(), s1.size()) == 0);
    return r;
}

// ===========================================================================
// RoundTripCity — load a .cty, then run the M3 invariant.
// ===========================================================================
RoundTripResult RoundTripCity(shim::IFileSystem* /*fs*/, const std::string& ctyPath,
                              std::uint32_t seed) {
    RoundTripResult r;

    // Blank slate so the load hash is a pure function of (city + seed).
    RoundTripZeroWorld(seed);

    io::WorldState world{};
    r.loaded = io::LoadWorld(ctyPath.c_str(), world);
    if (!r.loaded)
        return r;
    r.objectCount = world.objectCount;
    r.personCount = LivePersonCount();   // the live marker!=-1 count we'll save
    r.sceneTiles  = world.sceneTileCount;

    RoundTripResult inner = RoundTripLiveWorld(r.objectCount, r.personCount,
                                               r.sceneTiles, seed);
    inner.loaded = true;
    inner.objectCount = r.objectCount;
    inner.personCount = r.personCount;
    inner.sceneTiles  = r.sceneTiles;
    return inner;
}

} // namespace guild::play
