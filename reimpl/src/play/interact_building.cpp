// Wave 25 PLAY P5 — BUILDING-INTERACTION vertical slice implementation.
// See interact_building.h for the grounding (EnterAndDispatch @0x51defc kind-switch
// + QueueRequestArgs26 @0x494848 opcode-26 building-field delta).
//
// REUSE of real reconstructions (called, never redefined):
//   play::PickAndResolveSceneEntity (scene_pick.cpp) -> REAL
//                                    sim::GameObjectResolveEntityById (entity.cpp).
//   sim::CommandQueue::EnqueuePacket/FlushSendQueue/ExecCommands/ComputePacketSize
//                                    (command.cpp) — the REAL lockstep codec that
//                                    carries + applies the building command.
//   sim::BuildingFindById (entity.cpp) -> the live g_objects record the apply mutates
//                                    (viewed as sim::BuildingRec).
#include "play/interact_building.h"

#include "sim/entity.h"          // BuildingFindById + g_objects
#include "sim/building_types.h"  // BuildingRec view over the 169-byte object record

#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// EnterAndDispatch's switch(*v21) — building KIND code -> contact-menu group.
// 1:1 with the decompiled dispatch ladder (see header).
// ---------------------------------------------------------------------------
BuildingActionGroup BuildingDialogKindToActionGroup(int kind) {
    switch (kind) {
        case 19:  return BuildingActionGroup::kGuildMaster;  // GuildMasterActions
        case 20:  return BuildingActionGroup::kSabotage;     // SabotageActions
        case 21:  return BuildingActionGroup::kThreat;       // ThreatLetterRhetoric
        case 22:  return BuildingActionGroup::kWineCellar;   // WineCellar_RunContactLoop
        case 24:  return BuildingActionGroup::kMistress;     // ContactMenu_Mistress
        case 116: return BuildingActionGroup::kSmith;        // SmithProduction (0x74)
        case 133: return BuildingActionGroup::kCarpenter;    // CarpenterProduction
        case 155: return BuildingActionGroup::kStonemason;   // StonemasonProduction (0x9B)
        case 247: return BuildingActionGroup::kProduction;   // ProductionContactLoop (0xF7)
        case 276: return BuildingActionGroup::kTreasury;     // CityTreasury (0x114)
        default:  return BuildingActionGroup::kNone;         // plain frame loop
    }
}

// ---------------------------------------------------------------------------
// Classification — (building kind, menu item) -> a concrete building Command.
// The mutating menu items are offered by the production / treasury / wine groups
// (the loops that own price/stock/upgrade verbs); the social groups (guild-master,
// sabotage, threat, mistress) dispatch non-building-mutating social verbs, so a
// mutating item there does not issue a building command.
// ---------------------------------------------------------------------------
namespace {

// Does this action group's contact menu expose building-record verbs (price/stock/
// upgrade)? The production hubs (smith/carpenter/stonemason/generic), the wine
// cellar (buy -> stock), and the treasury (cash -> price/value) do; the social
// groups do not.
bool GroupHasBuildingVerbs(BuildingActionGroup g) {
    switch (g) {
        case BuildingActionGroup::kSmith:
        case BuildingActionGroup::kCarpenter:
        case BuildingActionGroup::kStonemason:
        case BuildingActionGroup::kProduction:
        case BuildingActionGroup::kWineCellar:
        case BuildingActionGroup::kTreasury:
            return true;
        default:
            return false;
    }
}

// The (field, delta) the opcode-26 command carries for a mutating menu item.
struct FieldDelta { i32 field; i32 delta; };
bool MenuItemToFieldDelta(BuildingMenuItem item, FieldDelta& out) {
    switch (item) {
        case BuildingMenuItem::kRaisePrice: out = {kFieldPrice,   +1}; return true;
        case BuildingMenuItem::kLowerPrice: out = {kFieldPrice,   -1}; return true;
        case BuildingMenuItem::kRestock:    out = {kFieldStock,   +1}; return true;
        case BuildingMenuItem::kSell:       out = {kFieldStock,   -1}; return true;
        case BuildingMenuItem::kUpgrade:    out = {kFieldUpgrade, +1}; return true;
        default:                            return false;  // kNone / non-mutating
    }
}

} // namespace

BuildingCommand ClassifyBuildingAction(int kind, BuildingMenuItem item) {
    BuildingCommand cmd;
    cmd.group = BuildingDialogKindToActionGroup(kind);

    FieldDelta fd{};
    if (!MenuItemToFieldDelta(item, fd))
        return cmd;                       // non-mutating item -> no command
    if (!GroupHasBuildingVerbs(cmd.group))
        return cmd;                       // this dialog has no building verbs

    cmd.issued = true;
    cmd.opcode = kBuildingCmdOpcode;      // 26
    cmd.field  = fd.field;
    cmd.delta  = fd.delta;
    return cmd;
}

// ---------------------------------------------------------------------------
// Dialog hooks + inert-by-default reconstructions.
// ---------------------------------------------------------------------------
namespace {
const BuildingDialogHooks* g_dialogHooks = nullptr;

bool DefaultCheckEntry(i32 /*buildingId*/, int /*kind*/) { return true; }
void DefaultEnterInterior(i32 /*buildingId*/, int /*kind*/) { /* inert */ }

// The opcode-26 apply default: write the field delta into the live building record.
// The building's field codes (124=price, 28=stock, 89=upgrade) name byte offsets the
// rules core addresses off the 169-byte object record; we resolve the record via the
// REAL BuildingFindById and apply the delta to the matching BuildingRec field.
void DefaultApplyFieldDelta(i32 buildingId, i32 field, i32 delta) {
    sim::ObjectRec* o = sim::BuildingFindById(buildingId);
    if (!o) return;                                   // no live record (stale target)
    auto* b = reinterpret_cast<sim::BuildingRec*>(o);
    switch (field) {
        case kFieldPrice: {
            i32 v;
            std::memcpy(&v, &b->qualityScalar, sizeof v);   // +122 price/value scalar
            v += delta;
            std::memcpy(&b->qualityScalar, &v, sizeof v);
            break;
        }
        case kFieldStock: {
            // +10 fill/stock level (word); clamp at 0.
            i32 v = static_cast<i32>(b->fillLevel) + delta;
            if (v < 0) v = 0;
            b->fillLevel = static_cast<u16>(v);
            break;
        }
        case kFieldUpgrade: {
            // The upgrade level lives in the high byte of the +89 packed dword
            // (building.h::GetUpgradeLevel = packed >> 24). +89 falls in the
            // BuildingRec pad133 region; address it by raw byte offset.
            u8* base = reinterpret_cast<u8*>(o);
            i32 packed;
            std::memcpy(&packed, base + kFieldUpgrade, sizeof packed);
            i32 lvl = (packed >> 24) + delta;
            packed = (packed & 0x00FFFFFF) | ((lvl & 0xFF) << 24);
            std::memcpy(base + kFieldUpgrade, &packed, sizeof packed);
            break;
        }
        default:
            break;
    }
}

bool RunCheckEntry(i32 id, int kind) {
    if (g_dialogHooks && g_dialogHooks->checkEntryAllowed)
        return g_dialogHooks->checkEntryAllowed(id, kind);
    return DefaultCheckEntry(id, kind);
}
void RunEnterInterior(i32 id, int kind) {
    if (g_dialogHooks && g_dialogHooks->enterInterior)
        g_dialogHooks->enterInterior(id, kind);
    else
        DefaultEnterInterior(id, kind);
}
void RunApplyFieldDelta(i32 id, i32 field, i32 delta) {
    if (g_dialogHooks && g_dialogHooks->applyFieldDelta)
        g_dialogHooks->applyFieldDelta(id, field, delta);
    else
        DefaultApplyFieldDelta(id, field, delta);
}
} // namespace

void SetBuildingDialogHooks(const BuildingDialogHooks* hooks) {
    g_dialogHooks = hooks;
}

// ---------------------------------------------------------------------------
// The opcode-26 building-command queue handler.
// ---------------------------------------------------------------------------
namespace {
void BuildingCmdHandler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt,
                        sim::AckEntry* /*ack*/) {
    // Decode the three dwords QueueRequestArgs26 staged at +0x10: id, field, value.
    i32 buildingId = static_cast<i32>(pkt.get32(kCmdBuildingIdOff));
    i32 field      = static_cast<i32>(pkt.get32(kCmdFieldOff));
    i32 delta      = static_cast<i32>(pkt.get32(kCmdValueOff));
    RunApplyFieldDelta(buildingId, field, delta);
}
} // namespace

void InstallBuildingCommandHandler(sim::CommandQueue& q) {
    q.set_handler(kBuildingCmdOpcode, &BuildingCmdHandler);
}

// ---------------------------------------------------------------------------
// BuildingDialogFsm — EnterAndDispatch's lifecycle reduced to its states.
// ---------------------------------------------------------------------------
bool BuildingDialogFsm::Open(i32 buildingId, int kind) {
    buildingId_ = buildingId;
    kind_       = kind;
    group_      = BuildingDialogKindToActionGroup(kind);

    // EnterAndDispatch head: CheckEntryAllowed gate.
    if (!RunCheckEntry(buildingId, kind)) {
        state_ = BuildingDialogState::kEntryDenied;
        return false;
    }
    state_ = BuildingDialogState::kEntered;

    // Load the interior + spin the kind-selected contact loop (hooked; inert default).
    RunEnterInterior(buildingId, kind);
    state_ = BuildingDialogState::kContactMenu;
    return true;
}

BuildingCommand BuildingDialogFsm::ChooseMenuItem(BuildingMenuItem item) {
    if (state_ != BuildingDialogState::kContactMenu)
        return BuildingCommand{};         // dialog not open / not at menu
    BuildingCommand cmd = ClassifyBuildingAction(kind_, item);
    state_ = BuildingDialogState::kAction;
    return cmd;
}

void BuildingDialogFsm::Close() {
    state_      = BuildingDialogState::kClosed;
    buildingId_ = 0;
    kind_       = 0;
    group_      = BuildingActionGroup::kNone;
}

// ---------------------------------------------------------------------------
// Build the opcode-26 staging packet (mirrors QueueRequestArgs26 @0x494848:
// bytes[0]=26, then v5/v6/v7 at +0x10 = id/field/value).
// ---------------------------------------------------------------------------
namespace {
sim::CommandPacket BuildBuildingCmdPacket(i32 buildingId, i32 field, i32 value) {
    sim::CommandPacket pkt{};
    pkt.bytes[0] = kBuildingCmdOpcode;                 // v4[0] = 26
    pkt.put32(kCmdBuildingIdOff, static_cast<u32>(buildingId)); // v5 = a1
    pkt.put32(kCmdFieldOff,      static_cast<u32>(field));      // v6 = a2
    pkt.put32(kCmdValueOff,      static_cast<u32>(value));      // v7 = a3
    return pkt;
}
} // namespace

// ---------------------------------------------------------------------------
// The bridge.
// ---------------------------------------------------------------------------
BuildingClickResult IssueBuildingClick(sim::CommandQueue& q,
                                       const CityViewCamera& cam, float sx, float sy,
                                       const ScenePickObject* objects, int count,
                                       float pickRadius,
                                       const std::function<int(i32 id)>& kindOf,
                                       BuildingMenuItem item) {
    BuildingClickResult out;

    // (1) RESOLVE the picked building from the world-view click via the REAL scene
    //     pick (-> REAL sim::GameObjectResolveEntityById).
    int resolveKind = 0;
    ScenePickResult pick =
        PickAndResolveSceneEntity(cam, sx, sy, objects, count, pickRadius, &resolveKind);
    out.pickIndex   = pick.index;
    out.pickId      = pick.id;
    out.resolveKind = resolveKind;
    if (pick.index < 0)
        return out;                       // clicked empty space — no building

    // (2) Determine the building KIND code (the scene-entity type word the dialog
    //     switch reads). The reconstructed record does not store it as a named field;
    //     the caller resolves it (seeded kind in tests / derived in the e2e).
    int kind = kindOf ? kindOf(pick.id) : 0;
    out.buildingKind = kind;
    out.group        = BuildingDialogKindToActionGroup(kind);

    // (3) OPEN the dialog FSM (CheckEntryAllowed gate, interior load, kind dispatch).
    BuildingDialogFsm fsm;
    out.opened = fsm.Open(pick.id, kind);
    if (!out.opened) {
        out.finalState = fsm.state();
        return out;                       // entry denied
    }

    // (4) CHOOSE the menu item -> classify the building Command.
    out.command    = fsm.ChooseMenuItem(item);
    out.finalState = fsm.state();
    if (!out.command.issued)
        return out;                       // non-mutating / unsupported for this kind

    // (5) BUILD + ENQUEUE the opcode-26 packet through the REAL CommandQueue codec.
    sim::CommandPacket pkt =
        BuildBuildingCmdPacket(pick.id, out.command.field, out.command.delta);
    u32 sendBefore = q.send_count();
    i32 slot = q.EnqueuePacket(pkt);
    out.enqueued = (slot >= 0) && (q.send_count() != sendBefore);
    out.ringSlot = slot;

    // (6) APPLY — flush locally (standalone) + execute so the opcode-26 handler
    //     mutates the live building record.
    if (out.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();
        out.applied = true;
    }

    return out;
}

} // namespace guild::play
