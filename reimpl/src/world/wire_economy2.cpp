// See wire_economy2.h. Binds the SupervisionHooks (master-AI daily sweep) and
// IStockHooks (building stock/value command tails) bridges to their real
// reconstructed leaves. Glue only.
//
// Shared state: every command emit stages onto the SAME shared real CommandQueue
// (real_hooks.h RealCommandQueue()) the other wave-1..4 installers use; the He
// handler probe runs against the SAME shared real HandlerTable (real_hooks3.h
// RealHandlerTable()) that real_hooks3 / wire_charaction own.
#include "world/wire_economy2.h"

#include "ai/meister_supervision.h"   // SupervisionHooks / Get/SetSupervisionHooks
#include "sim/building_stock.h"       // IStockHooks / SetStockHooks

#include "sim/real_hooks.h"           // RealCommandQueue()
#include "sim/real_hooks3.h"          // RealHandlerTable()
#include "sim/handler_entry.h"        // HandlerTable / HandlerRecord / HrField16
#include "sim/command_builders2.h"    // QueueRequestArgs26 (opcode 26)
#include "world/law.h"                // GesetzGetRecord / LawRecord (Gesetz_GetRecord(6))
#include "util/math_random.h"         // util::RandomModulo

#include <cstring>

namespace guild::world {

namespace {

guild::sim::CommandQueue& Q()   { return *guild::sim::RealCommandQueue(); }
guild::sim::HandlerTable&  HeT() { return *guild::sim::RealHandlerTable(); }

// =========================================================================
// SupervisionHooks leaves (ai/meister_supervision.h).
// =========================================================================

// VIBE_He_FindFirstHandlerByFilter(1, 0, type)  [+ owner-ordinal walk].
//
// RequestCmd134 calls handler_exists(1, 134, 0): "does ANY kind-134 handler
// exist?" -> the original is FindFirstHandlerByFilter(1, 0, 134) != null.
// RequestBuildingCmd43 calls handler_exists(1, 43, pid): "does a pending kind-43
// handler already TARGET person pid?" -> the original FindFirst(1,0,43) then walks
// FindNextMatchingHandler comparing the record's dword @+16 (HrField16, the cmd43
// owner column the decompile reads as *(rec+4 dword) == *(person+1)) against pid.
// owner==0 collapses to the plain existence probe (no walk).
bool WeHandlerExists(int /*filterKind*/, int type, int ownerId) {
    guild::sim::HandlerRecord* r = HeT().FindFirstHandlerByFilter(1, 0, type);
    if (ownerId == 0)
        return r != nullptr;
    while (r) {
        if (guild::sim::HrField16(r) == ownerId)
            return true;
        r = HeT().FindNextMatchingHandler();
    }
    return false;
}

// VIBE_Math_RandomModulo(n) — uniform draw in [0, n).
guild::u16 WeRngMod(guild::u16 n) {
    return static_cast<guild::u16>(guild::util::RandomModulo(n));
}

// =========================================================================
// IStockHooks leaves (sim/building_stock.h). Virtual subclass: the methods we
// can faithfully bind are overridden; everything else falls through to the inert
// IStockHooks base behaviour (scene QueryFind / Person-array resolves / the
// synchronous interaction-status pump / the multi-field delta packet — none of
// which the abstracted hook surface can reproduce byte-faithfully).
// =========================================================================
class RealStockHooks : public guild::sim::IStockHooks {
public:
    // VIBE_Gesetz_GetRecord(6, &rec) -> rec.threshold (+24). ComputeSalePrice
    // forms `4 - v7` where v7 == that field; default 4 == no tax adjustment.
    int SaleTaxTier() override {
        guild::world::LawRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        if (GesetzGetRecord(6, &rec))
            return rec.threshold;
        return 4;  // id out of range -> the module's inert default (no adjustment)
    }

    // VIBE_Command_QueueRequestArgs26(buildingId, field, floatBits): the original
    // passes the price/stock delta as a raw float reinterpreted into the dword
    // payload at +0x14. SyncStockLevel/AdjustStockAndNotify/DistributeGoods all
    // route their single-field building delta through here. Returns the ring/packet
    // handle (nonzero == staged; the callers check `if (!handle)`).
    int QueuePriceUpdate(guild::i32 buildingId, int field, float value) override {
        guild::i32 bits;
        std::memcpy(&bits, &value, sizeof(bits));  // float bit-pattern -> dword
        return guild::sim::QueueRequestArgs26(Q(), buildingId, field, bits);
    }
};

// --- process-lifetime wired hook instances --------------------------------
RealStockHooks g_stock{};

} // namespace

void InstallRealEconomy2Wiring() {
    HeT();  // force the shared real He pool to exist (composes with real_hooks3)
    Q();    // force the shared real command queue to exist

    // --- ai::SupervisionHooks (meister_supervision.h) ------------------------
    // SEED-FROM-DEFAULTS: start from the module's inert (non-null) stubs and
    // override only the two faithfully bindable leaves.
    guild::ai::SupervisionHooks sup = guild::ai::GetSupervisionHooks();
    sup.handler_exists = &WeHandlerExists;   // -> RealHandlerTable scan
    sup.rng_mod        = &WeRngMod;          // -> util::RandomModulo
    // queue_slot_reset28 (@0x4948c8): the reconstructed builder takes a 248-byte
    //   SlotResetScratch + PendingState body, NOT the (slotId,type,field7,kind)
    //   abstraction the supervision callers expose (same call established inert in
    //   world/wire_meister_loc.cpp for the sibling Meister3Hooks) -> inert.
    // request_build_op83 (@0x495954) / request_build_op84 (@0x495980): the real
    //   builders take a 5-/3-dword `src` array carrying the game-time tail + master
    //   id + kind bytes the original packs on the stack; the hook only passes a
    //   build/seat id, dropping that payload -> inert.
    // queue_state22 (@0x494750): the real builder copies a 124-byte delta block
    //   (entity id + field list); the hook passes only personId -> inert.
    // send_quickjump (@0x4c5d98): builds an 8 KB string-buffer Buffer28 packet
    //   (contact + body text); the hook collapses it to (msgId, ownerId) -> inert.
    // change_player_action (@0x4b09c8): never invoked by any reconstructed
    //   supervision pass in this slice -> left as the inert default stub.
    guild::ai::SetSupervisionHooks(sup);

    // --- sim::IStockHooks (building_stock.h) ----------------------------------
    // Virtual-class seed-from-defaults: install our subclass (overrides only the
    // two bindable methods; the rest inherit IStockHooks' inert base).
    guild::sim::SetStockHooks(&g_stock);
    // RoomPresent (GameObject_QueryFind scene walk) / ResolveOwnerRecord
    //   (Person_FindRecordById over the live Person array) / EnqueueGoodsTransfer
    //   (EnqueueObjectInteraction + the synchronous GetPacketStatusById poll the
    //   abstraction drops) / FinaliseCustomerUpdate (BeginDeltaPacket/AppendDelta
    //   /State22/QueueRequest39 multi-field delta chain): inert in the base class.
}

} // namespace guild::world
