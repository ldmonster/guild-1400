// ===========================================================================
// VIBE_Ai_CalcBankmeister @0x459264 — 1:1 reconstruction. See ai_meister_bank.h
// for the algorithm overview, field map, and the boundary/leaf notes.
// ===========================================================================
#include "sim/ai_meister_bank.h"

#include <cstring>

#include "sim/entity.h"
#include "sim/ai_meister_internal.h" // aimei::resolveHandle/makeObjHandle (REUSED, hdr-only)
#include "sim/ai_recon_brain.h"   // BankmeisterNewRate / ReserveTier / FinalBalance (REUSED)
#include "world/law.h"            // guild::world::GesetzGetRecord / LawRecord (REUSED)
#include "world/law_types.h"
#include "util/math_random.h"     // guild::util::RandomModulo (== Math_RandomModulo)

namespace guild::sim {

// --- captured-boundary / leaf globals (definitions) -------------------------
BankCmdSink*        g_bankCmdSink = nullptr;
const BankAiLeaves* g_bankLeaves  = nullptr;

void ResetBankAiState() {
    g_bankCmdSink = nullptr;
    g_bankLeaves  = nullptr;
}

namespace {

// raw little-endian field access at byte offset (records are packed).
inline i32 rd32(const void* b, int off) {
    i32 v; std::memcpy(&v, static_cast<const u8*>(b) + off, 4); return v;
}
inline u16 rdu16(const void* b, int off) {
    u16 v; std::memcpy(&v, static_cast<const u8*>(b) + off, 2); return v;
}
inline u8 rd8(const void* b, int off) { return *(static_cast<const u8*>(b) + off); }
inline void wr8(void* b, int off, u8 v) { *(static_cast<u8*>(b) + off) = v; }

// --- record offsets (symbol - base), recovered from the disasm ---------------
// Meister (person) record:
constexpr int kM_bldgRec  = 0x16C;  // (+364) building record ptr  (decompile esi+16Ch)
constexpr int kM_budget   = 0x1B8;  // (+440) budget / cash dword   (esi+1B8h)
constexpr int kM_flags2   = 0x1C8;  // (+456) per-tick flags byte 2 (esi+1C8h, bit 8)
// Building record:
constexpr int kB_id1      = 0x01;   // *(bldg+1)   building id (unaligned dword)
constexpr int kB_owner39  = 0x27;   // *(u16*)(bldg+0x27) owner / player word
constexpr int kB_cash101  = 0x65;   // *(int*)(bldg+0x65) held cash / reserve
constexpr int kB_root93   = 0x5D;   // *(bldg+0x5D) scene root id
// Person record (owner lookup):
constexpr int kP_kind     = 0x02;   // byte_12CE912 (person kind / profession byte)

// byte_6477A1 == 0 (the default-currency denomination byte the cmd15 legs carry).
constexpr u8 kCurrencyByte = 0;

// The FP scaling/threshold constants (dbl_6198C8 1.03 / 6198D0 1.5 / 6198D8 2.25
// / 6198E0 0.8, all get_bytes-verified) and the VIBE_Coord_ConvertX truncation
// (frndint, round-toward-zero) live inside the reused pure cores in
// ai_recon_brain.h (BankmeisterReserveTier/FinalBalance use (int)(double*k), which
// truncates toward zero exactly like ConvertX).

// owner person actor id == g_personIds[owner] (the binary's dword_12CE914 column
// at the interleaved 536*owner offset; the reimpl flattens it to g_personIds[]).
inline i32 ownerActorId(u16 owner) {
    if (owner >= static_cast<u16>(kPersonCapacity)) return 0;
    return g_personIds[owner];
}
inline u8 ownerKind(u16 owner) {
    if (owner >= static_cast<u16>(kPersonCapacity)) return 0;
    return rd8(&g_persons[owner], kP_kind);
}

// --- captured command emitters (the lockstep queue boundary) -----------------
// "set the bank's cash-reserve column to `value`" (BeginDeltaPacket +
// AppendDeltaField(4,1,&value,col=0x65) + QueueRequestState22). The delta base
// (dword_11AA474) cancels the building-pointer low word, leaving col == 0x65.
void emitReserveSet(i32 buildingId, i32 value) {
    if (!g_bankCmdSink) return;
    BankCommand c;
    c.kind          = BankCommand::kReserveSet;
    c.buildingId    = buildingId;
    c.reserveValue  = value;
    c.reserveColumn = static_cast<u16>(kB_cash101); // 0x65
    g_bankCmdSink->push(c);
}

// a MINT/MELT coin op (EnqueueBuildingActionStart("Bankmeister") + two cmd15 legs
// + EnqueueBuildingActionEnd). The two cmd15 legs are the credit/debit pair
// (actor <-> building); we capture the logical op once.
void emitCoinOp(bool mint, i32 amount, i32 actorId, i32 buildingId, u8 denom) {
    if (!g_bankCmdSink) return;
    BankCommand c;
    c.kind       = BankCommand::kCoinOp;
    c.mint       = mint;
    c.amount     = amount;
    c.actorId    = actorId;
    c.buildingId = buildingId;
    c.coinDenom  = denom;
    g_bankCmdSink->push(c);
}

inline int heCount(int filterCode, i32 key) {
    if (g_bankLeaves && g_bankLeaves->heCountMatching)
        return g_bankLeaves->heCountMatching(filterCode, key);
    return 0; // null hook: no matching handlers (FindFirst returns 0)
}
inline int coinCount(i32 sceneRootId, u8 denom) {
    if (g_bankLeaves && g_bankLeaves->coinCountAtLocation)
        return g_bankLeaves->coinCountAtLocation(sceneRootId, denom);
    return 0; // null hook: no coins at location
}

} // namespace

// ===========================================================================
// 0x459264 — VIBE_Ai_CalcBankmeister.
// ===========================================================================
int CalcBankmeister(u8* meisterRec) {
    int result = 0; // orig returns the eax passthrough / last cmd15 result.

    // 0x45926c: skip if flags2 bit 8 already set.
    if (rd8(meisterRec, kM_flags2) & 8)
        return result;

    // 0x45927c: read law-record 13; bail if missing.  (v42 == LawRecord.threshold)
    guild::world::LawRecord law;
    if (!guild::world::GesetzGetRecord(13, &law))
        return result;
    const i32 lawTarget = law.threshold;            // v42 (record +0x18) == 16 for law 13

    // building record + ids. The +0x16C column stores a *record pointer* in the
    // 32-bit original; the reimpl uses the wave-19 64-bit-safe handle model
    // (aimei::resolveHandle: 1+objIdx / 0x40000000+personIdx, 0==null). Fields are
    // then read off `bldg` at the byte offsets the decompile uses.
    const u8* bldg = aimei::resolveHandle(rd32(meisterRec, kM_bldgRec));
    if (!bldg) return result; // original would deref a real ptr; null == no bank rec
    const i32 bldgId    = rd32(bldg, kB_id1);        // *(bldg+1)
    const u16 bldgOwner = rdu16(bldg, kB_owner39);   // *(u16*)(bldg+0x27)
    const i32 bldgCash  = rd32(bldg, kB_cash101);    // *(int*)(bldg+0x65)
    const i32 sceneRoot = rd32(bldg, kB_root93);     // *(bldg+0x5D)
    const i32 budget    = rd32(meisterRec, kM_budget); // *(m+0x1B8)

    // 0x459298: count pending credit/loan handler entries (He probe).
    //   FindFirstHandlerByFilter(2,0,0x23,3,bldgId) then iterate; i == hit count.
    const int handlerCount = heCount(0x23, bldgId);

    // ---- reserve (interest-rate) adjust toward lawTarget ----------------------
    // The pure state machine lives in ai_recon_brain.h (BankmeisterNewRate,
    // REUSED). It returns the new rate and is byte-exact for every clamp. We must
    // preserve the original's RNG draw ORDER: only the single branch that runs
    // draws ONE VIBE_Math_RandomModulo(3). We pre-decide which branch the core
    // will take so we draw exactly once into the matching arg (the other is 0 and
    // is never read by that branch). |diff|>3 takes no draw.
    {
        const i32 absDiff = (bldgCash - lawTarget) < 0 ? (lawTarget - bldgCash)
                                                       : (bldgCash - lawTarget);
        i32 randDown = 0, randUp = 0;
        if (absDiff <= 3) {
            if (handlerCount == 0 && (lawTarget - 2) < bldgCash) {
                randDown = static_cast<int>(static_cast<u16>(guild::util::RandomModulo(3)));
            } else if (handlerCount > 3 && bldgCash < lawTarget) {
                randUp = static_cast<int>(static_cast<u16>(guild::util::RandomModulo(3)));
            }
        }
        const i32 newRate =
            BankmeisterNewRate(bldgCash, lawTarget, handlerCount, randUp, randDown);
        // A command is emitted only when the rate actually changes (the original
        // skips the delta packet on the "hold" paths; newRate == bldgCash there).
        if (newRate != bldgCash)
            emitReserveSet(bldgId, newRate);
    }

    // ---- per-denomination coin (vault tier) balancing (loop i = 1..3) ---------
    // The pure per-tier decision is BankmeisterReserveTier (ai_recon_brain.h,
    // REUSED). netMoved (ebp/v10) accumulates the RAW deltas: +rawDelta on a
    // buy/mint, -rawDelta on a sell/melt.
    const i32 ownerId = ownerActorId(bldgOwner);
    i32 netMoved = 0;                                  // ebp accumulator (v10)
    for (int i = 1; i < 4; ++i) {
        const u8 denom = static_cast<u8>(i);          // v11 = (char)i
        const i32 cnt = coinCount(sceneRoot, denom);  // CountAtLocation(*(bldg+0x5D))
        const BankReserveOp op = BankmeisterReserveTier(cnt, budget);
        if (op.kind == BankReserveOp::Buy) {
            netMoved += op.rawDelta;                   // ebp += v47
            emitCoinOp(/*mint=*/true, op.amount, ownerId, bldgId, denom);
        } else if (op.kind == BankReserveOp::Sell) {
            netMoved -= op.rawDelta;                   // ebp -= ecx
            emitCoinOp(/*mint=*/false, op.amount, ownerId, bldgId, denom);
        }
        // None: in-band -> no op for this denomination.
    }

    // ---- final capital top-up / draw-down -------------------------------------
    // 0x45941a: recount the default-currency denomination (byte_6477A1 == 0), then
    // the pure BankmeisterFinalBalance (REUSED). The 6/7 gate reads the OWNER
    // person's kind byte (0x459660: byte_12CE912[536*owner]).
    const i32 cnt = coinCount(sceneRoot, kCurrencyByte);
    const u8 kind = ownerKind(bldgOwner);
    const bool blocksSell = (kind == 6 || kind == 7);
    const BankReserveOp fin = BankmeisterFinalBalance(cnt, budget, netMoved, blocksSell);
    if (fin.kind == BankReserveOp::Buy)
        emitCoinOp(/*mint=*/true, fin.amount, ownerId, bldgId, kCurrencyByte);
    else if (fin.kind == BankReserveOp::Sell)
        emitCoinOp(/*mint=*/false, fin.amount, ownerId, bldgId, kCurrencyByte);

    // 0x4594ab: mark done-this-tick.
    wr8(meisterRec, kM_flags2, static_cast<u8>(rd8(meisterRec, kM_flags2) | 8));
    return result;
}

} // namespace guild::sim
