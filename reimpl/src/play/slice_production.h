#pragma once
// Wave 27 PLAY P5 — the BUILDINGS / PRODUCTION vertical slice (namespace
// guild::play).
//
// This is the production sibling of playable_slice.cpp's CONQUER slice: a faithful
//   click -> dialog/HUD -> REAL command -> sim effect -> render
// chain for a production/workshop building. Where playable_slice issued an opcode-80
// unit order, this slice issues the REAL production order the game emits when the
// player commits a "write production" job inside a workshop, then runs the REAL
// per-building production tick + one game-day and proves the production / treasury /
// stock state genuinely evolved (and that the whole run is deterministic).
//
// GROUNDING (decompiled this wave — the live production interaction + command path):
//   * VIBE_Building_EnterAndDispatch @0x51defc dispatches a building click by KIND
//     to a contact-menu loop. The production workshops route to:
//        0x74(116) -> VIBE_ContactMenu_SmithProduction      @0x513954
//        133       -> VIBE_ContactMenu_CarpenterProduction
//        0x9B(155) -> VIBE_ContactMenu_StonemasonProduction
//        0xF7(247) -> VIBE_Location_ProductionContactLoop    @0x52388c
//     (the same kind->group ladder play::interact_building already reconstructs as
//      BuildingDialogKindToActionGroup; this slice REUSES that ladder).
//   * VIBE_Location_ProductionContactLoop @0x52388c registers the contact entries:
//        "contact_PRODUKTION_SCHREIBEN"  -> VIBE_TradePanel_BuildProductionWindow
//        "contact_LAGER" / "contact_TRANSPORT" / staff / master-cert.
//     The PRODUKTION_SCHREIBEN ("write production") entry opens
//     VIBE_TradePanel_BuildProductionWindow @0x5087bc, the panel where the player
//     commits a production job. On commit that panel builds a 248-byte order record
//     (the stack scratch `v60`: flag v69[0]=1, packed game-time qword v66, the
//     product prot v70 = HIWORD(slotEntry), owner/building ids v72/v73) and emits
//     it through:
//        VIBE_Command_QueueRequestSlotReset28 @0x4948c8  (the REAL production-order
//          opcode 28: packet carries only the opcode byte; the 248-byte body travels
//          via VIBE_Command_StagePendingBlock(0xF8,...) — reconstructed as
//          sim::QueueRequestSlotReset28 in command_inherit.cpp).
//   * The per-DAY production simulation is the REAL production tick engine
//     (building_production.cpp): VIBE_Building_RecalcAllProduction @0x583c3c ->
//     VIBE_Building_RunProductionTick @0x5847a0, which interpolates each building's
//     input/output schedule curves and refreshes every slot's smoothed-input /
//     output / yield over g_prodStore. We run it for the order's building, then run
//     one game-day of the city economy (play::RunEconomyTurn).
//
// What is REAL vs INERT in this slice:
//   REAL siblings wired:
//     - play::PickAndResolveSceneEntity (scene_pick) -> REAL GameObjectResolveEntityById
//     - play::BuildingDialogKindToActionGroup / BuildingDialogFsm (interact_building)
//     - sim::QueueRequestSlotReset28 (command_inherit) -> the REAL opcode-28 builder
//       (StagePendingBlock side-channel) over the REAL sim::CommandQueue codec
//     - sim::Building_RunProductionTick / Building_RecalcAllProduction
//       (building_production) -> the REAL per-building production tick over g_prodStore
//     - play::RunEconomyTurn (turn_economy) -> the REAL per-day Amt economy passes
//     - play::HashFullWorld (world_digest) -> folds g_prodStore / g_prodSchedules
//   INERT default (hooked, said so):
//     - the opcode-28 APPLY (what the receiver does with a reassembled production
//       order). The original applies it inside the GUI/net receive path; here the
//       apply is supplied as an installable hook with an inert-by-default
//       reconstruction (ProductionApplyOrder) that writes the ordered prot into the
//       building's first free production slot and activates it — the production-state
//       effect a committed order produces, folded by HashFullWorld.
//
// ADDITIVE: new files only. No edits to interact_building.cpp / building_production.cpp /
// turn_economy.cpp / playable_slice.cpp / wiring.cpp.
#include <cstdint>
#include <string>

#include "guild/common/types.h"
#include "play/interact_building.h"   // BuildingActionGroup / BuildingDialogFsm
#include "shim/IFileSystem.h"

namespace guild::play {

// ===========================================================================
// The production menu item the player commits inside a workshop's contact menu.
// Mirrors VIBE_Location_ProductionContactLoop's registered entries; only
// kWriteProduction ("contact_PRODUKTION_SCHREIBEN") emits an order command.
// ===========================================================================
enum class ProductionMenuItem {
    kNone            = 0,   // a non-mutating entry (info / storage panel)
    kWriteProduction = 1,   // "PRODUKTION_SCHREIBEN" -> commit a production job
};

// The opcode the committed production order rides — VIBE_Command_QueueRequestSlotReset28.
inline constexpr u8 kProductionCmdOpcode = 28;     // 0x1C
// 248-byte StagePendingBlock body size (StagePendingBlock(0xF8, &scratch)).
inline constexpr u32 kProductionBodyBytes = 0xF8;  // 248

// ===========================================================================
// The classified production command (the unit golden: kind + menu item -> command).
// ===========================================================================
struct ProductionCommand {
    bool issued = false;            // a production order results from this (kind,item)
    u8   opcode = 0;                // wire opcode (kProductionCmdOpcode == 28 when issued)
    i16  prot   = 0;                // the product prot the order commits to produce
    BuildingActionGroup group = BuildingActionGroup::kNone;
};

// Does this building action group own a production workshop (a PRODUKTION_SCHREIBEN
// contact entry)? Smith / carpenter / stonemason / the generic production location.
bool GroupIsProductionWorkshop(BuildingActionGroup g);

// 1:1 classifier: a production-building KIND code + a chosen menu item + the product
// prot the player picked -> the concrete production ProductionCommand. A non-write
// item, or a kind whose dialog is not a production workshop, yields {issued=false}.
ProductionCommand ClassifyProductionAction(int kind, ProductionMenuItem item, i16 prot);

// ===========================================================================
// Opcode-28 production-order APPLY — installable hook, inert-by-default. Writes the
// ordered prot into the building's first free production slot (g_prodStore) and
// activates it. `buildingIndex` is the production-table building row; `prot` the
// product. Returns the slot it wrote (or -1 if no free slot / table not usable).
// ===========================================================================
using ProductionApplyHook = int(*)(int buildingIndex, i16 prot);
void SetProductionApplyHook(ProductionApplyHook hook);

// The inert default (the one installed when no hook is set). Public so a test can
// assert its behavior directly. Writes prot into building's first free slot.
int ProductionApplyOrder(int buildingIndex, i16 prot);

// Install the opcode-28 production-order handler on a CommandQueue (so a flushed +
// executed opcode-28 packet routes into the active apply hook). The slice records
// the (buildingIndex, prot) the order targets in a per-queue side channel set by
// IssueProductionClick; idempotent.
void InstallProductionCommandHandler(sim::CommandQueue& q);

// ===========================================================================
// The result of one production click -> dialog -> command -> apply cycle.
// ===========================================================================
struct ProductionClickResult {
    bool                opened       = false;  // the workshop dialog opened
    int                 pickIndex    = -1;     // scene_pick index (-1 == empty space)
    i32                 pickId       = 0;      // resolved entity id
    int                 buildingKind = 0;      // the building's KIND code
    BuildingActionGroup group        = BuildingActionGroup::kNone;
    ProductionCommand   command{};             // the classified production command
    bool                enqueued     = false;  // the opcode-28 packet hit the send ring
    i32                 ringSlot     = -1;      // EnqueuePacket ring slot
    bool                applied      = false;   // FlushSendQueue+ExecCommands ran
    int                 appliedSlot  = -1;      // production slot the apply wrote (-1 none)
};

// ===========================================================================
// The bridge: resolve a world-view click into a production building, open its
// contact dialog, commit a "write production" order for `prot`, build+enqueue the
// REAL opcode-28 packet through the REAL CommandQueue codec, and apply it so the
// building's production slot table mutates.
//
// `buildingIndex` is the production-table row the order targets (the building the
// click resolves to, mapped to its g_prodStore row by the caller — tests pass it
// directly, the e2e derives it). `kindOf` supplies the picked building's KIND code.
// `q` is the live CommandQueue (handler installed).
// ===========================================================================
ProductionClickResult IssueProductionClick(sim::CommandQueue& q,
                                           const CityViewCamera& cam, float sx, float sy,
                                           const ScenePickObject* objects, int count,
                                           float pickRadius,
                                           const std::function<int(i32 id)>& kindOf,
                                           int buildingIndex, i16 prot,
                                           ProductionMenuItem item);

// ===========================================================================
// Full-slice run result (the production analogue of SliceResult).
// ===========================================================================
struct ProductionSliceResult {
    bool loaded = false;
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;

    // --- the scripted production click -> command ---
    bool commandIssued   = false;   // a production order was classified
    bool commandEnqueued = false;   // the opcode-28 packet hit the send ring
    int  commandOpcode   = 0;       // the wire opcode (28 when issued)
    i16  commandProt     = 0;       // the product prot ordered
    int  appliedSlot     = -1;      // production slot the apply wrote
    int  prodBuilding    = 0;       // the production-table building row driven

    // --- the per-day production tick + economy day ---
    int  economyPasses   = 0;       // Amt passes the day ran

    // --- the real production-state observables (before/after the tick) ---
    float yieldBefore    = 0.0f;    // slot yield (g_prodStore slot+0x38) pre-tick
    float yieldAfter     = 0.0f;    // ... post-tick
    float smoothInBefore = 0.0f;    // slot smoothed-input (slot+0x20) pre-tick
    float smoothInAfter  = 0.0f;    // ... post-tick
    i64   treasuryBefore = 0;       // economy treasury pre-day
    i64   treasuryAfter  = 0;       // ... post-day

    // --- determinism oracle (the world hashes, fold order = loop order) ---
    std::uint64_t hashAfterLoad    = 0;  // HashFullWorld after load
    std::uint64_t hashAfterCommand = 0;  // ... after the production order applied
    std::uint64_t hashAfterDay     = 0;  // ... after the production tick + game-day

    bool worldChanged()        const { return hashAfterLoad != hashAfterDay; }
    bool commandChangedWorld() const { return hashAfterLoad != hashAfterCommand; }
    bool productionEvolved()   const { return yieldAfter != yieldBefore
                                           || smoothInAfter != smoothInBefore; }

    bool ok() const { return loaded && commandIssued && worldChanged(); }
};

// ===========================================================================
// RunProductionSlice — the whole production loop over a REAL city:
//   1. mount `gameDir` + io::LoadWorld the city into the live arrays,
//   2. seed the production-table row `prodBuilding` so it has a runnable schedule
//      (the loaded city does not populate g_prodStore; the slice seeds a faithful
//      one-slot workshop the REAL tick can integrate), HashFullWorld -> hashAfterLoad,
//   3. issue the scripted production click -> opcode-28 order -> apply (writes the
//      ordered prot into the building's slot), HashFullWorld -> hashAfterCommand,
//   4. run the REAL production tick (Building_RecalcAllProduction) + one game-day
//      (RunEconomyTurn), HashFullWorld -> hashAfterDay,
//   5. assert the production / treasury state evolved AND the world changed.
//
// The VFS is bound for the run and shut down on return. Deterministic: the same
// arguments reproduce a byte-identical result on every call.
// ===========================================================================
ProductionSliceResult RunProductionSlice(shim::IFileSystem* fs,
                                         const std::string& gameDir,
                                         const std::string& cityName,
                                         int prodBuilding, i16 prot,
                                         std::uint32_t econSeed);

// ===========================================================================
// RunProductionStepsSynthetic — the same step sequence on a SYNTHETIC live world
// (no assets). Measures HashFullWorld before/after each step so a unit test can
// assert the production order + tick mutate the world while a no-op render does not.
// out[] receives one hash per step (load, command, tick, day); returns the count.
// ===========================================================================
struct ProductionStepHash {
    std::uint64_t hashAfter = 0;
    bool          mutated   = false;
};
int RunProductionStepsSynthetic(std::uint32_t seed, int prodBuilding, i16 prot,
                                std::uint32_t econSeed,
                                ProductionStepHash* out, int cap);

} // namespace guild::play
