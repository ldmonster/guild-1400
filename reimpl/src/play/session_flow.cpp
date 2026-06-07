// gilde.exe — session / save flow (guild::play). See session_flow.h.
//
// Assembles the reconstructed save/load layer into a playable session flow: a
// deterministic new-game seed into the live entity arrays, a turn simulator over
// those live arrays, and a round-trip through the REAL io save spine + per-table
// serializers (so simulated state genuinely persists across save/reload).
//
// REUSED (extern, not redefined — ODR):
//   io::WriteGameState / LoadGameState / GameState        (io/gamestate.h)
//   io::SaveWriteScenarioBlock / SaveLoadHeaderAndThumbnail,
//   io::SaveWriteScalarBlock / SaveLoadScalarBlock,
//   io::SaveWritePersonRecords / SaveLoadPersonRecords,
//   io::SaveWriteGameStateHeaderPreamble / ...PersonSceneRecord (+ loaders),
//   io::SaveVersionGet/Set, io::Vfs*                       (io/save.h, io/vfs.h)
//   io::LoadWorld / WorldState                             (io/save_world_load.h)
//   sim::g_persons / g_objects / ResetEntityArrays         (sim/entity.h)
//   crt::Srand / RandNext                                  (crt/rand.h)
#include "play/session_flow.h"

#include <cstring>

#include "io/save.h"
#include "io/save_person.h"
#include "io/save_world_load.h"
#include "io/vfs.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "crt/rand.h"

namespace guild::play {

namespace {

// 0..0x7FFF deterministic draw (crt::RandNext) reduced to [0, n).
inline int Draw(int n) { return n > 0 ? (guild::crt::RandNext() % n) : 0; }

// A live person/scene record is a 536-byte POD (sim::Person == kPersonStride). The
// new-game seed and turn simulator address it through the same byte offsets the
// engine accessors use (sim::PersonField), so the live path is exercised exactly.
guild::u8* PersonRec(int i) {
    return reinterpret_cast<guild::u8*>(&guild::sim::g_persons[i]);
}
guild::u8* ObjectRec(int i) {
    return reinterpret_cast<guild::u8*>(&guild::sim::g_objects[i]);
}

template <class T>
void PutAt(guild::u8* base, int off, T v) {
    std::memcpy(base + off, &v, sizeof(T));
}
template <class T>
T GetAt(const guild::u8* base, int off) {
    T v; std::memcpy(&v, base + off, sizeof(T)); return v;
}

} // namespace

// ===========================================================================
// NewGame — seed a small live world into sim::g_persons / sim::g_objects.
// ===========================================================================
void NewGame(const SessionConfig& cfg, SessionWorld& world) {
    using namespace guild::sim;

    int nPersons = cfg.persons;
    if (nPersons < 0) nPersons = 0;
    if (nPersons > kPersonCapacity) nPersons = kPersonCapacity;
    int nObjects = cfg.objects;
    if (nObjects < 0) nObjects = 0;
    if (nObjects > kObjectCapacity) nObjects = kObjectCapacity;

    // Determinism anchor: the same seed reproduces the identical world.
    guild::crt::Srand(cfg.seed);

    ResetEntityArrays();

    // --- person / scene records (536-stride) ------------------------------
    for (int i = 0; i < nPersons; ++i) {
        guild::u8* r = PersonRec(i);
        std::memset(r, 0, kPersonStride);
        const guild::i16 marker = static_cast<guild::i16>(1 + Draw(7));   // != -1 (live)
        const guild::i32 id      = 1000 + i;
        PutAt<guild::i16>(r, 0x00, marker);                               // +0 marker
        PutAt<guild::u8>(r, PersonField::kPfKind,
                         static_cast<guild::u8>(4 + Draw(6)));            // <10 == person
        PutAt<guild::i32>(r, PersonField::kPfId, id);                    // +4 id
        g_personIds[i] = id;                                             // parallel column
        PutAt<guild::u8>(r, PersonField::kPfIsPlayer, i == 0 ? 1 : 0);   // player == slot 0
        PutAt<guild::u8>(r, PersonField::kPfGender,
                         static_cast<guild::u8>(Draw(2)));
        PutAt<guild::i16>(r, PersonField::kPfCash,
                          static_cast<guild::i16>(100 + Draw(900)));     // cash-on-hand
        PutAt<guild::u8>(r, PersonField::kPfAge,
                         static_cast<guild::u8>(18 + Draw(40)));
        PutAt<guild::u32>(r, PersonField::kPfSeed, guild::crt::RandNext()
                                                    | (guild::crt::RandNext() << 16));
        PutAt<guild::u32>(r, PersonField::kPfWealthScore,
                          static_cast<guild::u32>(500 + Draw(5000)));
        PutAt<guild::u32>(r, PersonField::kPfTurnBits, 0u);              // simulated each turn
    }

    // --- object / building records (169-stride) ---------------------------
    for (int i = 0; i < nObjects; ++i) {
        guild::u8* r = ObjectRec(i);
        std::memset(r, 0, kObjectStride);
        // alive byte in [1,29] (NEVER 30 — kind-30 records carry a heap plantmap
        // pointer the object serializer would dereference; we seed plain objects).
        PutAt<guild::u8>(r, 0x00, static_cast<guild::u8>(1 + Draw(29))); // alive (!=0)
        PutAt<guild::i32>(r, 0x01, 2000 + i);                           // +1 id (unaligned)
        // a couple of payload bytes so the table is non-trivial / changes are visible.
        PutAt<guild::u8>(r, 0x05, static_cast<guild::u8>(Draw(256)));
        PutAt<guild::u32>(r, 0x09, static_cast<guild::u32>(Draw(100000)));
        // Load-injected defaults for fields the (faithful) object save format does
        // NOT store: VIBE_Save object load sets *(r+48)=5000 and *(r+149)=-1. Seed
        // them so a freshly-NewGame'd object is byte-identical to a loaded one
        // (else the save->load round-trip is correctly non-byte-equal here).
        PutAt<guild::i32>(r, 48, 5000);
        PutAt<guild::i32>(r, 149, -1);
    }

    g_personArrayLoaded = nPersons > 0;
    g_sceneArrayLoaded  = true;

    // --- build the scalar GameState header/block --------------------------
    world = SessionWorld{};
    world.personCount = static_cast<guild::u32>(nPersons);
    world.objectCount = static_cast<guild::u32>(nObjects);

    io::SaveHeader& h = world.state.header;
    h.magic    = io::kSaveVersionWriter;
    h.flagByte = 0;   // full single-player save (not partial / network)
    std::strncpy(h.name, cfg.cityName.c_str(), sizeof h.name - 1);
    h.season    = static_cast<guild::u8>(1 + Draw(4));
    h.extraByte = 0;
    std::memcpy(h.scenarioTag, "SCENARIO\0\0\0\0\0\0\0", 16);
    h.wealth    = static_cast<guild::u32>(GetAt<guild::u32>(PersonRec(0),
                                          PersonField::kPfWealthScore));
    h.idA = nPersons > 0 ? static_cast<guild::u32>(1000) : 0xFFFFFFFFu; // player id
    h.idB = 0xFFFFFFFFu;
    h.field132 = 2;
    std::strncpy(h.name96, cfg.cityName.c_str(), sizeof h.name96 - 1);

    io::SaveScalarBlock& b = world.state.scalar;
    b.g649890 = cfg.seed;                 // stash the seed (a deterministic anchor)
    b.g632244 = 0;
    b.season  = h.season;
    b.g6498E4 = 1000;                     // player id
    b.g64771C = 0; b.g647720 = 0;
    b.g647724 = world.personCount;        // live person count (dword_647724)
    b.unk6477A8 = 0;
    b.g649894 = 0;
    b.g632240 = 1000000;                  // starting treasury (default)
    // game clock starts at day 0; RunTurns advances it.
    std::memset(b.gameTime, 0, sizeof b.gameTime);
}

// ===========================================================================
// RunTurns — simulate `turns` turns over the live arrays + scalar clock.
// ===========================================================================
int RunTurns(SessionWorld& world, int turns) {
    using namespace guild::sim;
    int mutations = 0;
    for (int t = 0; t < turns; ++t) {
        // advance the scalar game clock by one day (the +0 dword of gameTime).
        io::SaveScalarBlock& b = world.state.scalar;
        guild::i32 day = GetAt<guild::i32>(b.gameTime, 0);
        PutAt<guild::i32>(b.gameTime, 0, day + 1);

        // age + accrue every live person (writes the canonical sim::g_persons).
        for (guild::u32 i = 0; i < world.personCount; ++i) {
            guild::u8* r = PersonRec(static_cast<int>(i));
            // cash accrues a deterministic per-person stipend.
            guild::i16 cash = GetAt<guild::i16>(r, PersonField::kPfCash);
            cash = static_cast<guild::i16>(cash + 7 + (i & 3));
            PutAt<guild::i16>(r, PersonField::kPfCash, cash);
            // per-turn bitfield rotates so the record visibly changes each turn.
            guild::u32 bits = GetAt<guild::u32>(r, PersonField::kPfTurnBits);
            bits = (bits << 1) | 1u;
            PutAt<guild::u32>(r, PersonField::kPfTurnBits, bits);
            // wealth tracks cash.
            guild::u32 w = GetAt<guild::u32>(r, PersonField::kPfWealthScore);
            PutAt<guild::u32>(r, PersonField::kPfWealthScore, w + 1u);
            ++mutations;
        }

        // touch every live object (its +5 payload byte advances).
        for (guild::u32 i = 0; i < world.objectCount; ++i) {
            guild::u8* r = ObjectRec(static_cast<int>(i));
            guild::u8 p = GetAt<guild::u8>(r, 0x05);
            PutAt<guild::u8>(r, 0x05, static_cast<guild::u8>(p + 1));
        }
    }
    // keep the header treasury/wealth field in sync with the lead person.
    if (world.personCount > 0) {
        world.state.header.wealth =
            GetAt<guild::u32>(PersonRec(0), PersonField::kPfWealthScore);
    }
    return mutations;
}

// ===========================================================================
// SaveSession — capture the live tables, write header+scalar+tables to one stream.
// ===========================================================================
bool SaveSession(SessionWorld& world, const char* path) {
    using namespace guild::sim;
    if (!path)
        return false;

    // Capture the live entity tables into the snapshot (so a reload can be compared
    // and the round-trip does not alias the global arrays).
    world.personBytes.assign(static_cast<std::size_t>(world.personCount) * kPersonStride, 0);
    for (guild::u32 i = 0; i < world.personCount; ++i)
        std::memcpy(world.personBytes.data() + static_cast<std::size_t>(i) * kPersonStride,
                    PersonRec(static_cast<int>(i)), kPersonStride);
    world.objectBytes.assign(static_cast<std::size_t>(world.objectCount) * kObjectStride, 0);
    for (guild::u32 i = 0; i < world.objectCount; ++i)
        std::memcpy(world.objectBytes.data() + static_cast<std::size_t>(i) * kObjectStride,
                    ObjectRec(static_cast<int>(i)), kObjectStride);

    // Single-stream write, mirroring VIBE_Save_WriteGameFile's order:
    //   scenario header -> scalar block -> person/scene table -> object/building table.
    io::VfsHandle* h = io::VfsOpenFile(path, "wb");
    if (!h)
        return false;

    const guild::u8* thumb =
        world.state.thumbnail.size() == io::kThumbnailBytes
            ? world.state.thumbnail.data() : nullptr;

    bool ok = io::SaveWriteScenarioBlock(h, world.state.header, thumb)
           && io::SaveWriteScalarBlock(h, world.state.scalar);

    // Person/scene table (536-stride). The preamble carries the live count; each
    // record is written via the real per-record serializer.
    if (ok) {
        io::GameStateHeaderPreamble pre{};
        pre.marker63CC5C = 0;
        pre.id6498E8 = world.state.header.idA;
        pre.id6498EC = world.state.header.idB;
        for (int i = 0; i < 8; ++i) pre.handlerIds[i] = 0xFFFFFFFFu;
        ok = io::SaveWriteGameStateHeaderPreamble(h, pre, world.personCount);
        for (guild::u32 i = 0; ok && i < world.personCount; ++i) {
            io::PersonSceneLinks links{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
            ok = io::SaveWritePersonSceneRecord(
                h, world.personBytes.data() + static_cast<std::size_t>(i) * kPersonStride,
                links);
        }
    }

    // Object/building table (169-stride).
    if (ok)
        ok = io::SaveWritePersonRecords(h, world.objectBytes.data(), world.objectCount);

    io::VfsCloseStream(h);
    return ok;
}

// ===========================================================================
// LoadSession — read header+scalar+tables back into `out` and the live arrays.
// ===========================================================================
bool LoadSession(const char* path, std::uint32_t personCount,
                 std::uint32_t objectCount, SessionWorld& out) {
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

    // Person/scene table.
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

    // Object/building table.
    if (ok) {
        out.objectBytes.assign(static_cast<std::size_t>(objectCount) * kObjectStride, 0);
        ok = io::SaveLoadPersonRecords(h, out.objectBytes.data(), objectCount, ver);
    }

    io::VfsCloseStream(h);

    // Populate the live arrays from the loaded snapshot (the original relinks into
    // the live world; here the portable load scatters back into g_persons/g_objects).
    if (ok) {
        ResetEntityArrays();
        for (guild::u32 i = 0; i < personCount && i < (guild::u32)kPersonCapacity; ++i) {
            std::memcpy(PersonRec(static_cast<int>(i)),
                        out.personBytes.data() + static_cast<std::size_t>(i) * kPersonStride,
                        kPersonStride);
            g_personIds[i] = GetAt<guild::i32>(PersonRec(static_cast<int>(i)),
                                               sim::PersonField::kPfId);
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

// ===========================================================================
// CompareSessions — structural / byte equivalence oracle.
// ===========================================================================
EquivResult CompareSessions(const SessionWorld& a, const SessionWorld& b) {
    EquivResult r;
    r.headerEqual = std::memcmp(&a.state.header, &b.state.header,
                                sizeof(io::SaveHeader)) == 0;
    r.scalarEqual = std::memcmp(&a.state.scalar, &b.state.scalar,
                                sizeof(io::SaveScalarBlock)) == 0;
    r.personsEqual = (a.personCount == b.personCount) &&
                     (a.personBytes == b.personBytes);
    r.objectsEqual = (a.objectCount == b.objectCount) &&
                     (a.objectBytes == b.objectBytes);
    return r;
}

// ===========================================================================
// LoadRealCity — bonus guarded path through the real .cty world load driver.
// ===========================================================================
bool LoadRealCity(const char* path, std::uint32_t* outPersonCount,
                  std::uint32_t* outObjectCount) {
    if (!path)
        return false;
    guild::sim::ResetEntityArrays();
    io::WorldState world{};
    bool ok = io::LoadWorld(path, world);
    if (outPersonCount) *outPersonCount = world.cityRecCount;
    if (outObjectCount) *outObjectCount = world.objectCount;
    return ok;
}

} // namespace guild::play
