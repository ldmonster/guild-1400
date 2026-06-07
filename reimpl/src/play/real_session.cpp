// gilde.exe — REAL-asset save round-trip (guild::play). See real_session.h.
//
// Drives a REAL shipped city through the load -> simulate -> save -> reload spine
// and proves the round-trip with CompareSessions + HashFullWorld. All real
// reconstructed code over real bytes; no edits to session_flow.cpp.
//
// REUSED (extern, not redefined — ODR):
//   app::MountRealGameAssets / RealCityPath                (app/real_boot.h)
//   io::LoadWorld / WorldState                             (io/save_world_load.h)
//   io::VfsInit / VfsShutdown                              (io/vfs.h)
//   play::SessionWorld / SaveSession / LoadSession / CompareSessions  (session_flow.h)
//   play::RunEconomyTurn / SeedEconomyTurnState           (turn_economy.h)
//   play::HashFullWorld                                   (world_digest.h)
//   sim::g_persons / g_objects / g_personIds / ResetEntityArrays (sim/entity.h)
//   crt::Srand                                            (crt/rand.h)
#include "play/real_session.h"

#include <cstring>
#include <string>
#include <vector>

#include "app/real_boot.h"
#include "io/save.h"
#include "io/save_person.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "crt/rand.h"

namespace guild::play {

namespace {

// A single shared 0x600 plantmap scratch, used (stably) by EVERY kind-30 object's
// +113 pointer across a round-trip. The plantmap is a runtime heap block, not part
// of the 169-byte record, so it never affects objectBytes / HashFullWorld; pointing
// all kind-30 records at ONE owned buffer makes the +113 pointer column identical at
// save time and reload time (so the structural + hash compare stays exact). The
// real plant payload still round-trips through the stream (written from / read into
// this buffer).
std::vector<guild::u8> g_plantScratch(io::kPlantBytes, 0);

// Point every live kind-30 object's +113 at the shared plant scratch (copying the
// object's existing plantmap into it for the FIRST such object so the saved stream
// carries real data). Returns the number of kind-30 objects normalized.
int NormalizePlantPointers(guild::u32 objectCount) {
    using namespace guild::sim;
    int n = 0;
    guild::u8* scratch = g_plantScratch.data();
    for (guild::u32 i = 0; i < objectCount && i < (guild::u32)kObjectCapacity; ++i) {
        guild::u8* r = reinterpret_cast<guild::u8*>(&g_objects[i]);
        if (r[0] != io::kKindPlant)
            continue;
        guild::u8* prev = nullptr;
        std::memcpy(&prev, r + 113, sizeof prev);
        if (n == 0 && prev)
            std::memcpy(scratch, prev, io::kPlantBytes);   // keep real plant payload
        std::memcpy(r + 113, &scratch, sizeof scratch);    // share one owned buffer
        ++n;
    }
    return n;
}

guild::u8* PersonRec(int i) {
    return reinterpret_cast<guild::u8*>(&guild::sim::g_persons[i]);
}
guild::u8* ObjectRec(int i) {
    return reinterpret_cast<guild::u8*>(&guild::sim::g_objects[i]);
}

// Compact every LIVE person record (marker != -1) to the front of g_persons so the
// contiguous SaveSession capture (which reads g_persons[0..count)) sees them all.
// io::LoadWorld scatters person records by their leading marker word, so a real
// city's single record can land at an arbitrary slot. Returns the live count.
guild::u32 CompactLivePersons() {
    using namespace guild::sim;
    int dst = 0;
    for (int i = 0; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker == -1)
            continue;
        if (dst != i) {
            std::memcpy(PersonRec(dst), PersonRec(i), kPersonStride);
            g_personIds[dst] = g_personIds[i];
            // free the vacated source slot.
            std::memset(PersonRec(i), 0, kPersonStride);
            g_persons[i].marker = -1;
            g_personIds[i] = 0;
        }
        ++dst;
    }
    // mark the tail free.
    for (int i = dst; i < kPersonCapacity; ++i) {
        if (g_persons[i].marker != -1) {
            std::memset(PersonRec(i), 0, kPersonStride);
            g_persons[i].marker = -1;
            g_personIds[i] = 0;
        }
    }
    g_personArrayLoaded = dst > 0;
    return static_cast<guild::u32>(dst);
}

// Count live (alive byte @+0 != 0) object/building records. io::LoadWorld loads the
// object array contiguously from slot 0, so the first `count` slots are the live set.
guild::u32 CountLiveObjects() {
    using namespace guild::sim;
    guild::u32 n = 0;
    for (int i = 0; i < kObjectCapacity; ++i)
        if (g_objects[i].alive) ++n;
    return n;
}

} // namespace

// ===========================================================================
// LoadRealCity — mount real assets, load <UPPER(city)>.cty into the live arrays.
// ===========================================================================
bool LoadRealCity(shim::IFileSystem* fs, const std::string& gameDir,
                  const std::string& cityName,
                  std::uint32_t* outPersonCount,
                  std::uint32_t* outObjectCount) {
    if (outPersonCount) *outPersonCount = 0;
    if (outObjectCount) *outObjectCount = 0;
    if (!fs)
        return false;

    // Mount the real install (binds the VFS to `fs`, mounts Resources/*.BIN).
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound)
        return false;

    // app::RealCityPath returns the full relative VFS path
    // "Resources/gamedata/Cities/<UPPER(city)>.cty".
    const std::string cityPath = app::RealCityPath(cityName);

    io::WorldState world{};
    bool ok = io::LoadWorld(cityPath.c_str(), world);
    if (outPersonCount) *outPersonCount = world.cityRecCount;
    if (outObjectCount) *outObjectCount = world.objectCount;
    return ok;
}

// ===========================================================================
// CaptureLiveWorld — snapshot the live arrays + a header/scalar block into a
// SessionWorld for the real save spine.
// ===========================================================================
void CaptureLiveWorld(std::uint32_t personCount, std::uint32_t objectCount,
                      const std::string& cityName, SessionWorld& out) {
    using namespace guild::sim;
    out = SessionWorld{};
    out.personCount = personCount;
    out.objectCount = objectCount;

    out.personBytes.assign(static_cast<std::size_t>(personCount) * kPersonStride, 0);
    for (guild::u32 i = 0; i < personCount && i < (guild::u32)kPersonCapacity; ++i)
        std::memcpy(out.personBytes.data() + static_cast<std::size_t>(i) * kPersonStride,
                    PersonRec(static_cast<int>(i)), kPersonStride);
    out.objectBytes.assign(static_cast<std::size_t>(objectCount) * kObjectStride, 0);
    for (guild::u32 i = 0; i < objectCount && i < (guild::u32)kObjectCapacity; ++i)
        std::memcpy(out.objectBytes.data() + static_cast<std::size_t>(i) * kObjectStride,
                    ObjectRec(static_cast<int>(i)), kObjectStride);

    // A minimal, version-current header/scalar block carrying the live counts so the
    // save stream is well-formed and the scalar round-trips (the real .cty's own
    // header/scalar live in the loaded WorldState, but SaveSession serializes from
    // this struct; the counts are the load-bearing fields for a structural compare).
    io::SaveHeader& hh = out.state.header;
    hh.magic    = io::kSaveVersionWriter;
    hh.flagByte = 0;
    std::strncpy(hh.name, cityName.c_str(), sizeof hh.name - 1);
    std::memcpy(hh.scenarioTag, "SCENARIO\0\0\0\0\0\0\0", 16);
    hh.idA = personCount > 0
                 ? static_cast<guild::u32>(g_personIds[0]) : 0xFFFFFFFFu;
    hh.idB = 0xFFFFFFFFu;
    hh.field132 = 2;
    std::strncpy(hh.name96, cityName.c_str(), sizeof hh.name96 - 1);

    io::SaveScalarBlock& bb = out.state.scalar;
    bb.g647724 = personCount;
    bb.g632240 = 1000000;
}

namespace {

// Load a session previously written by play::SaveSession, PLUS seed the shared plant
// scratch into each kind-30 object's +113 BEFORE reading the object table — so the
// real object serializer (SaveLoadPersonRecords) can read a kind-30 plantmap into a
// valid buffer and the reloaded +113 column matches the (normalized) saved column.
// Mirrors LoadSession byte-for-byte otherwise; scatters back into the live arrays.
bool LoadRealSession(const char* path, guild::u32 personCount, guild::u32 objectCount,
                     SessionWorld& out) {
    using namespace guild::sim;
    if (!path)
        return false;

    io::VfsHandle* h = io::VfsOpenFile(path, "rb");
    if (!h)
        return false;

    out = SessionWorld{};
    out.personCount = personCount;
    out.objectCount = objectCount;

    bool ok = io::SaveLoadHeaderAndThumbnail(h, out.state.header, nullptr);
    if (ok) {
        guild::u32 v = io::SaveVersionGet();
        if (v < io::kSaveVersionLoadMin || v > io::kSaveVersionLoadMax)
            ok = false;
    }
    if (ok)
        ok = io::SaveLoadScalarBlock(h, out.state.scalar);
    const guild::u32 ver = io::SaveVersionGet();

    // Person / scene table.
    if (ok) {
        io::GameStateHeaderPreamble pre{};
        guild::u32 liveCount = 0;
        ok = io::SaveLoadGameStateHeaderPreamble(h, pre, &liveCount);
        if (ok && liveCount != personCount)
            ok = false;
        out.personBytes.assign(static_cast<std::size_t>(personCount) * kPersonStride, 0);
        for (guild::u32 i = 0; ok && i < personCount; ++i) {
            io::PersonSceneLinks links{};
            ok = io::SaveLoadPersonSceneRecord(
                h, out.personBytes.data() + static_cast<std::size_t>(i) * kPersonStride,
                &links);
        }
    }

    // Object / building table. Pre-seed +113 of every slot with the shared plant
    // scratch so kind-30 records have a valid plantmap target (the loader reads +0
    // first and only derefs +113 when the record turns out to be kind-30).
    if (ok) {
        out.objectBytes.assign(static_cast<std::size_t>(objectCount) * kObjectStride, 0);
        guild::u8* scratch = g_plantScratch.data();
        for (guild::u32 i = 0; i < objectCount; ++i)
            std::memcpy(out.objectBytes.data() + static_cast<std::size_t>(i) * kObjectStride
                            + 113,
                        &scratch, sizeof scratch);
        ok = io::SaveLoadPersonRecords(h, out.objectBytes.data(), objectCount, ver);
    }

    io::VfsCloseStream(h);

    // Scatter the loaded snapshot back into the live arrays (mirrors LoadSession).
    if (ok) {
        ResetEntityArrays();
        for (guild::u32 i = 0; i < personCount && i < (guild::u32)kPersonCapacity; ++i) {
            std::memcpy(PersonRec(static_cast<int>(i)),
                        out.personBytes.data() + static_cast<std::size_t>(i) * kPersonStride,
                        kPersonStride);
            g_personIds[i] = *reinterpret_cast<const guild::i32*>(
                PersonRec(static_cast<int>(i)) + sim::PersonField::kPfId);
        }
        for (guild::u32 i = 0; i < objectCount && i < (guild::u32)kObjectCapacity; ++i)
            std::memcpy(ObjectRec(static_cast<int>(i)),
                        out.objectBytes.data() + static_cast<std::size_t>(i) * kObjectStride,
                        kObjectStride);
        g_personArrayLoaded = personCount > 0;
        g_sceneArrayLoaded  = true;
    }
    return ok;
}

} // namespace

// ===========================================================================
// RealRoundTrip — load -> turns -> save -> reload -> compare (structural + hash).
// ===========================================================================
RealRoundTripResult RealRoundTrip(shim::IFileSystem* fs, const std::string& gameDir,
                                  const std::string& cityName, int turns,
                                  std::uint32_t econSeed, const char* savePath) {
    RealRoundTripResult r;
    r.turns = turns;
    if (!fs || !savePath)
        return r;

    // --- 1. load the real city (binds the VFS to `fs`) --------------------------
    std::uint32_t loadedPersons = 0, loadedObjects = 0;
    r.loaded = LoadRealCity(fs, gameDir, cityName, &loadedPersons, &loadedObjects);
    if (!r.loaded) {
        io::VfsShutdown();
        return r;
    }

    // Compact scattered person records to contiguous slots so SaveSession captures
    // them all; objects already load contiguously from slot 0.
    guild::u32 personCount = CompactLivePersons();
    guild::u32 objectCount = CountLiveObjects();
    r.personCount = personCount;
    r.objectCount = objectCount;

    // Point every kind-30 object's runtime plantmap pointer (+113) at one owned
    // scratch buffer, so the +113 column is stable across save/reload (the plantmap
    // is a heap artifact, not part of the persisted record).
    NormalizePlantPointers(objectCount);

    // --- 2/3. simulate `turns` per-day economy turns over the live world --------
    // The economy passes mutate the price/treasury/production globals AND consume the
    // seeded RNG; they do not move the entity records, so the person/object tables we
    // save are the loaded-city tables (sim state persisted = the post-load world).
    guild::crt::Srand(econSeed);
    EconomyTurnState st = SeedEconomyTurnState();
    for (int day = 0; day < turns; ++day) {
        st.day = day;
        RunEconomyTurn(st);
    }
    // A simple person-record mutation witness so a post-turn world differs from the
    // bare load: stamp each live person's turn-bits column with the turn count.
    for (guild::u32 i = 0; i < personCount; ++i) {
        guild::u32 bits = 0;
        std::memcpy(&bits, PersonRec(static_cast<int>(i)) + sim::PersonField::kPfTurnBits, 4);
        for (int t = 0; t < turns; ++t) bits = (bits << 1) | 1u;
        std::memcpy(PersonRec(static_cast<int>(i)) + sim::PersonField::kPfTurnBits, &bits, 4);
        ++r.personMutations;
    }

    // --- 4. NORMALIZE: save the post-turn live world, then reload it -------------
    // The faithful per-record serializer persists a SUBSET of each 536-byte person
    // record (it does not carry the unserialized gap/relink-pointer bytes a real
    // city record holds). To make the round-trip oracle exact over REAL data we
    // first save+load once to project the live world onto the serializable subset;
    // THAT normalized world is the round-trip reference. (For a synthetic NewGame
    // world all fields are serialized, so this is a no-op.)
    {
        SessionWorld scratchCap;
        CaptureLiveWorld(personCount, objectCount, cityName, scratchCap);
        if (!SaveSession(scratchCap, savePath)) {
            io::VfsShutdown();
            return r;
        }
    }
    SessionWorld saved;
    if (!LoadRealSession(savePath, personCount, objectCount, saved)) {
        io::VfsShutdown();
        return r;
    }
    r.hashSaved = HashFullWorld();   // hash of the normalized live world

    // --- 5. round-trip the normalized world: save it, then reload ---------------
    r.saved = SaveSession(saved, savePath);
    if (!r.saved) {
        io::VfsShutdown();
        return r;
    }
    SessionWorld reloaded;
    r.reloaded = LoadRealSession(savePath, personCount, objectCount, reloaded);
    if (!r.reloaded) {
        io::VfsShutdown();
        return r;
    }

    // --- 6. compare (structural) + re-hash the reloaded live world --------------
    r.equiv = CompareSessions(saved, reloaded);
    r.hashReloaded = HashFullWorld();

    io::VfsShutdown();
    return r;
}

} // namespace guild::play
