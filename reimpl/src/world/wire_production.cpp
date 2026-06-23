// See wire_production.h. Binds the three production / personnel-2 cross-cluster
// bridges (IProductionHooks, IProductionSlotHooks, PersonPersonnel2Hooks) to their
// real reconstructed leaves. Glue only — no module logic lives here.
//
// SEED-FROM-DEFAULTS: each bridge uses the house vtable-hook pattern (an installable
// IXxxHooks object whose virtuals carry inert defaults). We subclass the interface,
// override ONLY the fields with a clean reconstructed target, and inherit every
// other inert default — so unbound leaves keep their safe stubs (mirroring
// wire_election.cpp's RealCreateHooks). The shared real CommandQueue
// (RealCommandQueue()) is the SAME singleton the rest of the wiring owns.
//
// Of the three bridges only the production-SLOT tick exposes leaves with a clean
// reconstructed target:
//   * DiffMinutes            -> VIBE_GameTime_DiffMinutes (sim/gametime.h)
//   * EmitProductionFinished -> VIBE_Command_QueueRequest17 (sim/command_codec.h)
//   * MarketPrice            -> already the REAL price model by default
//                               (IProductionSlotHooks::MarketPrice forwards to
//                                Building_ComputeMarketPrice — left as-is).
// Everything else is a scene-graph query / Person-iter walk / process-global table /
// network sync / GUI widget op with no clean reconstructed leaf -> inert (per-field
// reasons documented at the install sites below and in wire_production.h).
#include "world/wire_production.h"

#include "sim/building_production.h"   // IProductionHooks / SetProductionHooks / PlayerBuildingIndex
#include "sim/production_slots.h"      // IProductionSlotHooks / SetProductionSlotHooks
#include "sim/person_personnel2.h"     // PersonPersonnel2Hooks / SetPersonPersonnel2Hooks

#include "sim/gametime.h"              // GameTimeDiffMinutes (VIBE_GameTime_DiffMinutes)
#include "sim/command_codec.h"         // QueueRequest17 (VIBE_Command_QueueRequest17)
#include "sim/real_hooks.h"            // RealCommandQueue()

namespace guild::world {

using guild::sim::GameTime;
using guild::sim::IProductionHooks;
using guild::sim::IProductionSlotHooks;
using guild::sim::PersonPersonnel2Hooks;

namespace {

guild::sim::CommandQueue& Q() { return *guild::sim::RealCommandQueue(); }

// =========================================================================
// IProductionSlotHooks — the production-timer tick leaves.
// =========================================================================
// VIBE_Inventory_TickProductionTimers (0x54f168) debits each work-slot timer by
// VIBE_GameTime_DiffMinutes(node+45, now) and, on completion, emits the "production
// finished" command VIBE_Command_QueueRequest17(-1, objId, 1, productType,
// byte_6477A1, 0). Both leaves are reconstructed; bind them to the real targets.
class RealProductionSlotHooks : public IProductionSlotHooks {
public:
    // VIBE_GameTime_DiffMinutes(a@eax, b@edx) — minutes (now - stored). The
    // reconstructed leaf takes pointers; the hook surface passes references.
    int DiffMinutes(const GameTime& stored, const GameTime& now) override {
        return guild::sim::GameTimeDiffMinutes(&stored, &now);
    }

    // VIBE_Command_QueueRequest17(-1, *(v2+3), 1, *v2, byte_6477A1, 0) — opcode 17,
    // the work-order-complete command. a1=-1, a2=objId(==personId here, the object
    // handle the tick reads from node+3), a3=1, a4=productType, a5=byte_6477A1
    // (PlayerBuildingIndex), a6=0. Emitted onto the shared real CommandQueue.
    void EmitProductionFinished(i32 personId, i16 productType) override {
        guild::sim::QueueRequest17(
            Q(), /*a1*/ -1, /*a2 objId*/ personId, /*a3*/ 1,
            /*a4 type*/ productType,
            /*a5*/ static_cast<u8>(guild::sim::PlayerBuildingIndex()),
            /*a6*/ 0);
    }

    // QueryObjectNode / FindGridSlot: VIBE_GameObject_QueryFind +
    // VIBE_Inventory_FindSlotByItemId — live scene-tree walks; not reconstructed as
    // standalone leaves (routed through hooks everywhere) -> inherit inert default.
    // MarketPrice: the base IProductionSlotHooks::MarketPrice ALREADY forwards to the
    // real Building_ComputeMarketPrice — left as the inherited (real) default.
    // NotifyProductReady: VIBE_He_SendEntityMessage (message transport) — no clean
    // reconstructed sink -> inherit inert default.
};

// =========================================================================
// IProductionHooks — per-tick slot worker/stock + standalone net sync.
// =========================================================================
// All four leaves are scene-graph / Person-iter / network side effects with no
// clean reconstructed standalone target; the bridge is wired only to force its
// real (default-backed) hook object to exist alongside its siblings, exactly as
// wire_charaction forces its shared pools to exist. No virtual is overridden, so
// every leaf keeps its module inert default:
//   * SlotWorkerOutput (ComputeSlotOutput Person-iter sum over scene work slots),
//   * SlotStoredQuantity (ComputeSlotYield's VIBE_GameObject_QueryFind stored qty):
//       live scene-tree dependent -> inert (default 0 / -1).
//   * SyncProductionState / RandomizeStockTransforms: standalone-host NETWORK sync
//       (gated on g_standaloneFlag == -1) — networking is not a pre-approved tech
//       swap (rule 6) and these have no reconstructed body -> inert (fire-and-forget).
class RealProductionHooks : public IProductionHooks {};

// =========================================================================
// process-lifetime wired hook objects (the global hook ptrs reference these for
// the life of the run, mirroring the engine's process-global hook blocks).
// =========================================================================
RealProductionSlotHooks g_slotHooks{};
RealProductionHooks     g_prodHooks{};

}  // namespace

void InstallRealProductionWiring() {
    // --- IProductionSlotHooks (production_slots.h) ---------------------------
    // Binds the production-timer tick's DiffMinutes + the completion command emit
    // (QueueRequest17) to their real reconstructed targets; MarketPrice stays the
    // inherited real default; the scene-query / message leaves stay inert.
    guild::sim::SetProductionSlotHooks(&g_slotHooks);

    // --- IProductionHooks (building_production.h) ----------------------------
    // No reconstructed leaf to bind (all four are scene-iter / network side
    // effects) — installed as the real default-backed object so the bridge is a
    // live, owned hook block rather than a dangling stub (rule 13).
    guild::sim::SetProductionHooks(&g_prodHooks);

    // --- PersonPersonnel2Hooks (person_personnel2.h) -------------------------
    // Every leaf needs state this hook surface cannot supply faithfully:
    //   * NearestDistance: byte_123D6CD per-person distance scalar table — a
    //       process-global table, not a callable leaf -> inert (default INT_MAX).
    //   * StaffBook: dword_11BC772 staff/mercenary book table — process-global,
    //       no standalone resolver -> inert (default empty book).
    //   * SumCurrencyHeld / ComputeTotalWealth / OwnedBuildingWorth: the
    //       reconstructed VIBE_Person_SumCurrencyHeld / _ComputeTotalWealth
    //       (inventory_wealth.h) take a resolved ContainerView + owned-building
    //       worth list, NOT a raw Person*; building those requires the live scene
    //       container walk (person+376) -> no clean binding from a raw record (the
    //       header itself notes this) -> inert (default 0).
    //   * OpenBuildingForActiveChar: VIBE_Dialog_OpenBuildingForActiveChar — a GUI
    //       dialog open -> inert (default 0).
    //   * QueryFirst / SyncShopChildren: VIBE_Person_QueryBegin/IterNext +
    //       scene-walk shop-child propagation — live scene-graph -> inert.
    //   * AddWidget / DestroyWidget: VIBE_Object_AddToWindow / _AddTextLabel /
    //       VIBE_Widget_DestroyByType over the process-global window/widget tables
    //       (dword_62D230 / dword_69FFB4); the (kind,x,y)->handle surface cannot
    //       carry the window slot / text / widget-array writes -> inert (GUI, same
    //       class as the skipped PersonnelGuiHooks).
    // Install nullptr to (re)assert the module's own inert default block as the
    // live binding (SetPersonPersonnel2Hooks(nullptr) restores g_defaultHooks).
    guild::sim::SetPersonPersonnel2Hooks(nullptr);
}

}  // namespace guild::world
