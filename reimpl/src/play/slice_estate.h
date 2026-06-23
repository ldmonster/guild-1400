#pragma once
// Wave 28 PLAY P5 — the REAL-ESTATE (buy building / property) vertical slice: a
// faithful click -> buy-building dialog -> REAL purchase command -> sim effect ->
// render slice for the property-acquisition system (namespace guild::play).
//
// It mirrors the slice_market / slice_bank / slice_council pattern: resolve a
// purchase interaction, classify it, emit the REAL ownership-transfer command
// packet, route it through the REAL sim::CommandQueue lockstep codec, apply it
// (mutating the bought object's folded owner field + the buyer's folded cash), then
// advance ONE game-day and prove ownership/treasury evolved while the whole run
// stays deterministic (HashFullWorld differs pre/post and is byte-identical on rerun).
//
// GROUNDING (decompiled this wave — the live player buy-building path):
//
//   * VIBE_Building_EnqueueBuyBuilding @0x588798 is the buy-building command BUILDER.
//     Driven by VIBE_Building_EvalBuyBuilding @0x46c97c on a kind-4 (purchasable)
//     building. It emits a grouped command sequence:
//        EnqueueBuildingActionStart("BuyBuilding")        // group begin
//        VIBE_Command_EnqueueCmd15(-1, seller, price, player);   // the MONEY leg
//        VIBE_Command_QueueRequestQuad56(buyerObjId, sellerRec, _, sellerRec); // OWNER
//        [ QueueRequestPair57 / QueueRequestArgs25 ... ]   // scene/UI follow-ups
//        EnqueueBuildingActionEnd()                        // group end
//     The OWNERSHIP transfer is OPCODE 56 (VIBE_Command_QueueRequestQuad56 @0x495098).
//
//   * VIBE_Command_QueueRequestQuad56 @0x495098 packs the opcode-56 packet:
//        v6=a1@+0x10 (the object being reparented / new owner record),
//        v7=a2@+0x14 (the bought object id), v8=a4@+0x18 (the parent record id),
//        bytes[0]=56. (The a3 register arg is staged OUTSIDE the packet and does NOT
//        reach the wire — faithful to the original; see sim/command_builders.h.)
//
//   * The opcode-56 APPLY is VIBE_Command_ExSetObjectParent @0x49ae60 (dispatch
//     table funcs_4941F4[56]), which calls VIBE_Building_SetObjectParent @0x58820c.
//     SetObjectParent writes the bought object record's PARENT/OWNER fields:
//        *(_WORD*)(obj + 37) = newParentHandle;   // +37 parent handle word
//        *(_WORD*)(obj + 39) = newOwnerId;         // +39 OWNER id word  <-- the
//                                                  //     folded ownership field
//     (the rest — storage-room attach, scene-slot fixup, character flag refresh —
//     are deep render/scene leaves the apply DEFERS; not part of the folded world).
//     Object records (g_objects) are folded RAW BYTES by HashFullWorld, so the +39
//     owner write is observed.
//
// WIRED REAL siblings: the opcode-56 QueueRequestQuad56 packet layout + the REAL
// sim::CommandQueue codec (EnqueuePacket/FlushSendQueue/ExecCommands), the real
// bought-object record (sim::BuildingFindById, the live g_objects row the apply
// reparents — folded by HashFullWorld), the opcode-15 money leg (the price debit),
// play::RunEconomyTurn (the real game-day cascade), play::HashFullWorld (the oracle).
//
// INERT-default GAPS (declared honestly): the full ExSetObjectParent/SetObjectParent
// apply pulls in Person-query, scene-slot, storage-room and character-flag leaves
// that are not part of the folded world; the slice applies the DETERMINISTIC FOLDED
// net effect that path produces — the +39 owner id (and +37 parent handle) on the
// bought object record, plus the buyer's cash debit (the EnqueueCmd15 money leg).
// Both are exposed as installable EstateApplyHooks with inert-by-default
// reconstructions DEFINED IN slice_estate.cpp (the build model), overridable by tests.
//
// Additive: no edits to any existing .cpp/.h.
#include <cstdint>

#include "guild/common/types.h"
#include "sim/command.h"

namespace guild::play {

// ===========================================================================
// The estate interaction the slice replays: buy the building `objectId` (a kind-4
// purchasable building) for `buyerId` at `price`, transferring ownership to
// `newOwnerId` (the buyer's record handle).
// ===========================================================================
struct EstateInteraction {
    i32 objectId    = 0;     // the bought building (live g_objects id; op56 a1 @+0x10)
    i32 buyerId     = 0;     // the buying person (the EnqueueCmd15 money-leg payer)
    i16 newOwnerId  = 0;     // the new owner id stamped into obj+39 (op56 derived)
    i16 parentHandle = 0;    // the new parent handle stamped into obj+37
    i32 price       = 0;     // the purchase price (EnqueueCmd15 a3)
    u8  objectKind  = 4;     // *building must be 4 (EvalBuyBuilding gate)
    u8  player      = 0;     // byte_6477A1 (acting player slot)
};

// ===========================================================================
// The classified purchase COMMAND (the golden: interaction -> wire command). 1:1
// with the OWNERSHIP leg (QueueRequestQuad56, opcode 56) of EnqueueBuyBuilding.
// ===========================================================================
inline constexpr u8 kEstateCmdOpcode = 56;       // VIBE_Command_QueueRequestQuad56

// QueueRequestQuad56 packet-staging offsets (recovered @0x495098; field ROLES
// recovered from the builder EnqueueBuyBuilding @0x588798 and the apply
// ExSetObjectParent @0x49ae60). The builder call is
//   QueueRequestQuad56(*(Begin+1), *(v19+1), v11/*not in packet*/, *(v19+1))
// so a1@+0x10 = the bought OBJECT id (Begin = the reparented object), a2@+0x14 =
// the seller/new-owner record id, a4@+0x18 = the same record id. The apply reads:
//   Begin = QueryBegin(..., *(pkt+0x10))                 // the object
//   parentRec = FindRecordById(*(pkt+0x14))              // -> SetObjectParent a2
//   ownerRec  = FindRecordById(*(pkt+0x18))              // -> SetObjectParent a3
//   SetObjectParent(Begin, *parentRec, (u16)*ownerRec)   // +37 = parent, +39 = owner
// i.e. +0x10 is the OBJECT, +0x14 sources the +37 parent handle, +0x18 sources the
// +39 owner id. (a3 is staged at ebp-4h and never reaches the wire.)
inline constexpr u32 kEstateObjectOff   = 0x10;  // v6 = a1 (the bought object id)
inline constexpr u32 kEstateParentOff   = 0x14;  // v7 = a2 (parent-handle source rec)
inline constexpr u32 kEstateNewOwnerOff = 0x18;  // v8 = a4 (owner-id source rec)

// The bought-object folded fields SetObjectParent @0x58820c writes (object record
// raw bytes, folded by HashFullWorld).
inline constexpr u32 kEstateOwnerFieldOff  = 39;  // *(_WORD*)(obj+39) = owner id
inline constexpr u32 kEstateParentFieldOff = 37;  // *(_WORD*)(obj+37) = parent handle

// The buyer's spendable cash (the EnqueueCmd15 money leg debits the price).
inline constexpr u32 kEstateCashFieldOff = 0x0A;  // Person.cash word (GetCashAmount)

struct EstateCommand {
    bool issued      = false;    // a valid purchase (kind 4, price > 0)
    u8   opcode      = 0;        // kEstateCmdOpcode (56) when issued
    i32  object      = 0;        // a2 (the bought object id)
    i16  newOwnerId  = 0;        // stamped into obj+39
    i16  parentHandle = 0;       // stamped into obj+37
    i32  buyer       = 0;        // the money-leg payer
    i32  price       = 0;        // the price (money leg)
    u8   player      = 0;
};

// 1:1 classifier: resolve an EstateInteraction into its EstateCommand. A non-
// purchase (kind != 4 / price <= 0) yields {issued=false}.
EstateCommand ClassifyEstateInteraction(const EstateInteraction& ei);

// ===========================================================================
// Estate apply hooks — the leaves not covered by one reconstructed sibling.
// Installable with inert-by-default reconstructions defined in slice_estate.cpp.
// ===========================================================================
struct EstateApplyHooks {
    // Reparent the bought object: write owner (+39) and parent handle (+37). Default
    // mutates the live g_objects record via sim::BuildingFindById.
    void (*applyOwnership)(i32 objectId, i16 ownerId, i16 parentHandle) = nullptr;
    // Debit the buyer's cash by the price (the EnqueueCmd15 money leg). Default
    // mutates the live Person record via sim::PersonFindRecordById (clamped at 0).
    void (*applyCash)(i32 buyerId, i64 delta) = nullptr;
};
void SetEstateApplyHooks(const EstateApplyHooks* hooks);

// Install the opcode-56 purchase-command handler on `q`. The money leg (opcode 15)
// is applied inline by the slice (it is a sibling command in the buy group); the
// opcode-56 handler performs the ownership reparent. Idempotent per queue.
void InstallEstateCommandHandler(sim::CommandQueue& q);

// Read the current owner id (object +39) of a live object (0 if not found).
i16 ReadObjectOwner(i32 objectId);
// Read the current parent handle (object +37) of a live object (0 if not found).
i16 ReadObjectParentHandle(i32 objectId);
// Read the current cash (Person +0x0A) of a live buyer (0 if not found).
i64 ReadBuyerCash(i32 buyerId);

// ===========================================================================
// The result of one estate click -> command -> apply -> game-day cycle.
// ===========================================================================
struct EstateSliceResult {
    // --- the classified + emitted command ---
    EstateCommand command{};
    bool   enqueued = false;       // a packet hit the send ring
    i32    ringSlot = -1;          // EnqueuePacket ring slot
    bool   applied  = false;       // FlushSendQueue + ExecCommands ran

    // --- before / after the purchase (the sim effect) ---
    i16    ownerBefore = 0;
    i16    ownerAfter  = 0;
    i16    parentBefore = 0;
    i16    parentAfter  = 0;
    i64    cashBefore = 0;
    i64    cashAfter  = 0;

    // --- the game-day ---
    int    economyPasses = 0;

    // --- determinism oracle (the three world hashes) ---
    std::uint64_t hashBefore       = 0;  // HashFullWorld() before the purchase
    std::uint64_t hashAfterCommand = 0;  // ... after the purchase applied
    std::uint64_t hashAfterDay     = 0;  // ... after the game-day

    bool purchaseChangedWorld() const { return hashBefore != hashAfterCommand; }
    bool dayChangedWorld()      const { return hashAfterCommand != hashAfterDay; }
    bool ownerMoved()           const { return ownerBefore != ownerAfter; }
    bool cashMoved()            const { return cashBefore != cashAfter; }
};

// ===========================================================================
// RunEstateSlice — the purchase slice over the ALREADY-LOADED live world.
//
//   1. snapshot the object's owner/parent + buyer cash + HashFullWorld() (before),
//   2. classify the interaction -> EstateCommand,
//   3. apply the money leg (opcode-15 price debit) inline, then build the opcode-56
//      ownership packet, enqueue through the REAL sim::CommandQueue, flush + exec so
//      the apply handler reparents the bought object (owner +39, parent +37),
//   4. HashFullWorld() (after command),
//   5. advance ONE game-day via play::RunEconomyTurn (seeded by `econSeed`),
//   6. HashFullWorld() (after day), snapshot owner/parent + cash (after).
//
// The caller has populated the live sim arrays (synthetic seed or io::LoadWorld)
// with a live object whose id == ei.objectId and a buyer Person whose id ==
// ei.buyerId. `q` is a standalone CommandQueue (Init()'d). Returns the run result.
EstateSliceResult RunEstateSlice(const EstateInteraction& ei,
                                 std::uint32_t econSeed, sim::CommandQueue& q);

} // namespace guild::play
