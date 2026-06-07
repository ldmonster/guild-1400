// Wave 27 PLAY P5 — BUILDINGS / PRODUCTION vertical slice implementation.
// See slice_production.h for full grounding.
//
// The production analogue of playable_slice.cpp's conquer slice. Every link is a
// CALL into an already-reconstructed sibling:
//   play::PickAndResolveSceneEntity / BuildingDialogKindToActionGroup / BuildingDialogFsm
//   sim::QueueRequestSlotReset28 (the REAL opcode-28 production-order builder)
//   sim::Building_RecalcAllProduction / RunProductionTick (the REAL production tick)
//   play::RunEconomyTurn (the REAL per-day economy passes)
//   play::HashFullWorld (folds g_prodStore / g_prodSchedules)
#include "play/slice_production.h"

#include <cstring>
#include <vector>

#include "app/real_boot.h"
#include "io/save_world_load.h"
#include "io/save_person.h"            // kPlantBytes / kKindPlant
#include "io/vfs.h"
#include "play/scene_pick.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "sim/command.h"
#include "sim/command_inherit.h"       // QueueRequestSlotReset28 / SlotResetScratch
#include "sim/command_pending.h"       // PendingState
#include "sim/entity.h"
#include "sim/types.h"
#include "sim/building_lifecycle.h"    // g_buildingPersons
#include "sim/building.h"              // g_buildingTypes / g_buildingTypesLoaded
#include "sim/building_create.h"       // g_buildingNextId
#include "sim/building_production.h"   // g_prodStore/_Schedules, the tick, scene tables
#include "sim/actionqueue.h"           // g_gameTick
#include "sim/command_apply5.h"        // g_sysGameTime, g_currentPlayer/g_sysActivePlayer
#include "sim/command_apply6.h"        // g_tickClock, g_tickSubCounter
#include "world/city.h"                // g_cities, g_goods, g_capDivisor, totals
#include "world/law.h"                 // g_lawTable
#include "world/event.h"               // g_eventTable, g_eventTableCount, g_missionLcgState
#include "world/office.h"              // g_officeHolders
#include "world/crime.h"               // g_crimeTable
#include "world/relation.h"            // g_relationMatrix
#include "crt/rand.h"

namespace guild::play {

namespace {

// Zero EVERY live world table HashFullWorld folds (mirror of playable_slice's
// ZeroWorldGlobals) so the slice hashes are a pure function of (loaded city + seed),
// reproducible across reruns in one process.
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

void ResetWorldBaseline(std::uint32_t baselineSeed) {
    crt::Srand(baselineSeed);
    world::CityInitParameterTable(100.0f);
    world::g_capDivisor = 0.0f;
}

// io::LoadWorld points each kind-30 (plant) object's +113 plantmap ptr at a fresh
// heap allocation; normalize them onto one shared owned scratch so the raw-byte
// object digest is reproducible across reruns (mirror of playable_slice).
void NormalizePlantPointers(std::uint32_t objectCount) {
    using namespace guild::sim;
    static std::vector<guild::u8> plantScratch(io::kPlantBytes, 0);
    guild::u8* scratch = plantScratch.data();
    for (std::uint32_t i = 0; i < objectCount && i < (std::uint32_t)kObjectCapacity; ++i) {
        guild::u8* r = reinterpret_cast<guild::u8*>(&g_objects[i]);
        if (r[0] != io::kKindPlant) continue;
        std::memcpy(r + 113, &scratch, sizeof scratch);
    }
}

// -------------------------------------------------------------------------------
// Seed a faithful one-slot WORKSHOP into the production tables so the REAL tick has
// something to integrate. The loaded city does not populate g_prodStore (that table
// is built by the live RecalcAllProduction preamble from scene objects, outside the
// slice's scope); we seed the row the same shape the tick reads:
//   * a SceneTypeDef for `prot`: kind 6 (a producible room good, not 23/37 which the
//     tick treats as terminal storage), a nonzero cachedPrice so the REAL
//     Building_ComputeMarketPrice returns a deterministic positive market price.
//   * a ProdSchedule with hasProduction + a rising input/output keyframe curve, so
//     InterpCurve yields a nonzero in/out value across the day.
//   * the slot table header (inScale/outScale per-mille) for the building row.
// This is setup state (the world the tick acts on), not reconstructed rule logic.
// -------------------------------------------------------------------------------
void SeedWorkshop(int buildingIndex, i16 prot) {
    using namespace guild::sim;
    if (buildingIndex < 0 || buildingIndex >= kProdBuildingCapacity) return;
    if (prot < 0 || prot >= kSceneTypeCapacity) return;

    // SceneTypeDef for the product prot (drives market price + the slot kind gate).
    SceneTypeDef& td = g_sceneTypes[prot];
    std::memset(&td, 0, sizeof td);
    td.kind        = 6;       // producible room good (not 23/37)
    td.subtype     = 0;
    td.baseValue   = 4000;
    td.priceField  = 1;
    td.compType    = 0;       // leaf good (no component recursion)
    td.divisor     = 1;
    td.cachedPrice = 700;     // market price = 32*700*100*0.01 = 22400 (>= 512)
    g_sceneTypesLoaded = true;

    // Production schedule: a rising curve so the tick's inValue/outValue move.
    ProdSchedule& sc = g_prodSchedules[buildingIndex];
    std::memset(&sc, 0, sizeof sc);
    sc.hasProduction = 1;
    sc.input[0]  = {0,    0};
    sc.input[1]  = {2000, 1000};
    sc.output[0] = {0,    0};
    sc.output[1] = {2000, 1000};

    // Slot-table header: per-mille input/output scale.
    ProdBuilding pb = ProdBuildingAt(buildingIndex);
    pb.inScale()  = 1000;
    pb.outScale() = 1000;
}

} // namespace

// ---------------------------------------------------------------------------
// Classification — (kind, menu item, prot) -> production command.
// ---------------------------------------------------------------------------
bool GroupIsProductionWorkshop(BuildingActionGroup g) {
    switch (g) {
        case BuildingActionGroup::kSmith:       // kind 116 SmithProduction
        case BuildingActionGroup::kCarpenter:   // kind 133 CarpenterProduction
        case BuildingActionGroup::kStonemason:  // kind 155 StonemasonProduction
        case BuildingActionGroup::kProduction:  // kind 247 Location_ProductionContactLoop
            return true;
        default:
            return false;
    }
}

ProductionCommand ClassifyProductionAction(int kind, ProductionMenuItem item, i16 prot) {
    ProductionCommand cmd;
    cmd.group = BuildingDialogKindToActionGroup(kind);
    if (item != ProductionMenuItem::kWriteProduction)
        return cmd;                          // non-mutating entry -> no order
    if (!GroupIsProductionWorkshop(cmd.group))
        return cmd;                          // this dialog is not a production workshop
    cmd.issued = true;
    cmd.opcode = kProductionCmdOpcode;       // 28
    cmd.prot   = prot;
    return cmd;
}

// ---------------------------------------------------------------------------
// Opcode-28 production-order APPLY (installable hook; inert default).
// ---------------------------------------------------------------------------
int ProductionApplyOrder(int buildingIndex, i16 prot) {
    using namespace guild::sim;
    if (buildingIndex < 0 || buildingIndex >= kProdBuildingCapacity) return -1;
    ProdBuilding pb = ProdBuildingAt(buildingIndex);
    // Find the first free slot (protPacked high word == 0) and write the ordered
    // prot into it + activate it (the production-state effect a committed order has:
    // the building begins producing `prot`).
    for (int s = 0; s < kProdSlotsPerBuilding; ++s) {
        i16 cur = static_cast<i16>(pb.slotProtPacked(s) >> 16);
        if (cur == 0) {
            pb.slotProtPacked(s) = static_cast<i32>(static_cast<u32>(prot) << 16);
            pb.slotActive(s)     = 1;
            return s;
        }
    }
    return -1;
}

namespace {
ProductionApplyHook g_applyHook = nullptr;
int RunApply(int buildingIndex, i16 prot) {
    return g_applyHook ? g_applyHook(buildingIndex, prot)
                       : ProductionApplyOrder(buildingIndex, prot);
}

// Per-queue side channel: the (buildingIndex, prot) IssueProductionClick is about to
// apply (the opcode-28 wire packet carries only the opcode; the order body travels
// via the StagePendingBlock side-channel, which the receiver reassembles — modeled
// here as this side channel so the handler knows what to apply).
struct PendingOrder { int building = 0; i16 prot = 0; int slot = -1; bool valid = false; };
PendingOrder g_pendingOrder;

void ProductionCmdHandler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt,
                          sim::AckEntry* /*ack*/) {
    if (pkt.opcode() != kProductionCmdOpcode) return;
    if (!g_pendingOrder.valid) return;
    g_pendingOrder.slot  = RunApply(g_pendingOrder.building, g_pendingOrder.prot);
    g_pendingOrder.valid = false;
}
} // namespace

void SetProductionApplyHook(ProductionApplyHook hook) { g_applyHook = hook; }

void InstallProductionCommandHandler(sim::CommandQueue& q) {
    q.set_handler(kProductionCmdOpcode, &ProductionCmdHandler);
}

// ---------------------------------------------------------------------------
// The bridge: click -> workshop dialog -> opcode-28 order -> apply.
// ---------------------------------------------------------------------------
ProductionClickResult IssueProductionClick(sim::CommandQueue& q,
                                           const CityViewCamera& cam, float sx, float sy,
                                           const ScenePickObject* objects, int count,
                                           float pickRadius,
                                           const std::function<int(i32 id)>& kindOf,
                                           int buildingIndex, i16 prot,
                                           ProductionMenuItem item) {
    ProductionClickResult out;

    // (1) RESOLVE the picked building via the REAL scene pick.
    int resolveKind = 0;
    ScenePickResult pick =
        PickAndResolveSceneEntity(cam, sx, sy, objects, count, pickRadius, &resolveKind);
    out.pickIndex = pick.index;
    out.pickId    = pick.id;
    if (pick.index < 0)
        return out;                          // clicked empty space

    int kind = kindOf ? kindOf(pick.id) : 0;
    out.buildingKind = kind;
    out.group        = BuildingDialogKindToActionGroup(kind);

    // (2) OPEN the workshop contact dialog (CheckEntry gate + kind dispatch).
    BuildingDialogFsm fsm;
    out.opened = fsm.Open(pick.id, kind);
    if (!out.opened)
        return out;

    // (3) CLASSIFY the production order (the golden).
    out.command = ClassifyProductionAction(kind, item, prot);
    if (!out.command.issued)
        return out;                          // non-write item / not a workshop

    // (4) STAGE the order (the side channel the opcode-28 body would carry) + BUILD
    //     + ENQUEUE the REAL opcode-28 packet through the REAL CommandQueue codec.
    g_pendingOrder = PendingOrder{buildingIndex, prot, -1, true};
    sim::PendingState pending{};
    sim::SlotResetScratch scratch{};
    // Mirror the BuildProductionWindow v60 record's observable fields: flag v69[0]=1,
    // the product prot v70 = HIWORD slot, the building id v73. (The wire packet itself
    // is opcode-only; these populate the staged 248-byte body.)
    scratch.words[0]  = 1u;                                   // v69[0] = 1
    scratch.words[1]  = static_cast<u32>(prot) << 16;          // v70 = prot (HIWORD)
    scratch.words[2]  = static_cast<u32>(buildingIndex);       // owner/building id

    u32 sendBefore = q.send_count();
    i32 slot = sim::QueueRequestSlotReset28(q, pending, scratch, /*a2Unused=*/0);
    out.enqueued = (slot >= 0) && (q.send_count() != sendBefore);
    out.ringSlot = slot;

    // (5) APPLY — flush + execute so the opcode-28 handler runs the apply.
    if (out.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();
        out.applied     = true;
        out.appliedSlot = g_pendingOrder.slot;
    }
    g_pendingOrder.valid = false;
    return out;
}

// ===========================================================================
// RunProductionSlice — the whole production loop over a REAL city.
// ===========================================================================
ProductionSliceResult RunProductionSlice(shim::IFileSystem* fs,
                                         const std::string& gameDir,
                                         const std::string& cityName,
                                         int prodBuilding, i16 prot,
                                         std::uint32_t econSeed) {
    ProductionSliceResult r;
    r.prodBuilding = prodBuilding;
    if (!fs) return r;

    // --- LOAD ---
    app::RealGameAssets assets =
        app::MountRealGameAssets(fs, gameDir, "Gilde.INI", {}, /*caseInsensitive=*/false);
    if (!assets.vfsBound) { io::VfsShutdown(); return r; }
    ZeroWorldGlobals();
    sim::ResetEntityArrays();
    const std::string cityPath = app::RealCityPath(cityName);
    io::WorldState world{};
    r.loaded = io::LoadWorld(cityPath.c_str(), world);
    if (!r.loaded) { io::VfsShutdown(); return r; }
    r.personCount = world.cityRecCount;
    r.objectCount = world.objectCount;
    NormalizePlantPointers(world.objectCount);
    ResetWorldBaseline(econSeed);

    // Seed the workshop row the REAL production tick will integrate.
    SeedWorkshop(prodBuilding, prot);

    crt::Srand(econSeed);
    r.hashAfterLoad = HashFullWorld();

    // --- CLICK -> production order -> apply (writes prot into the slot table) ---
    {
        sim::CommandQueue q;
        q.Init();
        q.set_standalone(true);
        InstallProductionCommandHandler(q);
        SetProductionApplyHook(nullptr);    // use the inert-default apply

        // The slice resolves the click directly onto the production building (the
        // pick roster is one object at the camera centre); the e2e supplies the real
        // building's KIND. Here we drive it as a generic production workshop (kind 247).
        ScenePickObject obj; obj.id = (prodBuilding + 1); obj.pos[0] = 0; obj.pos[1] = 0; obj.pos[2] = 0;
        float eye[3] = {0,0,0};
        CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 64, 64);
        auto kindOf = [](i32) { return 247; };   // generic production location
        ProductionClickResult pc =
            IssueProductionClick(q, cam, obj.pos[0], obj.pos[2], &obj, 1, 64.0f,
                                 kindOf, prodBuilding, prot, ProductionMenuItem::kWriteProduction);
        r.commandIssued   = pc.command.issued;
        r.commandEnqueued = pc.enqueued;
        r.commandOpcode   = pc.command.opcode;
        r.commandProt     = pc.command.prot;
        r.appliedSlot     = pc.appliedSlot;
    }
    r.hashAfterCommand = HashFullWorld();

    // --- PRODUCTION TICK (real) + GAME-DAY (real economy passes) ---
    {
        using namespace guild::sim;
        // Read the ordered slot's production observables before the tick.
        ProdBuilding pb = ProdBuildingAt(prodBuilding);
        int s = (r.appliedSlot >= 0) ? r.appliedSlot : 0;
        r.yieldBefore    = pb.slotYield(s);
        r.smoothInBefore = pb.slotSmoothIn(s);

        // The REAL production tick over the ordered building row (interpolates the
        // schedule curves + refreshes every slot's smoothed-in / output / yield).
        // Building_RunProductionTick is RecalcAllProduction's per-building body (it
        // gates on g_prodSchedules[0]; we tick the targeted row directly, which is
        // the call the live engine makes for an active production building).
        sim::Building_RunProductionTick(prodBuilding, /*nowDay=*/1, /*nowMinute=*/1500);

        r.yieldAfter    = pb.slotYield(s);
        r.smoothInAfter = pb.slotSmoothIn(s);

        // The REAL per-day city economy passes.
        crt::Srand(econSeed);
        EconomyTurnState st = SeedEconomyTurnState();
        st.day = 0;
        r.treasuryBefore = st.treasury;
        EconomyTurnDeltas d = RunEconomyTurn(st);
        r.economyPasses = d.passesRun;
        r.treasuryAfter = st.treasury;
    }
    r.hashAfterDay = HashFullWorld();

    io::VfsShutdown();
    return r;
}

// ===========================================================================
// RunProductionStepsSynthetic — the step sequence on a synthetic world.
// ===========================================================================
int RunProductionStepsSynthetic(std::uint32_t seed, int prodBuilding, i16 prot,
                                std::uint32_t econSeed,
                                ProductionStepHash* out, int cap) {
    using namespace guild::sim;
    if (!out || cap < 4) return 0;

    // --- step 0: LOAD (synthetic) ---
    ZeroWorldGlobals();
    ResetEntityArrays();
    crt::Srand(seed);
    // A few live objects so the entity digest is nonempty.
    for (int i = 0; i < 4 && i < kObjectCapacity; ++i) {
        g_objects[i].alive = 1;
        g_objects[i].id = 100 + i;
    }
    g_sceneArrayLoaded = true;
    SeedWorkshop(prodBuilding, prot);
    ResetWorldBaseline(econSeed);
    std::uint64_t hLoad = HashFullWorld();

    // --- step 1: COMMAND (production order applied -> writes prot into a slot) ---
    {
        sim::CommandQueue q;
        q.Init();
        q.set_standalone(true);
        InstallProductionCommandHandler(q);
        SetProductionApplyHook(nullptr);
        ScenePickObject obj; obj.id = prodBuilding + 1;
        float eye[3] = {0,0,0};
        CityViewCamera cam = MakeCityViewCamera(eye, 1.0f, 64, 64);
        auto kindOf = [](i32) { return 247; };
        IssueProductionClick(q, cam, 0.0f, 0.0f, &obj, 1, 64.0f, kindOf,
                             prodBuilding, prot, ProductionMenuItem::kWriteProduction);
    }
    std::uint64_t hCmd = HashFullWorld();

    // --- step 2: TICK (real production tick mutates slot yield/smoothed-in) ---
    sim::Building_RunProductionTick(prodBuilding, /*nowDay=*/1, /*nowMinute=*/1500);
    std::uint64_t hTick = HashFullWorld();

    // --- step 3: DAY (real economy passes mutate economy + RNG) ---
    {
        crt::Srand(econSeed);
        EconomyTurnState st = SeedEconomyTurnState();
        st.day = 0;
        RunEconomyTurn(st);
    }
    std::uint64_t hDay = HashFullWorld();

    out[0] = {hLoad, false};
    out[1] = {hCmd,  hCmd  != hLoad};
    out[2] = {hTick, hTick != hCmd};
    out[3] = {hDay,  hDay  != hTick};
    return 4;
}

} // namespace guild::play
