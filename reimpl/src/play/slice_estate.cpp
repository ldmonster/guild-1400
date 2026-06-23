// Wave 28 PLAY P5 — the REAL-ESTATE (buy building) vertical slice implementation.
// See slice_estate.h for the grounding (EnqueueBuyBuilding @0x588798 ->
// QueueRequestQuad56 @0x495098 opcode-56 ownership packet + EnqueueCmd15 money leg;
// ExSetObjectParent @0x49ae60 -> SetObjectParent @0x58820c writes obj+39 owner /
// obj+37 parent handle).
//
// REUSE of real reconstructions (called, never redefined — ODR):
//   sim::BuildingFindById                     (entity.cpp; the live bought-object row
//                                              the apply reparents, folded by HashFullWorld)
//   sim::PersonFindRecordById                 (entity.cpp; the buyer record the money
//                                              leg debits, folded by HashFullWorld)
//   sim::CommandQueue::EnqueuePacket / FlushSendQueue / ExecCommands / set_handler
//                                             (command.cpp; the REAL lockstep codec)
//   play::RunEconomyTurn / SeedEconomyTurnState (turn_economy.cpp; the real game-day)
//   play::HashFullWorld                       (world_digest.cpp; the determinism oracle)
//   crt::Srand                                (crt/rand.h)
#include "play/slice_estate.h"

#include <cstring>

#include "crt/rand.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "sim/entity.h"                // BuildingFindById + PersonFindRecordById

namespace guild::play {

namespace {

// ---------------------------------------------------------------------------
// Default (inert-by-default) ownership/cash apply — DEFINED here so the unified
// build links; tests may install EstateApplyHooks to override.
//
// SetObjectParent @0x58820c writes the bought object record's owner id (+39 word)
// and parent handle (+37 word); the buyer's cash (+0x0A word) is debited the price
// (the EnqueueCmd15 money leg). Both records are folded raw bytes by HashFullWorld,
// so the moves are observed. The deep storage/scene/character leaves SetObjectParent
// also touches are not part of the folded world (see header).
// ---------------------------------------------------------------------------
const EstateApplyHooks* g_hooks = nullptr;

void DefaultApplyOwnership(i32 objectId, i16 ownerId, i16 parentHandle) {
    sim::ObjectRec* o = sim::BuildingFindById(objectId);
    if (!o) return;
    u8* base = reinterpret_cast<u8*>(o);
    std::memcpy(base + kEstateParentFieldOff, &parentHandle, sizeof parentHandle); // +37
    std::memcpy(base + kEstateOwnerFieldOff,  &ownerId,      sizeof ownerId);      // +39
}

void DefaultApplyCash(i32 buyerId, i64 delta) {
    sim::Person* p = sim::PersonFindRecordById(buyerId);
    if (!p) return;
    u8* base = reinterpret_cast<u8*>(p);
    i16 cur;
    std::memcpy(&cur, base + kEstateCashFieldOff, sizeof cur);
    i32 nv = static_cast<i32>(cur) + static_cast<i32>(delta);
    if (nv < 0) nv = 0;
    cur = static_cast<i16>(nv);
    std::memcpy(base + kEstateCashFieldOff, &cur, sizeof cur);
}

void RunApplyOwnership(i32 id, i16 owner, i16 parent) {
    if (g_hooks && g_hooks->applyOwnership) g_hooks->applyOwnership(id, owner, parent);
    else                                    DefaultApplyOwnership(id, owner, parent);
}
void RunApplyCash(i32 id, i64 delta) {
    if (g_hooks && g_hooks->applyCash) g_hooks->applyCash(id, delta);
    else                               DefaultApplyCash(id, delta);
}

// ---------------------------------------------------------------------------
// The opcode-56 ownership-command handler: decode the QueueRequestQuad56 staging
// and reparent the bought object (the net folded effect SetObjectParent produces).
// The new owner id (a1@+0x10) and parent handle (a4@+0x18) target the bought object
// (a2@+0x14).
// ---------------------------------------------------------------------------
void EstateCmdHandler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt,
                      sim::AckEntry* /*ack*/) {
    i32 objectId = static_cast<i32>(pkt.get32(kEstateObjectOff));    // a1 @+0x10 (object)
    i32 parent   = static_cast<i32>(pkt.get32(kEstateParentOff));    // a2 @+0x14 (parent src)
    i32 newOwner = static_cast<i32>(pkt.get32(kEstateNewOwnerOff));  // a4 @+0x18 (owner src)
    RunApplyOwnership(objectId, static_cast<i16>(newOwner), static_cast<i16>(parent));
}

} // namespace

// ===========================================================================
// Classifier — interaction -> purchase command (the ownership leg).
// ===========================================================================
EstateCommand ClassifyEstateInteraction(const EstateInteraction& ei) {
    EstateCommand cmd;
    if (ei.objectKind != 4)
        return cmd;                       // not a purchasable building (EvalBuyBuilding gate)
    if (ei.price <= 0)
        return cmd;                       // no price

    cmd.issued      = true;
    cmd.opcode      = kEstateCmdOpcode;   // 56
    cmd.object      = ei.objectId;
    cmd.newOwnerId  = ei.newOwnerId;
    cmd.parentHandle = ei.parentHandle;
    cmd.buyer       = ei.buyerId;
    cmd.price       = ei.price;
    cmd.player      = ei.player;
    return cmd;
}

void SetEstateApplyHooks(const EstateApplyHooks* hooks) { g_hooks = hooks; }

void InstallEstateCommandHandler(sim::CommandQueue& q) {
    q.set_handler(kEstateCmdOpcode, &EstateCmdHandler);
}

i16 ReadObjectOwner(i32 objectId) {
    sim::ObjectRec* o = sim::BuildingFindById(objectId);
    if (!o) return 0;
    i16 v;
    std::memcpy(&v, reinterpret_cast<u8*>(o) + kEstateOwnerFieldOff, sizeof v);
    return v;
}

i16 ReadObjectParentHandle(i32 objectId) {
    sim::ObjectRec* o = sim::BuildingFindById(objectId);
    if (!o) return 0;
    i16 v;
    std::memcpy(&v, reinterpret_cast<u8*>(o) + kEstateParentFieldOff, sizeof v);
    return v;
}

i64 ReadBuyerCash(i32 buyerId) {
    sim::Person* p = sim::PersonFindRecordById(buyerId);
    if (!p) return 0;
    i16 v;
    std::memcpy(&v, reinterpret_cast<u8*>(p) + kEstateCashFieldOff, sizeof v);
    return v;
}

// Build the opcode-56 packet (1:1 with QueueRequestQuad56 @0x495098) for a purchase.
namespace {
sim::CommandPacket BuildEstatePacket(const EstateCommand& c) {
    sim::CommandPacket pkt{};
    pkt.bytes[0] = kEstateCmdOpcode;                                 // v5[0] = 56
    pkt.put32(kEstateObjectOff,   static_cast<u32>(c.object));       // v6 = a1 (object,+0x10)
    pkt.put32(kEstateParentOff,   static_cast<u32>(c.parentHandle)); // v7 = a2 (parent,+0x14)
    pkt.put32(kEstateNewOwnerOff, static_cast<u32>(c.newOwnerId));   // v8 = a4 (owner,+0x18)
    return pkt;
}
} // namespace

// ===========================================================================
// RunEstateSlice — the purchase slice over the already-loaded live world.
// ===========================================================================
EstateSliceResult RunEstateSlice(const EstateInteraction& ei,
                                 std::uint32_t econSeed, sim::CommandQueue& q) {
    EstateSliceResult r;

    // --- snapshot BEFORE -----------------------------------------------------
    r.ownerBefore  = ReadObjectOwner(ei.objectId);
    r.parentBefore = ReadObjectParentHandle(ei.objectId);
    r.cashBefore   = ReadBuyerCash(ei.buyerId);
    crt::Srand(econSeed);
    r.hashBefore = HashFullWorld();

    // --- step: CLASSIFY ------------------------------------------------------
    r.command = ClassifyEstateInteraction(ei);
    if (!r.command.issued) {
        r.ownerAfter  = r.ownerBefore;
        r.parentAfter = r.parentBefore;
        r.cashAfter   = r.cashBefore;
        crt::Srand(econSeed);
        r.hashAfterCommand = HashFullWorld();
        r.hashAfterDay     = r.hashAfterCommand;
        return r;
    }

    // --- step: MONEY LEG (opcode-15 price debit) then OWNERSHIP (opcode 56) --
    InstallEstateCommandHandler(q);
    SetEstateApplyHooks(nullptr);   // inert-default ownership/cash mutation
    // The buy group's EnqueueCmd15 money leg debits the buyer the price. (Applied
    // inline as the sibling command in the group; the opcode-56 packet below carries
    // the ownership transfer through the real codec.)
    RunApplyCash(r.command.buyer, -static_cast<i64>(r.command.price));

    sim::CommandPacket pkt = BuildEstatePacket(r.command);
    u32 before = q.send_count();
    i32 slot = q.EnqueuePacket(pkt);
    r.ringSlot = slot;
    r.enqueued = (slot >= 0) && (q.send_count() != before);
    if (r.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();             // -> EstateCmdHandler -> ownership reparent
        r.applied = true;
    }
    r.ownerAfter  = ReadObjectOwner(ei.objectId);
    r.parentAfter = ReadObjectParentHandle(ei.objectId);
    r.cashAfter   = ReadBuyerCash(ei.buyerId);
    crt::Srand(econSeed);
    r.hashAfterCommand = HashFullWorld();

    // --- step: GAME-DAY (the REAL economy passes; seeded for determinism) ----
    crt::Srand(econSeed);
    EconomyTurnState st = SeedEconomyTurnState();
    st.day = 0;
    EconomyTurnDeltas d = RunEconomyTurn(st);
    r.economyPasses = d.passesRun;
    crt::Srand(econSeed);
    r.hashAfterDay = HashFullWorld();
    return r;
}

} // namespace guild::play
