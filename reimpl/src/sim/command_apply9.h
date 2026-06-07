#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/command_pending.h"
#include "sim/command_codec.h"   // DeltaWriter, QueueRequest17, QueueRequestState22

// command_apply9 — a further batch of genuinely-untranslated VIBE_Command_*
// leaves (namespace guild::sim). Every name/address below was checked, both by
// address (UPPERCASE + split forms) AND by bare name, against
// command_apply..command_apply8, command_codec, command_builders*,
// command_pending, command_inherit and command_receive; none is defined there.
//
// This file covers:
//   * the send-queue RESET pair (QueueReset / QueueResetAlt) — byte-exact
//     re-initialisation of the 0x2000-slot ring free-list + the 32768-entry
//     ACK table, modelled on one RingResetState instance (the originals write
//     a pile of file globals: byte_1078360 ring, dword_B5FB58 ACK table, …);
//   * WaitForPacketType — the blocking received-list scan with a game-tick
//     deadline (the lockstep "wait until packet of type X arrives" helper);
//   * EncodeFlagState — a pure bit-classification of a 4-byte flag word into a
//     single representative bitmask (the testable core of this file);
//   * three Check* predicates (CheckObjectFlagClear / CheckCanRunForOffice /
//     CheckOfficePrerequisites) — thin guards that read a person/office record;
//   * four EnqueuePacket builders (RequestBuildOp80 / RequestBuildOp81 /
//     RequestBuildOp85Unit / RequestChrMoveToUniverse), each staging a 153-byte
//     CommandPacket, writing the opcode at +0 and the payload from +0x10, then
//     calling CommandQueue::EnqueuePacket (reused via the existing class);
//   * two selection-driven command builders (QueueSetSelectedFlag /
//     QueueRevealAllPersons) whose console-token parse + emit path is faithful,
//     with their world/person reads routed through hooks.
//
// Cross-module leaves (person/office records, the selection arrays, the op-80/81
// snapshot blob, …) are routed through an installable hooks struct with inert
// defaults defined in this library .cpp (the CutsceneMiscHooks pattern). Tests
// install spies. Reused, already-reconstructed callees (CommandQueue::Enqueue-
// Packet, DeltaWriter::{BeginDeltaPacket,AppendRawField}, QueueRequest17,
// QueueRequestState22) are linked, never redefined.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Ring-reset state. The QueueReset originals re-initialise the global send ring
// (byte_1078360, 0x2000 slots of 153 bytes, intrusive prev@+0x91 / next@+0x95
// links) and the global ACK table (dword_B5FB58 entries, 10-byte stride, 32768
// entries: ring index @+2 = -1, status byte @+8 = 1). We model both on one
// instance so the reset is observable in isolation; one live instance per
// session reproduces the original single global state.
// ---------------------------------------------------------------------------
struct RingResetState {
    // Send ring: kSendRingSlots slots (32768) of kPacketStride (153) bytes.
    // The reset only touches slot byte 0 (cleared) and the two link fields.
    static constexpr u32 kSlots  = 0x2000;          // 8192 reset iterations
    static constexpr u32 kStride = kPacketStride;   // 153

    u8  ring[kSlots * kPacketStride];
    // ACK table: 32768 entries × 10 bytes (kAckTableBytes).
    u8  ack[kAckTableBytes];

    // Head/counter globals the reset writes:
    u32 head        = 0; // dword_11AA49C — &ring[0]
    u32 received     = 0; // dword_11AA498 — received-list head (0)
    u8  reasmFlag    = 0; // byte_11AA4A4 (0)
    u32 sendCount    = 0; // dword_11AA494 (0)
    u32 pendingHead  = 0; // dword_11AA46C (0)

    RingResetState() { Clear(); }
    void Clear() {
        for (u8& b : ring) b = 0;
        for (u8& b : ack) b = 0;
        head = received = reasmFlag = sendCount = pendingHead = 0;
    }

    // Accessors for the two link fields of slot i (prev @+0x91 == kLPrev,
    // next @+0x95 == kLNext) and for an ACK entry (ring index @+2, status @+8).
    // Little-endian.
    u32 link_prev(u32 i) const { return rd32(ring + i * kStride + kLPrev); }
    u32 link_next(u32 i) const { return rd32(ring + i * kStride + kLNext); }
    i32 ack_ring(u32 i)  const { return static_cast<i32>(rd32(ack + i * 10 + 2)); }
    u8  ack_status(u32 i) const { return ack[i * 10 + 8]; }

    static u32 rd32(const u8* p) {
        return u32(p[0]) | (u32(p[1]) << 8) | (u32(p[2]) << 16) | (u32(p[3]) << 24);
    }
};

// gilde.exe 0x493308 — VIBE_Command_QueueReset.
// gilde.exe 0x4933c0 — VIBE_Command_QueueResetAlt (byte-identical twin).
// Walk all 0x2000 ring slots: clear byte 0; set the prev link (+0x91) to the
// PREVIOUS slot's base (0 for slot 0), and the next link (+0x95) to the NEXT
// slot's base (0 for the last slot, index 0x1FFF). Then seed the head/counter
// globals and walk the 32768 ACK entries setting ring(+2) = -1, status(+8) = 1.
// Returns 327680 (the original's `result`, the final ACK-table byte offset).
i32 QueueReset(RingResetState& s);
i32 QueueResetAlt(RingResetState& s);

// ---------------------------------------------------------------------------
// WaitForPacketType — blocking received-list scan with a tick deadline.
// ---------------------------------------------------------------------------
// The received list is an intrusive singly-linked list of CommandPacket nodes
// linked through +149 (kLNext). We model it as a host-supplied callback set:
// the original drains/receives on each spin (FlushSendQueue/ReceiveAndQueue),
// then scans `received` for the first node whose opcode == `wanted`. The
// game-tick clock (dword_62EB38) is the host's monotonic counter.
struct WaitHooks {
    // FlushSendQueue + ReceiveAndQueue spin (default: no-op).
    void (*pump)();
    // Current received-list head (default: nullptr). dword_11AA498.
    CommandPacket* (*receivedHead)();
    // Walk to the next received-list node. The binary reads *(node+149) directly;
    // on a 64-bit host the link cannot live in the packet's 32-bit field, so the
    // host supplies the equivalent of CommandQueue's link side-table. Return
    // nullptr at the end of the chain (default: end immediately).
    CommandPacket* (*nextNode)(CommandPacket* node);
    // Current game tick dword_62EB38 (default: 0).
    u32 (*gameTick)();
};
void SetWaitHooks(const WaitHooks* hooks);
const WaitHooks& GetWaitHooks();

// gilde.exe 0x493f34 — VIBE_Command_WaitForPacketType(wanted, timeoutTicks).
// deadline = gameTick() + timeoutTicks (unsigned). If it does not advance past
// the start tick (timeout 0 or wrap), returns nullptr immediately. Otherwise
// spins: pump(); if a received node exists, scan its +149 chain for the first
// node with opcode == wanted and return it; once gameTick() reaches the
// deadline, returns nullptr.
CommandPacket* WaitForPacketType(u8 wanted, u32 timeoutTicks);

// ---------------------------------------------------------------------------
// EncodeFlagState — pure bit classification (the file's golden-vector core).
// ---------------------------------------------------------------------------
// gilde.exe 0x5681cc — VIBE_Command_EncodeFlagState(flagBase). Reads the 4-byte
// flag word at flagBase+44 (and the byte at +45/+46/+47 it overlaps). If the
// dword is zero, nothing is emitted (returns 1). Otherwise it picks the FIRST
// matching nibble/bit group, top priority first, and emits a representative
// bitmask via QueueRequestArgs25(personId, 44, 0, 4, mask). The classification
// is exposed separately so it can be golden-tested without a queue.
//   mask 0 if none match. The priority ladder (from the binary):
//     (b44 & 0x0F)            -> 7
//     (b44 & 0xF0)            -> 48          (0x30)
//     (b45 & 0x0F)            -> 768         (0x300)
//     (b45 & 0x30)            -> 4096        (0x1000)
//     (dw44 & 0x1C000)        -> 49152       (0xC000)
//     (b46 & 0x0E)            -> 0x20000
//     (b46 & 0x70)            -> 0x100000
//     (w46 & 0x180)           -> 0x800000
//     (b47 & 0x1E)            -> 0x6000000   (100663296)
// `flag4` is the 4-byte little-endian word at +44 (bytes 44..47).
i32 EncodeFlagStateMask(u32 flag4);

// Full builder: read the 4-byte flag at base+44, classify, and (when nonzero)
// emit on `q`. `personId` is *(base+4). Returns 1 always (matching the binary).
i32 EncodeFlagState(CommandQueue& q, i32 personId, u32 flag4);

// ---------------------------------------------------------------------------
// Check* predicates + the cross-module record hooks they read.
// ---------------------------------------------------------------------------
struct CheckHooks {
    // VIBE_Person_QueryBegin(a2, 1, 1, key): resolve a person record base.
    // `outFlagByte90` receives the record's byte at +90 (CheckObjectFlagClear).
    // Returns nonzero if a record was found. Default: returns 0 (not found).
    int (*personQueryBeginFlag90)(int a2, int key, u8* outFlagByte90);
    // VIBE_Office_CanRunForOffice(officePtr, a2): nonzero if eligible.
    // Default: returns 0 (not eligible).
    int (*officeCanRunFor)(int officePtr, int a2);
    // VIBE_Office_CheckPrerequisitesMet(officePtr): nonzero if met.
    // Default: returns 0 (not met).
    int (*officePrereqMet)(int officePtr);
};
void SetCheckHooks(const CheckHooks* hooks);
const CheckHooks& GetCheckHooks();

// gilde.exe 0x496124 — VIBE_Command_CheckObjectFlagClear(cmd, a2). Resolves a
// person via QueryBegin(a2, key = *(cmd+16)); returns true (1) if no record OR
// (record byte[90] & 2) == 0. `cmd16` is *(cmd+16). Faithful BOOL result.
int CheckObjectFlagClear(int cmd16, int a2);

// gilde.exe 0x49614c — VIBE_Command_CheckCanRunForOffice(cmd, a2). Returns
// !CanRunForOffice(cmd+16, a2). `officePtr` is cmd+16.
int CheckCanRunForOffice(int officePtr, int a2);

// gilde.exe 0x496160 — VIBE_Command_CheckOfficePrerequisites(cmd). Returns
// CheckPrerequisitesMet(cmd+16) == 0. `officePtr` is cmd+16.
int CheckOfficePrerequisites(int officePtr);

// ---------------------------------------------------------------------------
// EnqueuePacket builders + their snapshot hook.
// ---------------------------------------------------------------------------
struct BuilderHooks {
    // *(dword*)dword_6315C0 — the op-80/81/85 snapshot dword. Returns the value,
    // or -1 if the pointer global is null. Default: returns -1 (null pointer).
    i32 (*op80SnapshotDword)();
    // VIBE_Combat_FindUnitById(id): returns *(unit+36) (the +9 dword), or a
    // sentinel when not found. `found` is set true/false. Default: not found.
    i32 (*combatUnitField9)(i32 id, bool* found);
    // VIBE_ErrorLog_ReportMessage("cm: RequestChrMoveTo… name too long").
    // Default: no-op.
    void (*errorLog)(const char* msg);
};
void SetBuilderHooks(const BuilderHooks* hooks);
const BuilderHooks& GetBuilderHooks();

// NOTE: VIBE_Command_RequestBuildOp80 @0x495874 is ALREADY reconstructed in
// combat_packets.cpp (guild::sim::RequestBuildOp80 over an OrderStage) — not
// duplicated here. Op81 is its byte-identical twin (only the opcode differs) and
// was NOT reconstructed, so we provide it.

// gilde.exe 0x4958d0 — VIBE_Command_RequestBuildOp81(a1, body44). opcode 81.
// payload: a1@+0x10 (v4), 44 bytes of `body44`@+0x14 (v5), then the snapshot
// dword@+0x40 (v6 = *dword_6315C0 or -1). Returns EnqueuePacket slot.
i32 RequestBuildOp81(CommandQueue& q, i32 a1, const void* body44);

// gilde.exe 0x4959a8 — VIBE_Command_RequestBuildOp85Unit(unit, mode, body44).
// opcode 85. payload: snapshot dword@+0x10 (v7=*dword_6315C0), mode byte@+0x14
// (v8), 44 bytes of `body44` @+0x15 (v16). When `unit` is non-null AND
// *(unit+388) is non-null, six transform dwords from *(*(unit+388)+52)+{76,80,
// 84,132,136,140} are written at +0x15.. and *(unit+36) at +0x31; and when
// mode==2 the carried-unit's field-9 (combatUnitField9) overwrites v16[10]
// (payload +0x3D). Cross-module struct walks are routed through `unitXform`.
// `unitXform` fills the 6 transform dwords + the +36 field; returns whether the
// unit had a valid +388 chain.
struct Op85UnitXform { i32 d[6]; i32 field36; };
struct Op85Hooks { int (*unitXform)(i32 unit, Op85UnitXform* out); };
void SetOp85Hooks(const Op85Hooks* hooks);
const Op85Hooks& GetOp85Hooks();
i32 RequestBuildOp85Unit(CommandQueue& q, i32 unit, i8 mode, const void* body44);

// gilde.exe 0x494dd8 — VIBE_Command_RequestChrMoveToUniverse(a1, a2, name, a4).
// opcode 48 (0x30). payload: a1@+0x10 (v10), a2@+0x14 (v11), a4@+0x18 (v12),
// then a HE_NULL-class 2-byte-stride copy of `name` at +0x1C (v13). If
// strlen(name) >= 32 the name is NOT copied and errorLog() fires. Returns the
// EnqueuePacket slot. The 2-byte-stride copy mirrors the original's wide walk:
// it copies bytes pairwise, stopping at the first NUL in either lane.
i32 RequestChrMoveToUniverse(CommandQueue& q, i32 a1, i32 a2, const char* name, i32 a4);

// ---------------------------------------------------------------------------
// Selection-driven command builders + their world/person hooks.
// ---------------------------------------------------------------------------
struct SelectionHooks {
    // VIBE_Util_ParseInt(str): the original's signed-int console parser.
    // Default: a faithful clone (sign, decimal digits, stops at first non-digit).
    i32 (*parseInt)(const char* s);
    // QueueSetSelectedFlag: resolve a selected person's record from the selection
    // table `sel` at slot `idx` (the original: FindRecordById(*(sel+8*idx+4))).
    // Returns the record base (opaque), or 0. `outEntityId` receives *(rec+4).
    int (*selectedPersonRecord)(int sel, int idx, i32* outEntityId);
    // QueueRevealAllPersons: number of active world objects (word). Default: 0.
    i32 (*worldActiveCount)();
    // QueueRevealAllPersons: for slot i (0..767), is it a revealable person?
    // (the binary tests word_12CE910/byte_12CE918/byte_12CE912/byte_12CEA76).
    // Returns nonzero if revealable; `outEntityId` receives dword_12CE914[i].
    int (*revealableSlot)(int i, i32* outEntityId);
    // byte_6477A1 — the reveal flag byte passed to QueueRequest17. Default: 0.
    u8 (*revealFlagByte)();
};
void SetSelectionHooks(const SelectionHooks* hooks);
const SelectionHooks& GetSelectionHooks();

// gilde.exe 0x4fbbcc — VIBE_Command_QueueSetSelectedFlag(sel, arg). Console form
// "-<int>": the leading byte of `arg` must be '-' (45); parses the int as the
// flag value, resolves the selected person at slot `idx` (from `selKey`/`idx`
// recovered from the register args), and if found emits a 1-byte raw delta at
// field offset 433 then QueueRequestState22. Returns 1 on emit, 0 on reject.
// The register-arg shuffle (a1@ecx is the selection key snapshot) is captured by
// passing the selection table `sel` + slot `idx` directly. `dw` is the per-call
// delta writer.
i32 QueueSetSelectedFlag(CommandQueue& q, DeltaWriter& dw, int sel, int idx,
                         const char* arg);

// gilde.exe 0x4fbc40 — VIBE_Command_QueueRevealAllPersons(arg). Console form
// "-XXXX…" (>= 5 chars after '-'): lowercases the tail, reads the active-object
// count, then for each of the 768 person slots that is revealable, emits a
// QueueRequest17(entityId, -1, 1, activeCount, revealFlagByte, 0). Returns 1 if
// the parse passed and at least the loop ran, 0 on a malformed token / no actives.
i32 QueueRevealAllPersons(CommandQueue& q, const char* arg);

} // namespace guild::sim
