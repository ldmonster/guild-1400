// Wave 27 P5 — CHURCH / RELIGION (donation) vertical slice. See slice_church.h.
//
// The whole donation loop: load -> seat a donor + a church object -> click
// "Spenden" (donate) -> REAL opcode-15 command (EnqueueCmd15 0x494604) routed
// through the REAL sim::CommandQueue codec -> REAL apply (ExRemapObjectPair
// 0x496978) moving currency donor->church via the engine's object-stock leaves
// (RemoveObjektAmount 0x5863b4 / AddObjektToParent 0x5862a4, wired to the folded
// object money field +77) -> run a real game-day -> prove the (folded) world
// evolved and the run is deterministic. Every link is a CALL into an
// already-reconstructed sibling; this file defines no new world state.
//
// REUSED (extern, never redefined — ODR):
//   app::MountRealGameAssets / RealCityPath            (app/real_boot.h)
//   io::LoadWorld / VfsShutdown                        (io/save_world_load.h, io/vfs.h)
//   sim::CommandQueue (EnqueuePacket/FlushSendQueue/ExecCommands/set_handler)
//                                                      (sim/command.h)
//   sim::ExRemapObjectPair (opcode-15 apply)           (sim/command_apply2.h)
//   sim::SetRemoveObjektHook / SetAddObjektHook / ResetApply2State
//                                                      (sim/command_apply2.h)
//   sim::BuildingFindById / g_objects / ObjectRec / ResetEntityArrays
//                                                      (sim/entity.h, sim/types.h)
//   world::CityInitParameterTable                      (world/city.h)
//   play::RunGameDay / SeedGameDay / GameDayState      (play/game_day.h)
//   play::HashFullWorld                                (play/world_digest.h)
//   crt::Srand                                          (crt/rand.h)
#include "play/slice_church.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"   // kPlantBytes / kKindPlant (plant-pointer normalization)
#include "io/vfs.h"
#include "play/game_day.h"
#include "play/world_digest.h"
#include "sim/command_apply2.h"
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"  // g_buildingPersons (base digest)
// The full set of live world tables HashFullWorld folds — zeroed before a load so
// the slice's hashes are a pure function of (loaded city + seed), independent of
// any prior run that dirtied these globals in this process. (Same set
// world_digest.cpp folds; mirrors playable_slice.cpp / slice_council.cpp.)
#include "sim/building.h"
#include "sim/building_create.h"
#include "sim/building_production.h"
#include "sim/actionqueue.h"      // g_gameTick
#include "sim/command_apply5.h"   // g_sysGameTime / g_sysActivePlayer / g_currentPlayer
#include "sim/command_apply6.h"   // g_tickClock / g_tickSubCounter
#include "world/city.h"
#include "world/law.h"
#include "world/event.h"
#include "world/office.h"
#include "world/crime.h"
#include "world/relation.h"
#include "crt/rand.h"

namespace guild::play {

// ===========================================================================
// ChurchCommand::Encode — the on-wire image EnqueueCmd15 (0x494604) lays out.
// ===========================================================================
sim::CommandPacket ChurchCommand::Encode() const {
    sim::CommandPacket pkt{};
    pkt.bytes[0] = opcode;                                  // v5[0] = 0Fh
    pkt.put32(kDonationDstOff, static_cast<u32>(churchId)); // var_8C = a1 (recipient)
    pkt.put32(kDonationSrcOff, static_cast<u32>(donorId));  // var_88 = a2 (donor)
    pkt.bytes[kDonationTypeOff] = currency;                 // var_80 = a4 (byte)
    pkt.put32(kDonationAmtOff, static_cast<u32>(amount));   // var_7F = a3 (dword)
    return pkt;
}

// ===========================================================================
// Classifier — interaction -> donation command (1:1 with the dialog's emit).
// ===========================================================================
ChurchCommand ClassifyChurchInteraction(const ChurchInteraction& it) {
    ChurchCommand cmd;
    if (it.action != ChurchAction::kDonate)
        return cmd;                       // not a donate -> nothing enqueued
    if (it.amount <= 0)
        return cmd;                       // dialog only enqueues a positive donation

    cmd.issued   = true;
    cmd.opcode   = kDonationOpcode;       // 15
    cmd.churchId = it.churchId;           // a1 -> dst (+0x10)
    cmd.donorId  = it.donorAccount;       // a2 -> src (+0x14)
    cmd.amount   = it.amount;             // a3 -> +0x1D
    cmd.currency = it.currencyType;       // a4 -> +0x1C
    return cmd;
}

// ===========================================================================
// Folded money-field access + the object-stock leaf backend.
// ===========================================================================
i64 ReadObjectMoney(i32 objectId) {
    sim::ObjectRec* o = sim::BuildingFindById(objectId);
    if (!o) return 0;
    i32 v;
    std::memcpy(&v, reinterpret_cast<u8*>(o) + kChurchMoneyOff, sizeof v);
    return v;
}

namespace {

// Apply a signed currency delta to an object's money field (object+77). This is
// the deterministic net effect the REAL RemoveObjektAmount/AddObjektToParent
// produce on a currency stack: a currency-proto stock IS the entity's money. The
// folded raw bytes change so HashFullWorld observes the move.
void AddObjectMoney(i32 objectId, i32 delta) {
    sim::ObjectRec* o = sim::BuildingFindById(objectId);
    if (!o) return;
    u8* base = reinterpret_cast<u8*>(o);
    i32 cur;
    std::memcpy(&cur, base + kChurchMoneyOff, sizeof cur);
    cur += delta;
    std::memcpy(base + kChurchMoneyOff, &cur, sizeof cur);
}

// VIBE_GameObject_RemoveObjektAmount @0x5863b4 leaf backend: subtract `amount` of
// the currency from the source (donor) account. Returns nonzero == success (the
// real function returns 1 on a successful subtraction; ExRemapObjectPair short-
// circuits the whole apply if it returns 0).
int DonationRemoveLeaf(i32 containerId, i32 /*proto*/, i32 amount) {
    AddObjectMoney(containerId, -amount);
    return 1;
}
// VIBE_GameObject_AddObjektToParent @0x5862a4 leaf backend: add `amount` of the
// currency to the recipient (church). Returns nonzero == success.
int DonationAddLeaf(i32 containerId, i32 /*proto*/, i32 amount) {
    AddObjectMoney(containerId, +amount);
    return 1;
}

// Adapter: the CommandQueue handler signature is void(queue, pkt, ack); the real
// opcode-15 apply is int ExRemapObjectPair(pkt, ack). Route through it.
void ChurchCmdHandler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt,
                      sim::AckEntry* ack) {
    sim::ExRemapObjectPair(pkt, ack);    // gilde.exe 0x496978 — the real handler
}

} // namespace

void InstallChurchCommandHandler(sim::CommandQueue& q) {
    // Point the engine's deferred object-stock leaves at the folded money field.
    sim::SetRemoveObjektHook(&DonationRemoveLeaf);   // RemoveObjektAmount leaf
    sim::SetAddObjektHook(&DonationAddLeaf);         // AddObjektToParent leaf
    q.set_handler(kDonationOpcode, &ChurchCmdHandler);
}

int SeatChurchObject(i32 objectId, i32 money) {
    using namespace guild::sim;
    for (int i = 0; i < kObjectCapacity; ++i) {
        if (g_objects[i].alive != 0)
            continue;
        g_objects[i].alive = 2;          // kind-2 (the dialog's QueryFind kind)
        g_objects[i].id    = objectId;
        u8* base = reinterpret_cast<u8*>(&g_objects[i]);
        std::memcpy(base + kChurchMoneyOff, &money, sizeof money);
        return i;
    }
    return -1;
}

// ===========================================================================
// Shared helpers (mirror playable_slice.cpp / slice_council.cpp for hash
// reproducibility).
// ===========================================================================
namespace {

// Zero EVERY live world table HashFullWorld folds, to a clean blank slate. Called
// BEFORE io::LoadWorld so loaded-city tables repopulate identically each run and
// every folded table starts blank in every run -> the whole-world hash is a pure
// function of (city + seed) across reruns in one process.
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

// Seed the economy parameter table + RNG to a deterministic baseline.
void ResetWorldBaseline(std::uint32_t baselineSeed) {
    crt::Srand(baselineSeed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
}

// Stabilize the kind-30 plantmap heap-pointer column (a fresh heap ptr per load,
// which would make the raw-byte digest non-reproducible). Mirrors
// playable_slice.cpp::NormalizePlantPointers.
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

// Run the CLICK->COMMAND donation step over the live world: build the op-15
// packet, enqueue it through the REAL CommandQueue, flush + exec (-> the real
// ExRemapObjectPair apply), recording the folded money fields before/after.
void RunChurchCommand(ChurchSliceResult& r, const ChurchInteraction& it,
                      sim::CommandQueue& q) {
    r.churchMoneyBefore = ReadObjectMoney(it.churchId);
    r.donorMoneyBefore  = ReadObjectMoney(it.donorAccount);

    r.command = ClassifyChurchInteraction(it);
    if (!r.command.issued) {
        r.churchMoneyAfter = r.churchMoneyBefore;
        r.donorMoneyAfter  = r.donorMoneyBefore;
        return;
    }

    InstallChurchCommandHandler(q);
    sim::CommandPacket pkt = r.command.Encode();
    u32 before = q.send_count();
    i32 slot = q.EnqueuePacket(pkt);
    r.ringSlot = slot;
    r.enqueued = (slot >= 0) && (q.send_count() != before);
    if (r.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();             // -> ChurchCmdHandler -> ExRemapObjectPair
        r.applied = true;
    }
    r.churchMoneyAfter = ReadObjectMoney(it.churchId);
    r.donorMoneyAfter  = ReadObjectMoney(it.donorAccount);
}

} // namespace

// ===========================================================================
// RunChurchSlice — the whole donation loop over a REAL city.
// ===========================================================================
ChurchSliceResult RunChurchSlice(shim::IFileSystem* fs, const std::string& gameDir,
                                 const std::string& cityName,
                                 const ChurchInteraction& it,
                                 std::uint32_t seed) {
    ChurchSliceResult r;
    if (!fs)
        return r;

    // --- step: LOAD (mount real assets + io::LoadWorld into the live arrays) --
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) {
        io::VfsShutdown();
        return r;
    }
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    sim::ResetApply2State();             // blank the apply2 hooks/tables
    const std::string cityPath = app::RealCityPath(cityName);
    io::WorldState world{};
    r.loaded = io::LoadWorld(cityPath.c_str(), world);
    if (!r.loaded) {
        io::VfsShutdown();
        return r;
    }
    r.personCount = world.cityRecCount;
    r.objectCount = world.objectCount;
    NormalizePlantPointers(world.objectCount);

    // --- deterministic baseline + the seated donor + church ------------------
    ResetWorldBaseline(seed);
    SeatChurchObject(it.donorAccount, /*money=*/it.amount * 4 + 1000);
    SeatChurchObject(it.churchId,     /*money=*/0);

    // --- HashFullWorld after load (anchor RNG so the hash is reproducible) ----
    crt::Srand(seed);
    r.hashAfterLoad = HashFullWorld();

    // --- step: CLICK -> OPCODE-15 COMMAND -> REAL APPLY ----------------------
    {
        sim::CommandQueue q;
        q.Init();
        RunChurchCommand(r, it, q);
    }
    crt::Srand(seed);
    r.hashAfterCommand = HashFullWorld();

    // --- step: GAME-DAY (the REAL per-day cascade; seeded for determinism) ----
    crt::Srand(seed);
    GameDayState st = SeedGameDay(seed);
    GameDayDeltas d = RunGameDay(seed, st);
    r.daySteps = d.stepsRun;
    crt::Srand(seed);
    r.hashAfterDay = HashFullWorld();

    io::VfsShutdown();
    return r;
}

// ===========================================================================
// RunChurchStepsSynthetic — the same step sequence on a synthetic live world.
// ===========================================================================
int RunChurchStepsSynthetic(std::uint32_t seed, const ChurchInteraction& it,
                            ChurchStepHash* out, int cap,
                            i64* outChurchBefore, i64* outChurchAfter) {
    if (!out || cap < 3)
        return 0;

    // --- kSeed: blank slate + deterministic baseline + donor & church --------
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    sim::ResetApply2State();
    ResetWorldBaseline(seed);
    SeatChurchObject(it.donorAccount, it.amount * 4 + 1000);
    SeatChurchObject(it.churchId,     0);
    crt::Srand(seed);
    std::uint64_t hSeed = HashFullWorld();
    if (outChurchBefore) *outChurchBefore = ReadObjectMoney(it.churchId);

    // --- kCommand: build op-15 + enqueue + exec the real ExRemapObjectPair ---
    {
        sim::CommandQueue q;
        q.Init();
        InstallChurchCommandHandler(q);
        ChurchCommand cmd = ClassifyChurchInteraction(it);
        if (cmd.issued) {
            sim::CommandPacket pkt = cmd.Encode();
            if (q.EnqueuePacket(pkt) >= 0 && q.standalone()) {
                q.FlushSendQueue();
                q.ExecCommands();
            }
        }
    }
    crt::Srand(seed);
    std::uint64_t hCmd = HashFullWorld();
    if (outChurchAfter) *outChurchAfter = ReadObjectMoney(it.churchId);

    // --- kDay: the real per-day cascade --------------------------------------
    {
        crt::Srand(seed);
        GameDayState st = SeedGameDay(seed);
        RunGameDay(seed, st);
    }
    crt::Srand(seed);
    std::uint64_t hDay = HashFullWorld();

    out[0] = {ChurchStep::kSeed,    hSeed, false};
    out[1] = {ChurchStep::kCommand, hCmd,  hCmd != hSeed};
    out[2] = {ChurchStep::kDay,     hDay,  hDay != hCmd};
    return 3;
}

} // namespace guild::play
