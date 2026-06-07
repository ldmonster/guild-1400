// Wave 25 PLAY P1/P5 — INPUT -> COMMAND -> sim-apply bridge implementation.
// See input_command.h for the grounding (DispatchSelectedUnits @0x4891c8 +
// IssueOnObject @0x488ff0 decomp).
//
// REUSE of real reconstructions (called, never redefined):
//   play::PickAndResolveSceneEntity  (scene_pick.cpp) -> REAL
//                                     sim::GameObjectResolveEntityById (entity.cpp).
//   sim::IssueOnObject               (command_apply12.cpp, VIBE_Command_IssueOnObject
//                                     @0x488ff0) — the classifier+builder.
//   sim::BuildMoveToPacket/BuildConquerCommand/BuildLabeledMoveCommand/
//   BuildAttackPacket + RequestBuildOp80 (combat_packets.cpp) — driven via
//                                     IssueOnObject's hooks.
//   sim::CommandQueue::EnqueuePacket/FlushSendQueue/ExecCommands (command.cpp) —
//                                     the REAL codec that carries+applies the order.
#include "play/input_command.h"

#include "sim/entity.h"   // g_objects/g_persons + BuildingFindById/PersonFindRecordById

#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// Classification — 1:1 with IssueOnObject's branch ladder.
// ---------------------------------------------------------------------------
OrderKind ClassifyOrder(PickedClass picked, CursorMode mode, bool attackAllowed) {
    // if ( !dword_67221C ) return 0;  — the cursor-order armed latch.
    if (mode == CursorMode::kDisarmed)
        return kKindNone;

    // if ( dword_631720 ) { ...object/label branch... } else { ...ground... }
    if (picked == PickedClass::kNone) {
        // No object under the cursor -> the cursor-raycast GROUND move (kind 1).
        return kKindGround;
    }

    if (picked == PickedClass::kUnit) {
        // *(picked+535)==2: a battle UNIT. Attack iff allowed (different team OR
        // friendly-fire override) — else nothing issued (return v18==0).
        return attackAllowed ? kKindAttack : kKindNone;
    }

    // A non-unit object: treated as an order-label STRING. The selected cursor
    // mode supplies which label is active (the toolbar's selected order tool):
    //   kMove    -> "sp_ESCAPE"  -> MOVE          (kind 3)
    //   kLabeled -> "sp_CONQUER" -> LABELLED MOVE (kind 4)
    //   kConquer -> "WARE"       -> CONQUER       (kind 6)
    //   kAttackMove on a non-unit object -> no label match -> nothing issued.
    switch (mode) {
        case CursorMode::kMove:    return kKindMove;
        case CursorMode::kLabeled: return kKindLabeled;
        case CursorMode::kConquer: return kKindConquer;
        default:                   return kKindNone;
    }
}

// ---------------------------------------------------------------------------
// The order-label string each cursor mode selects (the dword_631720 the original
// reads). IssueOnObject classifies these via the (real) strncmp prefix tests.
// ---------------------------------------------------------------------------
namespace {
const char* LabelForMode(CursorMode mode) {
    switch (mode) {
        case CursorMode::kMove:    return "sp_ESCAPE";
        case CursorMode::kLabeled: return "sp_CONQUER";
        case CursorMode::kConquer: return "WARE";
        default:                   return "";   // no label match
    }
}

// ---------------------------------------------------------------------------
// Opcode-80 ORDER APPLY hook + the default (inert-by-default reconstruction).
// ---------------------------------------------------------------------------
const InputCommandApplyHooks* g_applyHooks = nullptr;

// The default apply: write (kind, destX, destZ) into the resolved target's live
// entity record — the observable "the unit took the order" mutation. Resolves the
// target id across the REAL entity arrays (object first, then person), matching the
// id-resolution order the order-apply path uses.
void DefaultApplyOrder(i32 targetId, u8 kind, i32 destX, i32 destZ) {
    auto writeRec = [&](u8* base) {
        base[kAppliedKindOff] = kind;
        std::memcpy(base + kAppliedDestXOff, &destX, sizeof destX);
        std::memcpy(base + kAppliedDestZOff, &destZ, sizeof destZ);
    };
    if (sim::ObjectRec* o = sim::BuildingFindById(targetId)) {
        writeRec(reinterpret_cast<u8*>(o));
        return;
    }
    if (sim::Person* p = sim::PersonFindRecordById(targetId)) {
        writeRec(reinterpret_cast<u8*>(p));
        return;
    }
    // No live record for the id: nothing to mutate (a stale order target).
}

// CommandQueue opcode-80 handler: decode the staging block the builder wrote into
// the packet (+0x14) and forward the order to the active apply hook. This is the
// apply step ExecCommands invokes after FlushSendQueue stores the packet locally.
void Op80Handler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt, sim::AckEntry* /*ack*/) {
    const u8* stg = pkt.bytes + 0x14;             // RequestBuildOp80: staging @ +0x14
    // staging +0 == *(target+4) (the original stamps the target id PLUS 4); recover
    // the id by undoing the +4 (matching BuildLabeledMove/Conquer/ground writes).
    i32 stampedTargetPlus4 =
        static_cast<i32>(static_cast<u32>(stg[0]) | (static_cast<u32>(stg[1]) << 8)
        | (static_cast<u32>(stg[2]) << 16) | (static_cast<u32>(stg[3]) << 24));
    i32 targetId = stampedTargetPlus4 - 4;
    u8  kind = stg[sim::kSlotKind];               // staging +4

    // Destination tile: a ground move stores two dwords at +0x10/+0x14 (set_field4/5);
    // a conquer order packs two single bytes at +0x20/+0x21. Decode per-kind so the
    // applied mutation carries the real destination either way.
    i32 destX = 0, destZ = 0;
    if (kind == kKindConquer) {
        destX = stg[sim::kConquerTileXByte];
        destZ = stg[sim::kConquerTileZByte];
    } else {
        // ground move (kind 1): set_field4 -> +0x10, set_field5 -> +0x14.
        destX = static_cast<i32>(static_cast<u32>(stg[0x10]) | (static_cast<u32>(stg[0x11]) << 8)
              | (static_cast<u32>(stg[0x12]) << 16) | (static_cast<u32>(stg[0x13]) << 24));
        destZ = static_cast<i32>(static_cast<u32>(stg[0x14]) | (static_cast<u32>(stg[0x15]) << 8)
              | (static_cast<u32>(stg[0x16]) << 16) | (static_cast<u32>(stg[0x17]) << 24));
    }

    const InputCommandApplyHooks* hk = g_applyHooks;
    if (hk && hk->applyOrder)
        hk->applyOrder(targetId, kind, destX, destZ);
    else
        DefaultApplyOrder(targetId, kind, destX, destZ);
}
} // namespace

void SetInputCommandApplyHooks(const InputCommandApplyHooks* hooks) {
    g_applyHooks = hooks;
}

void InstallOrderApplyHandler(sim::CommandQueue& q) {
    q.set_handler(80, &Op80Handler);
}

// ---------------------------------------------------------------------------
// The bridge.
// ---------------------------------------------------------------------------
WorldOrder IssueWorldClick(sim::CommandQueue& q,
                           const CityViewCamera& cam, float sx, float sy,
                           const ScenePickObject* objects, int count,
                           float pickRadius, CursorMode mode,
                           bool attackAllowed,
                           const sim::CombatOrderHandle& h,
                           const sim::CombatOrderContext& ctx) {
    WorldOrder out;

    // (1) RESOLVE the picked object/tile from the world-view click via the REAL
    //     scene pick (-> REAL sim::GameObjectResolveEntityById).
    int resolveKind = 0;
    ScenePickResult pick =
        PickAndResolveSceneEntity(cam, sx, sy, objects, count, pickRadius, &resolveKind);
    out.pickIndex   = pick.index;
    out.pickId      = pick.id;
    out.resolveKind = resolveKind;

    // The picked CLASS as the classifier reads it: nothing under cursor -> none;
    // a person/unit resolve (kind 3) -> a battle unit (the attack branch); any
    // other resolved object -> an order-label string.
    PickedClass cls = PickedClass::kNone;
    if (pick.index >= 0)
        cls = (resolveKind == 3) ? PickedClass::kUnit : PickedClass::kObject;

    // (2) CLASSIFY (the golden) — what order this (class, mode) issues.
    out.kind = ClassifyOrder(cls, mode, attackAllowed);

    if (mode == CursorMode::kDisarmed)
        return out;   // disarmed: IssueOnObject returns 0 without issuing.

    // (3) BUILD via the REAL builders, threaded through sim::IssueOnObject. We feed
    //     IssueOnObject the resolved pick + selected label through its hooks (the
    //     same globals DispatchSelectedUnits would have populated), so the genuine
    //     classifier+builder runs and stamps the real CommandQueue.
    i32 target = pick.id;
    const char* label = LabelForMode(mode);
    const bool pickedIsUnit = (cls == PickedClass::kUnit);
    const bool havePick = (cls != PickedClass::kNone);

    sim::IssueOnObjectHooks ihk{};
    ihk.orderArmed   = [] { return true; };               // dword_67221C set
    ihk.pickedObject = [havePick, target] { return havePick ? (target ? target : 1) : 0; };
    ihk.pickedIsUnit = [pickedIsUnit](i32) { return pickedIsUnit; };
    ihk.pickedUnitOwner = [](i32) { return 0; };
    ihk.attackAllowed   = [attackAllowed](i32, i32) { return attackAllowed; };
    ihk.pickedLabel  = [label] { return label; };
    // Ground branch: when nothing is picked, raycast the cursor to a tile via the
    // forwarded worldToTile (the ctx the caller supplies models the heightmap).
    ihk.raycastGround = [&ctx](i32& ox, i32& oz) {
        if (ctx.worldToTile) return ctx.worldToTile(0.0f, 0.0f, 0.0f, ox, oz);
        ox = 0; oz = 0; return false;
    };
    sim::SetIssueOnObjectHooks(&ihk);

    sim::OrderStage slot{};
    u32 sendBefore = q.send_count();
    char issued = sim::IssueOnObject(q, h, target, ctx, slot);
    sim::SetIssueOnObjectHooks(nullptr);

    out.issued  = (issued != 0);
    out.slot    = slot;
    out.enqueued = (q.send_count() != sendBefore);
    if (out.enqueued)
        out.ringSlot = static_cast<i32>(q.send_count());  // last assigned ring slot

    // (4) APPLY — flush the pending order locally (standalone) and execute the
    //     received list, dispatching opcode 80 to the apply handler so the live sim
    //     mutates. (No-op when nothing was enqueued, e.g. the labelled-move / no-op
    //     branches.)
    if (out.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();
    }

    return out;
}

} // namespace guild::play
