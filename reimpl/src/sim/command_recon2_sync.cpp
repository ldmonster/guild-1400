// command_recon2_sync — implementation of the three lockstep scene-sync command
// orchestrators (namespace guild::sim). Strict 1:1 translation of the Hex-Rays
// decompile at the addresses noted per function. Coupled leaves (entity array
// iteration, RNG, money rate, codec primitives, lockstep pump, sync-range
// barrier) are reached only through SceneSyncDispatchHooks so the control flow,
// constants, opcode arguments and field offsets are reproduced exactly while
// the unit links with no engine/third-party dependency.
//
// The codec/sync-range/queue primitives these dispatch to are ALREADY
// reconstructed (command_codec, command_inherit, command_recon_syncrange,
// command_apply10, command.h); they are reused via the hook vtable rather than
// redefined (ODR rule).

#include "sim/command_recon2_sync.h"

#include <cstring>

namespace guild::sim {

namespace {

// Inert default RNG/leaf helpers: deterministic, side-effect-free. They never
// fabricate engine state — they exist so the orchestrators are exercisable in
// isolation; live wiring replaces them.
u16 InertRandMod(u32 m) { return m ? static_cast<u16>(0) : static_cast<u16>(0); }
i32 InertMoneyRate(i32 amount, u8) { return amount; }
i32 InertGetSeq(u32 ringId) { return static_cast<i32>(ringId); }
u32 InertEnqObj(u8, i32, i16, i32, i32, u8, u8, u8) { return 0; }

// Convenience accessors that fall back to the inert defaults when a hook is
// null, so callers may wire only the subset they need.
inline u16 RandMod(const SceneSyncDispatchHooks& h, u32 m) {
    return h.randMod ? h.randMod(m) : InertRandMod(m);
}
inline void RandNext(const SceneSyncDispatchHooks& h) { if (h.randNext) h.randNext(); }
inline i32 MoneyRate(const SceneSyncDispatchHooks& h, i32 a, u8 r) {
    return h.moneyRate ? h.moneyRate(a, r) : InertMoneyRate(a, r);
}
inline void BeginDelta(const SceneSyncDispatchHooks& h, void* base, u32 id) {
    if (h.beginDelta) h.beginDelta(base, id);
}
inline void AppendRaw(const SceneSyncDispatchHooks& h, u8 w, u8 c, const void* v, u16 o) {
    if (h.appendRaw) h.appendRaw(w, c, v, o);
}
inline void AppendDelta(const SceneSyncDispatchHooks& h, u8 w, u8 c, const void* v, u16 o) {
    if (h.appendDelta) h.appendDelta(w, c, v, o);
}
inline void State22(const SceneSyncDispatchHooks& h) { if (h.queueState22) h.queueState22(); }
inline u32 EnqObj(const SceneSyncDispatchHooks& h, u8 a1, i32 a2, i16 a3, i32 a4,
                  i32 a5, u8 a6, u8 a7, u8 a8) {
    return h.enqObjInteraction ? h.enqObjInteraction(a1, a2, a3, a4, a5, a6, a7, a8)
                               : InertEnqObj(a1, a2, a3, a4, a5, a6, a7, a8);
}
inline void EnqCmd15(const SceneSyncDispatchHooks& h, i32 a1, i32 a2, i32 a3, u8 a4) {
    if (h.enqCmd15) h.enqCmd15(a1, a2, a3, a4);
}
inline void Req17(const SceneSyncDispatchHooks& h, i32 a1, i32 a2, i32 a3, i16 a4, u8 a5, i32 a6) {
    if (h.queueReq17) h.queueReq17(a1, a2, a3, a4, a5, a6);
}
inline i32 GetSeq(const SceneSyncDispatchHooks& h, u32 id) {
    return h.getPacketSeqById ? h.getPacketSeqById(id) : InertGetSeq(id);
}
inline void Pump(const SceneSyncDispatchHooks& h) { if (h.pumpOnce) h.pumpOnce(); }
inline void Refresh(const SceneSyncDispatchHooks& h) { if (h.refreshGuild) h.refreshGuild(); }
inline void MarkStart(const SceneSyncDispatchHooks& h) { if (h.markStart) h.markStart(); }
inline void MarkEnd(const SceneSyncDispatchHooks& h) { if (h.markEnd) h.markEnd(); }
inline bool Acked(const SceneSyncDispatchHooks& h) { return h.acked ? h.acked() : true; }

// The scene-object entity record stride (sizeof a word_12CE910 element block).
constexpr u32 kEntityStride = 536;       // 0x218
// Field offset of the first 16-bit "id" word inside a record.
constexpr u32 kStateByteOff = 2;         // record byte +2 (< 5 == "active")

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x500c38 — VIBE_Command_SyncSceneObjectStates
// ---------------------------------------------------------------------------
int SyncSceneObjectStates(const SceneSyncDispatchHooks& h) {
    // for ( i = 0; i != 16; v17[i] = -1 ) ++i;  — zero/sentinel the 16-slot
    // ack-tracking array; v17/v18/v19 share that stack region. We track the two
    // synced object packet seqs (v18=objSeqA, v19=objSeqB) explicitly.
    i32 seqLocalA = -1;   // v18
    i32 seqLocalB = -1;   // v19

    RandNext(h);          // VIBE_Util_RandNext()

    // do { ... } while (v3 != &unk_12D0A90);  — walk every entity record.
    auto* base = static_cast<u8*>(h.entityArrayBase);
    if (base) {
        for (u32 e = 0; e < h.entityArrayCount; ++e) {
            u8* rec = base + static_cast<size_t>(e) * kEntityStride;
            const u16 firstWord = static_cast<u16>(rec[0] | (rec[1] << 8));
            const i8  stateByte = static_cast<i8>(rec[kStateByteOff]);
            if (firstWord != 0xFFFF && stateByte < 5) {
                // v5 = RandomModulo(0x122); v20 = MoneyMultiplyByRate(v5+10, byte_6477A1);
                const u16 r = RandMod(h, 0x122u);
                i32 v20 = MoneyRate(h, static_cast<i32>(r) + 10, h.currencyByte);
                // BeginDeltaPacket(rec, *(u32*)(rec+4));
                u32 entId;
                std::memcpy(&entId, rec + 4, sizeof(entId));
                BeginDelta(h, rec, entId);
                // AppendRawField(4, 1, &v20, 428);
                AppendRaw(h, 4u, 1u, &v20, 428u);
                State22(h);
            }
        }
    }

    // --- pass 1: two object-interaction commands under a barrier ------------
    MarkStart(h);
    const u32 p1a = EnqObj(h, 12, -1, 18, -1,  0 /*v7 uninit in original->0*/, 0, 0, 0);
    const u32 p1b = EnqObj(h, 12, -1, 18, -1, -1, 0, 0, 0);
    MarkEnd(h);
    while (!Acked(h)) Pump(h);

    seqLocalA = GetSeq(h, p1a);
    seqLocalB = GetSeq(h, p1b);
    if (h.objSeqA) *h.objSeqA = seqLocalA;
    if (h.objSeqB) *h.objSeqB = seqLocalB;

    // --- pass 2: flip a state byte on each synced packet --------------------
    MarkStart(h);
    {
        i32 v21 = 0;            // LOBYTE(v21[0]) = v9 (uninit in orig); low byte used
        u8 b0 = 0;              // value flipped to 0 then 1
        std::memcpy(&v21, &b0, 1);
        // BeginDeltaPacket(seqA, *(u32*)(seqA+4)); the field offset in the
        // original is (seqA + 12 - dword_11AA474): low part of the seq plus the
        // record header, relative to the live entity base.
        BeginDelta(h, &seqLocalA, static_cast<u32>(seqLocalA));
        const u16 offA = static_cast<u16>((static_cast<u32>(seqLocalA) + 12u) - h.deltaEntityBase);
        AppendDelta(h, 1u, 1u, &v21, offA);
        State22(h);

        b0 = 1;
        std::memcpy(&v21, &b0, 1);
        BeginDelta(h, &seqLocalB, static_cast<u32>(seqLocalB));
        const u16 offB = static_cast<u16>(((static_cast<u32>(seqLocalB) & 0xFFFFu) + 12u) - h.deltaEntityBase);
        AppendDelta(h, 1u, 1u, &v21, offB);
        State22(h);
    }
    MarkEnd(h);
    while (!Acked(h)) Pump(h);

    // --- pass 3: 8 object spawns -------------------------------------------
    seqLocalA = -1;
    seqLocalB = -1;
    MarkStart(h);
    for (int j = 0; j != 8; ++j) {
        const u8  arg6 = static_cast<u8>(RandMod(h, 3u) + 4);   // v16 = rand(3)+4
        const i16 kind = static_cast<i16>(RandMod(h, 8u));      // v12 = rand(8)
        // v17[j] = EnqueueObjectInteraction(0, -1, kind+16, -1, -1, arg6, 0, 0);
        EnqObj(h, 0, -1, static_cast<i16>(kind + 16), -1, -1, arg6, 0, 0);
    }
    MarkEnd(h);
    while (!Acked(h)) Refresh(h);

    // --- pass 4: 8 object removals + a price-request each -------------------
    MarkStart(h);
    for (int k = 0; k != 8; ++k) {
        const i16 kind = static_cast<i16>(RandMod(h, 8u));      // v14 = rand(8)
        // v17[k+1] = EnqueueObjectInteraction(11, -1, kind+16, -1, -1, 0, 19, 0);
        EnqObj(h, 11, -1, static_cast<i16>(kind + 16), -1, -1, 0, 19, 0);
        // QueueRequest17(-2, -1, 1, 340, byte_6477A1, 0);
        Req17(h, -2, -1, 1, 340, h.currencyByte, 0);
    }
    MarkEnd(h);
    int result = Acked(h) ? 1 : 0;
    while (!result) { Refresh(h); result = Acked(h) ? 1 : 0; }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x500f0c — VIBE_Command_SyncSceneEntryExit
// ---------------------------------------------------------------------------
int SyncSceneEntryExit(const SceneSyncDispatchHooks& h, i32 entryEntityId) {
    // for ( i = 0; i != 16; v7[i] = -1 ) ++i;  v7[0] = this;  — ack tracker; slot 0
    // is seeded with `this` (entryEntityId) but the array is NEVER read again, so
    // entryEntityId is observably dead. (Disasm 0x500f10: edx = -1 for the init
    // loop and is NOT reloaded; it is still -1 at both EnqObj calls.)
    (void)entryEntityId;
    i32 seqLocalA = -1;   // v8
    i32 seqLocalB = -1;   // v9

    MarkStart(h);
    // FIX (gilde.exe 0x500f30..0x500f3d): the first EnqObj's v2 args are edx, and
    // edx == -1 (0FFFFFFFFh from 0x500f10), NOT `this`. Hex-Rays mislabels v2 as
    // uninit/`this`; the disasm shows ebx=edx=-1, push edx(-1). Both calls are
    // identical: EnqueueObjectInteraction(12, -1, 18, -1, -1, 0,0,0).
    const u32 p1a = EnqObj(h, 12, -1, 18, -1, -1, 0, 0, 0);
    // v9 = EnqueueObjectInteraction(12, -1, 18, -1, -1, 0,0,0);
    const u32 p1b = EnqObj(h, 12, -1, 18, -1, -1, 0, 0, 0);
    MarkEnd(h);
    while (!Acked(h)) Pump(h);

    seqLocalA = GetSeq(h, p1a);
    seqLocalB = GetSeq(h, p1b);
    if (h.objSeqA) *h.objSeqA = seqLocalA;
    if (h.objSeqB) *h.objSeqB = seqLocalB;

    MarkStart(h);
    {
        i32 v10 = 0;
        u8  b0 = 0;
        std::memcpy(&v10, &b0, 1);                  // LOBYTE(v10) = 0
        BeginDelta(h, &seqLocalA, static_cast<u32>(seqLocalA));
        const u16 offA = static_cast<u16>((static_cast<u32>(seqLocalA) + 12u) - h.deltaEntityBase);
        AppendDelta(h, 1u, 1u, &v10, offA);
        State22(h);

        b0 = 1;
        std::memcpy(&v10, &b0, 1);                  // LOBYTE(v10) = 1
        BeginDelta(h, &seqLocalB, static_cast<u32>(seqLocalB));
        const u16 offB = static_cast<u16>(((static_cast<u32>(seqLocalB) & 0xFFFFu) + 12u) - h.deltaEntityBase);
        AppendDelta(h, 1u, 1u, &v10, offB);
        State22(h);
    }
    MarkEnd(h);

    int result = Acked(h) ? 1 : 0;
    while (!result) { Pump(h); result = Acked(h) ? 1 : 0; }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x501064 — VIBE_Command_SyncCharSlotAssignments
// (a1=count, a2=slotCount, a3=slotTable)
// ---------------------------------------------------------------------------
int SyncCharSlotAssignments(const SceneSyncDispatchHooks& h, i32 count, i32 slotCount,
                            i32* slotTable) {
    // v5 = RandomModulo(slotCount);  start index.
    i32 v5 = static_cast<i32>(RandMod(h, static_cast<u32>(slotCount)));
    // stride: RandomModulo(2) ? 1 : 3.
    const i32 stride = RandMod(h, 2u) ? 1 : 3;     // v17

    MarkStart(h);
    // Loop bound recovered from disasm 0x501099/0x5010ad: ecx = a1 (count),
    // `test ecx,ecx; jle` -> `if (count > 0)`; v19 (the counter) is `xor edx,edx`
    // = 0 at 0x5010a2 -> runs `count` iterations from 0 while v19 < count.
    //
    // v20 (slotByte) is a single stack slot ([esp+Ch]) declared OUTSIDE the loop in
    // the binary; it is NOT reset per iteration. When the inner probe loop exhausts
    // all `slotCount` slots without finding a non-zero one (--v9 hits 0 -> LABEL_7),
    // v20 KEEPS the value from the previous iteration (uninitialized garbage on the
    // first such failure). So slotByte must persist across iterations.
    u8 slotByte = 0;                                // v20 (persists across iterations)
    for (i32 iter = 0; iter < count; ++iter) {
        if (slotCount) {
            i32 probe = slotCount;                  // v9
            while (true) {
                i32* slot = slotTable + v5;         // (a3 + 4*v5)
                if (*slot) {                        // if (*v10) break;
                    slotByte = static_cast<u8>(*slot & 0xFF);   // v20 = *(u8*)v10
                    *slot = 0;                                  // *v10 = 0
                    break;
                }
                --probe;
                v5 = (v5 + stride) % slotCount;     // (v5 + v17) % a2
                if (!probe) break;
            }
        }
        // EnqueueObjectInteraction(5, -1, 16, -1, -1, slotByte, 0, 2);
        EnqObj(h, 5, -1, 16, -1, -1, slotByte, 0, 2);
        // v11 = MoneyMultiplyByRate(750, byte_6477A1);
        const i32 v11 = MoneyRate(h, 750, h.currencyByte);
        // EnqueueCmd15(-2, -1, v11, byte_6477A1);
        EnqCmd15(h, -2, -1, v11, h.currencyByte);
        if (h.guildBankFlag) {                      // if (dword_63C7AC)
            const u16 r = RandMod(h, 0x1388u);      // RandomModulo(0x1388)
            const i32 v14 = MoneyRate(h, static_cast<i32>(r) + 5000, h.currencyByte);
            EnqCmd15(h, -2, -1, v14, h.currencyByte);
        }
    }
    MarkEnd(h);

    int result = Acked(h) ? 1 : 0;
    while (!result) { Pump(h); result = Acked(h) ? 1 : 0; }
    return result;
}

} // namespace guild::sim
