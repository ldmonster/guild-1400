#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/types.h"

// gilde.exe — Command APPLY handlers (the receive/apply side of the lockstep
// command system). namespace guild::sim.
//
// These are a representative, coherent subset of the ~120 VIBE_Command_Ex*
// functions (range ~0x496000..0x49d400) that the dispatch jump table at
// 0x631298 invokes from VIBE_Command_ExecCommands. The CODEC half (build /
// encode / size / enqueue / sequence and the delta ENCODER) lives in
// command.{h,cpp} and command_codec.{h,cpp}; this file is the side that decodes
// a received packet payload and mutates world/entity state.
//
// Common ABI recovered from the originals (all are __usercall):
//   * The packet record base arrives in EAX; we model it as `CommandPacket&`.
//     The opcode-specific payload begins at byte offset +0x10 (kFPayload).
//   * The 10-byte ACK/status entry arrives in EDX (the AckEntry* the dispatcher
//     hands each handler). Every handler stamps it: status=2 (in-progress),
//     +1=0, +6=0 on entry for the "long" handlers, then status=1 on success.
//     A few set status directly to (result==0)+1. A null ack is tolerated.
//   * Return value: 0 == applied, 1/2 == target not found / rejected. The
//     dispatcher ignores the return; the ACK byte carries the outcome.
//
// Determinism: each handler reads the same payload bytes and performs the same
// integer/float arithmetic + clamps as the binary, so applying an identical
// command sequence reproduces byte-identical entity records on every peer.
//
// Handlers that need deep render/cutscene/AI/He/Office/Beweis leaf subsystems
// call those leaves through mockable function-pointer hooks (declared below);
// the leaves themselves are deferred (see the module report). In production the
// host installs real implementations; tests install spies.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Opcode -> handler constants (indices into the 96-entry jump table @0x631298).
// Reuses the wire opcode numbers; names match the recovered handler symbols.
// ---------------------------------------------------------------------------
enum ApplyOpcode : u8 {
    kOpPatchObjectFieldsAdd     = 0x16, // 22  ExPatchObjectFieldsAdd
    kOpWriteObjectFields        = 0x17, // 23  ExWriteObjectFields
    kOpApplyNeedDeltas          = 0x18, // 24  ExApplyNeedDeltas
    kOpPatchObjectBitfield      = 0x19, // 25  ExPatchObjectBitfield
    kOpAddObjectFloatField      = 0x1A, // 26  ExAddObjectFloatField
    kOpRegisterIdPair           = 0x24, // 36  ExRegisterIdPair
    kOpUnregisterIdPair         = 0x25, // 37  ExUnregisterIdPair
    kOpAdjustObjectCounter      = 0x5A, // 90  ExAdjustObjectCounter
    kOpAdjustCharacterReputation= 0x5B, // 91  ExAdjustCharacterReputation
    kOpSetObjectFillLevel       = 0x5D, // 93  ExSetObjectFillLevel
    kOpEndTurn                  = 0x56, // 86  ExEndTurn (delegating)
    kOpAck                      = 0x41, // 65  ExAck    (delegating)
};

// ---------------------------------------------------------------------------
// Field-patch payload layout (opcodes 0x16/0x17/0x19/0x1A and the codec's
// AppendDeltaField / AppendRawField). Recovered from the handler loops:
//   +0x10 : target entity id (dword). Special remap ids -2/-3/-4 are replaced
//           by the last-created object/scene/trade ids (g_lastObjectId etc).
//   +0x14 : field count (byte) for the multi-field handlers (0x16/0x17), OR
//           for the single-field bitfield/float handlers the offset/value live
//           at fixed positions (see below).
//   +0x15 : start of (width:1, count:1, offset:2, values[width*count]) records.
// ---------------------------------------------------------------------------
enum ApplyField : u32 {
    kAfEntityId    = 0x10, // dword: target entity id (or -2/-3/-4 remap token)
    kAfFieldCount  = 0x14, // byte:  number of (w,c,off,vals) records following
    kAfRecords     = 0x15, // first patch record
};

// "Last-created id" remap registers (dword_631288/63128C/631290 in the binary).
// Field-patch packets may carry the sentinel id -2/-3/-4 meaning "the object /
// scene-node / trade-entry the previous command just created". The dispatcher
// fills these as side effects of the create handlers. Exposed for tests.
extern i32 g_lastObjectId;   // dword_631288  (id == -2 resolves to this)
extern i32 g_lastSceneId;    // dword_63128C  (id == -3)
extern i32 g_lastTradeId;    // dword_631290  (id == -4)

// Global id-pair association table (dword_11C2160 / dword_11C2164, 2048 pairs,
// 4096 dwords each). Used by the evidence/"Beweis" subsystem; opcodes 0x24/0x25
// insert and remove pairs. Modeled directly so the apply round-trips.
constexpr int kIdPairSlots = 2048;            // 4096 dwords / 2
extern i32 g_idPairA[kIdPairSlots];           // dword_11C2160[2*slot]
extern i32 g_idPairB[kIdPairSlots];           // dword_11C2164[2*slot]
void ResetIdPairTable();                       // test/setup helper (not in orig)

// ---------------------------------------------------------------------------
// Mockable leaf hooks (the deferred render/cutscene/AI/Office/Beweis calls).
// Default no-op/identity behavior keeps the self-contained handlers testable.
// ---------------------------------------------------------------------------

// VIBE_Beweis_FindOrAllocSlot @0x4c347c — find/allocate an evidence pair slot for
// (id, kind); returns slot index or -1. Default: linear find-or-append into the
// id-pair table (faithful enough for the apply path + tests).
using BeweisAllocFn = int (*)(i32 kind, i32 id);
void SetBeweisAllocHook(BeweisAllocFn fn);

// VIBE_GameTick_HandleTurnControlCommand @0x5799a8 — turn-control state machine.
// Returns nonzero if the turn command was accepted. Default: returns 1.
using TurnControlFn = int (*)();
void SetTurnControlHook(TurnControlFn fn);

// VIBE_City_ApplyStatsFromAck @0x5792e0 — apply pending city-stat snapshot.
// Default: no-op. Installed by the city module / spied by tests.
using CityAckFn = void (*)();
void SetCityAckHook(CityAckFn fn);

// ---------------------------------------------------------------------------
// The handlers. Each takes the received packet and the dispatcher's ACK entry
// (may be null). Signature matches CommandQueue::Handler once bound (see
// RegisterApplyHandlers) — the queue passes (CommandQueue&, pkt, ack); these
// free functions drop the queue ref via the thin adapters in the .cpp.
// ---------------------------------------------------------------------------

// gilde.exe 0x497c18 — opcode 0x16. Resolve entity by id (+0x10, with -2/-3/-4
// remap); then for each of byte(+0x14) records, ADD the per-element delta values
// at the record's offset into the entity (width 1/2/4). Receive side of
// AppendDeltaField. Returns 0 on apply, 1 if the entity id is unknown.
int ExPatchObjectFieldsAdd(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x497da4 — opcode 0x17. Same payload shape as 0x16 but WRITES the
// absolute values (receive side of AppendRawField/AppendCopiedField).
int ExWriteObjectFields(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x497ed0 — opcode 0x18. AI need-delta apply: resolve entity (person)
// by id; for each of byte(+0x14) 5-byte entries [statId:1][float:4], add the
// float to the need stat at entity + 144 + 12*statId, then clamp to [0, 1000].
int ExApplyNeedDeltas(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498024 — opcode 0x19. Resolve entity; clear-then-set a bitfield at
// offset(+0x14 dword) of width(+0x18 dword 1/2/4): field = (field & ~mask) | set
// where set is at +0x1C and mask at +0x20 (per width).
int ExPatchObjectBitfield(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x498104 — opcode 0x1A. Resolve entity; ADD a float (+0x18) to the
// float at byte offset(+0x14 dword) of the entity.
int ExAddObjectFloatField(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x4991ec — opcode 0x24. Register an id pair (id@+0x10, kind@+0x14)
// into the evidence table via the Beweis hook.
int ExRegisterIdPair(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x499230 — opcode 0x25. Remove the id pair (A==+0x14, B==+0x10) from
// the table (sets both columns to -1). Returns 1 if not found.
int ExUnregisterIdPair(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49d080 — opcode 0x5A. Person record (+0x10 id): add value(+0x14) to
// the dword at person+0x194; clamp to [0, 50].
int ExAdjustObjectCounter(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49d0e4 — opcode 0x5B. Person record (+0x10 id): add value(+0x14) to
// the byte at person+0x1B1 (reputation); clamp to [0, 254] (stored 0..254). The
// HUD-refresh and Office-clear side effects are routed through hooks.
int ExAdjustCharacterReputation(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49d1fc — opcode 0x5D. Person record (+0x10 id): add value(+0x18) to
// the byte at person+0x80+index where index=(+0x14 dword, must be <=4); clamp to
// [0, 252]. (The original routes the clamped float through VIBE_Coord_ConvertX, a
// no-op rounding for our integer result; documented but not needed.)
int ExSetObjectFillLevel(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49cec0 — opcode 0x56. Turn-control: stamp ack, delegate to the
// turn-control hook, set status=1 on accept. Self-contained modulo the hook.
int ExEndTurn(CommandPacket& pkt, AckEntry* ack);

// gilde.exe 0x49bcd0 — opcode 0x41. Apply pending city stats from the ack via the
// city hook; stamp ack success.
int ExAck(CommandPacket& pkt, AckEntry* ack);

// ---------------------------------------------------------------------------
// Wire the translated handlers into a CommandQueue's dispatch table. Opcodes we
// did not translate are left unset (the queue treats them as safe no-ops).
// ---------------------------------------------------------------------------
void RegisterApplyHandlers(CommandQueue& q);

// Apply a single packet directly (bypassing the queue), for tests and for the
// standalone local-apply path. Looks the opcode up in the translated set and
// invokes it; unknown/guarded opcodes return -1 without touching state.
int ApplyPacket(CommandPacket& pkt, AckEntry* ack);

} // namespace guild::sim
