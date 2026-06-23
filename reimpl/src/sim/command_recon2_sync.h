#pragma once
#include "guild/common/types.h"

// command_recon2_sync — three lockstep "scene sync" command orchestrators of the
// Die Gilde Command subsystem (namespace guild::sim). These are the high-level
// barrier-bracketed batches that the host runs to push a deterministic block of
// scene/character state onto the lockstep command stream and then spin until the
// whole batch is ACKed.
//
//   VIBE_Command_SyncSceneObjectStates    @0x500c38  (size 722)
//   VIBE_Command_SyncSceneEntryExit       @0x500f0c  (size 343)
//   VIBE_Command_SyncCharSlotAssignments  @0x501064  (size 376)
//
// WHY HOOKS (rule 6 / rule 8 boundary): each function is *pure dispatch* over
// primitives that are ALREADY reconstructed elsewhere in src/sim — the codec
// (BeginDeltaPacket / AppendDeltaField / AppendRawField / QueueRequestState22 /
// EnqueueObjectInteraction / EnqueueCmd15 / QueueRequest17), the sync-range
// barriers (command_recon_syncrange.h: MarkSyncRangeStart / MarkSyncRangeEnd,
// CheckSyncRangeAcked in command_apply10), and the send/recv/exec pump
// (CommandQueue::FlushSendQueue / ReceiveAndQueue / ExecCommands in command.h).
// The remaining dependencies are *coupled leaves* — the global entity array
// (word_12CE910, 536-byte stride), the RNG (VIBE_Math_RandomModulo /
// VIBE_Util_RandNext), the money rate helper (VIBE_Money_MultiplyByRate), and
// guild-state refresh (VIBE_Amt_RefreshGuildState). Per the cluster rules those
// are modeled as an injectable interface so the control flow, constants, opcode
// arguments and packet field offsets are reproduced 1:1 while staying
// link-clean and unit-testable in the headless build. The default hook is inert
// (deterministic stubs) and never fakes engine results — callers wire the live
// implementations.
//
// ODR: none of these three symbols, nor the SceneSyncDispatchHooks helpers below, are
// defined anywhere else under src/** (verified by address and by bare name).
// The codec/sync-range/queue primitives are REUSED, never redefined: this unit
// only declares the orchestrators and the hook vtable; it pulls the live codec
// in via command_recon2_sync.cpp's includes when GUILD has them, otherwise the
// hook indirection keeps it self-contained.

namespace guild::sim {

// --- coupled-leaf hook vtable -----------------------------------------------
// Mirrors exactly the engine calls the three orchestrators make, in the order
// and with the argument shapes recovered from the decompile. The orchestrators
// invoke ONLY these hooks plus the sync-range boundary arithmetic; nothing here
// touches real globals, so the unit links with no third-party / engine deps.
struct SceneSyncDispatchHooks {
    // word_12CE910 base + per-entity stride. The scene-object loop walks
    // [base, base + 536*count) testing the first word != 0xFFFF and byte+2 < 5.
    void*  entityArrayBase = nullptr;   // &word_12CE910
    u32    entityArrayCount = 0;        // number of 536-byte entity records

    // VIBE_Util_RandNext @0x5cb8bc — advance the shared RNG (no return used).
    void (*randNext)() = nullptr;
    // VIBE_Math_RandomModulo @0x58b89c — uniform in [0, m). Returns u16.
    u16  (*randMod)(u32 m) = nullptr;
    // VIBE_Money_MultiplyByRate @0x58f19c — scale `amount` by currency `rate`.
    i32  (*moneyRate)(i32 amount, u8 rate) = nullptr;
    // byte_6477A1 — the active currency/rate byte passed to money ops.
    u8   currencyByte = 0;

    // dword_63C7AC — guild-bank-present flag gating the bonus cash command in
    // SyncCharSlotAssignments.
    i32  guildBankFlag = 0;

    // --- codec primitives (already reconstructed; wired by the caller) -------
    // VIBE_Command_BeginDeltaPacket @0x493a94
    void (*beginDelta)(void* entityBase, u32 entityId) = nullptr;
    // VIBE_Command_AppendRawField @0x493c14 (width,count,values,fieldOffset)
    void (*appendRaw)(u8 width, u8 count, const void* values, u16 off) = nullptr;
    // VIBE_Command_AppendDeltaField @0x493aec
    void (*appendDelta)(u8 width, u8 count, const void* values, u16 off) = nullptr;
    // VIBE_Command_QueueRequestState22 @0x494750
    void (*queueState22)() = nullptr;
    // VIBE_Command_EnqueueObjectInteraction @0x4944f0 — returns ring id.
    u32  (*enqObjInteraction)(u8 a1, i32 a2, i16 a3, i32 a4,
                              i32 a5, u8 a6, u8 a7, u8 a8) = nullptr;
    // VIBE_Command_EnqueueCmd15 @0x494604
    void (*enqCmd15)(i32 a1, i32 a2, i32 a3, u8 a4) = nullptr;
    // VIBE_Command_QueueRequest17 @0x49465c
    void (*queueReq17)(i32 a1, i32 a2, i32 a3, i16 a4, u8 a5, i32 a6) = nullptr;

    // dword_11AA474 — the live entity-base global that BeginDeltaPacket stashes;
    // the field-offset args are computed relative to it (matches the binary's
    // `entityPtr + 12 - dword_11AA474` and `LOWORD(seq) + 12 - dword_11AA474`).
    // Modeled here as a base address the orchestrator subtracts.
    u32  deltaEntityBase = 0;

    // --- packet-seq lookup + lockstep pump -----------------------------------
    // VIBE_Command_GetPacketSeqById @0x4939fc
    i32  (*getPacketSeqById)(u32 ringId) = nullptr;
    // dword_6498E8 / dword_6498EC[0] — the two synced object packet seqs that
    // the orchestrators store and then re-address. Owned by the caller.
    i32* objSeqA = nullptr;
    i32* objSeqB = nullptr;

    // VIBE_Command_FlushSendQueue / ReceiveAndQueue / ExecCommands pump body.
    void (*pumpOnce)() = nullptr;
    // VIBE_Amt_RefreshGuildState @0x4becdc — alternate spin body.
    void (*refreshGuild)() = nullptr;

    // command_recon_syncrange + command_apply10 barrier:
    // VIBE_Command_MarkSyncRangeStart / End and CheckSyncRangeAcked. The
    // orchestrator calls markStart/markEnd around a batch and spins on acked().
    void (*markStart)() = nullptr;
    void (*markEnd)() = nullptr;
    bool (*acked)() = nullptr;
};

// gilde.exe 0x500c38 — VIBE_Command_SyncSceneObjectStates.
// (1) For every live scene-object record (first word != 0xFFFF && byte+2 < 5):
//     advance RNG, draw v = RandomModulo(0x122)+10 scaled by the currency rate,
//     emit a raw delta field (width 4, count 1, offset 428) and QueueRequestState22.
// (2) Two object-interaction commands (opcode 12, kind 18) bracketed by a
//     sync-range barrier; spin (FlushSend/Receive/Exec) until acked; record their
//     seqs into objSeqA/objSeqB.
// (3) Re-address those two seqs and write a 1-byte delta field (offset
//     +12 - deltaEntityBase) flipping a state byte (0 then 1); spin until acked.
// (4) 8 object spawns (opcode 0, kind 16+rand(8), arg=rand(3)+4); spin on
//     RefreshGuildState until acked.
// (5) 8 object removals (opcode 11, kind 16+rand(8), arg7=19) each followed by
//     QueueRequest17(-2,-1,1,340,currency,0); spin on RefreshGuildState until acked.
// Returns the final acked() result (non-zero) exactly as the binary returns eax.
int SyncSceneObjectStates(const SceneSyncDispatchHooks& h);

// gilde.exe 0x500f0c — VIBE_Command_SyncSceneEntryExit.
// The "enter/exit" subset of the above: two object-interaction commands
// (opcode 12, kind 18) under a sync-range barrier, spin until acked, then two
// 1-byte delta packets (state 0 then 1) under a second barrier, spin until
// acked. `entryEntityId` is the entity the first interaction targets (the
// original passes `this`/v2 = the scene entity id). Returns the acked result.
int SyncSceneEntryExit(const SceneSyncDispatchHooks& h, i32 entryEntityId);

// gilde.exe 0x501064 — VIBE_Command_SyncCharSlotAssignments.
// Picks a random start index r = rand(slotCount) and a stride (rand(2)?1:3),
// then for `count` iterations: linear-probe the slot table at `slotTable`
// (slotCount entries) from r by stride for the first non-zero slot, clear it,
// emit an object-interaction (opcode 5, kind 16, arg6=slotByte, arg8=2), a
// money command EnqueueCmd15(-2,-1, MoneyRate(750,currency), currency), and —
// when guildBankFlag is set — a second EnqueueCmd15 of
// MoneyRate(rand(0x1388)+5000, currency). All under one sync-range barrier;
// spin (FlushSend/Receive/Exec) until acked. Returns the acked result.
//
// `count` = a1 (iteration count), `slotCount` = a2 (probe modulus), `slotTable`
// = a3 (base of the i32 slot array). The clobbered slot byte is the low byte of
// the cleared slot word.
int SyncCharSlotAssignments(const SceneSyncDispatchHooks& h, i32 count, i32 slotCount,
                            i32* slotTable);

} // namespace guild::sim
