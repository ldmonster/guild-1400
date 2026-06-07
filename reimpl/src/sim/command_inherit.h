#pragma once
#include "guild/common/types.h"
#include "sim/command.h"
#include "sim/command_codec.h"
#include "sim/command_pending.h"

// command_inherit — the inheritance/heir command-emission cluster (guild::sim).
//
// Two previously-deferred pieces of the Command codec live here:
//
//  1. The real low-level request builders that the inheritance path (and dozens
//     of other call sites) depend on but that were only referenced by comment in
//     the rest of src/ until now:
//       VIBE_Command_EnqueueCmd15            @0x494604  (opcode 15 bulk-cash xfer)
//       VIBE_Command_QueueRequestSlotReset28 @0x4948c8  (opcode 28 slot-reset +
//                                                        248-byte StagePendingBlock)
//       VIBE_Command_QueueRequestState22     @0x494750  (opcode 22, the *real*
//                                                        no-arg variant that copies
//                                                        the 124-byte delta-build
//                                                        global block to +0x10)
//
//  2. The packet-emission body of VIBE_Person_DistributeInheritance @0x58cfa4 —
//     specifically the `if (publish)` block at 0x58d29c..0x58d6fa that serializes
//     the dead person's record into the lockstep stream: two delta (State22)
//     packets built field-by-field with AppendDeltaField, then five Args26
//     (opcode-26, field id + i32 value) scalar commands for the float columns.
//
// The full DistributeInheritance also does heir selection (Person_QueryBegin /
// BuildingValue_ComputeRoomWorth), object reassignment (GameObject_QueryFind /
// QueueRequestQuad43 / EnqueueCmd15) and message-string assembly
// (Text_RenderFormattedMessage). Those touch ~17 cross-cluster leaves; the
// packet-emission body translated here is the determinism-critical core and is
// driven against a real CommandQueue + DeltaWriter so its wire bytes are exact.
//
// ODR: EnqueueCmd15 / QueueRequestSlotReset28 / the no-arg QueueRequestState22 /
// EmitInheritanceDelta were each confirmed absent from src/ (grep) before adding.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Real request builders (opcode 15 / 28 / 22).
// ---------------------------------------------------------------------------

// gilde.exe 0x494604 — VIBE_Command_EnqueueCmd15 (opcode 15).
//   v5[0]=15; v6=a1@+0x10; v7=a2@+0x14; v8=a4(byte)@+0x1C; v9=a3@+0x1D.
// Bulk cash transfer: a1 payer account, a2 recipient (-1 == "the world"/sink),
// a3 amount, a4 currency/city rate byte. Returns the assigned ring slot index.
i32 EnqueueCmd15(CommandQueue& q, i32 a1, i32 a2, i32 a3, u8 a4);

// 32-dword scratch the SlotReset28 builder seeds. In the binary this is the
// caller's stack struct (&v47 in DistributeInheritance): a header byte at +0,
// dwords thereafter, and dword[14] reset to -1 if it is currently 0. The whole
// 248-byte region is then handed to StagePendingBlock as the opcode-28 body.
struct SlotResetScratch {
    // 248-byte body: StagePendingBlock(0xF8, &scratch) copies the full 248 bytes
    // into the staging block, so the region MUST be at least 248 bytes (the
    // original is a >=248-byte stack frame). The inherit caller touches only
    // words[8..14]; the 0x4FB000 debug-command builders write fields out to
    // byte offset 0x9C (word index 39). 62 dwords == 248 bytes.
    u32 words[62];
};

// gilde.exe 0x4948c8 — VIBE_Command_QueueRequestSlotReset28 (opcode 28).
// The original:
//   v5[0]=28;                       // staged packet opcode
//   for (p=a1+8; p!=a1+16; ++p)     // a1[8]..a1[13] then a1[14] guard? — actually
//       if (!a1[14]) a1[14] = -1;   // a1[14] forced to -1 when zero
//   VIBE_Command_StagePendingBlock(0xF8, a1);   // 248-byte body side-channel
//   return VIBE_Command_EnqueuePacket(v5);
// `a2` (ecx) lands in an out-of-packet stack slot (v6 // [ebp-4]) and never
// reaches the wire; we keep it for the prototype only. `pending` carries the
// StagePendingBlock global state (one per session, see command_pending.h).
i32 QueueRequestSlotReset28(CommandQueue& q, PendingState& pending,
                            SlotResetScratch& scratch, i32 a2Unused);

// gilde.exe 0x494750 — VIBE_Command_QueueRequestState22 (opcode 22), the real
// no-arg builder. It qmemcpy's 124 bytes starting at dword_11AA3E0 (the delta
// build globals: entity_id[4] | field_count[1] | payload[119]) into the payload
// at +0x10, then enqueues. We read those 124 bytes out of the supplied
// DeltaWriter so the wire image is byte-identical to the original global block.
i32 QueueRequestState22FromDelta(CommandQueue& q, const DeltaWriter& dw);

// ---------------------------------------------------------------------------
// DistributeInheritance packet-emission body.
// ---------------------------------------------------------------------------

// The dead person's 536-byte record, as the emission body reads it. Only the
// columns the delta/Args26 packets touch are named; everything else is opaque
// bytes so the record round-trips through the delta codec. Offsets are the raw
// byte offsets from VIBE_Person_DistributeInheritance (v66 == person base):
//   +0x04 (+4)   dword person id           (header id for both delta packets)
//   +8           byte  status byte 100      (delta f1: width1 count1)
//   +10          word  flags 16             (delta f2: width2 count1)
//   +0x14 (+20)  float skill column         (Args26 field 20)
//   +0x18 (+24)  float mood column          (Args26 field 24, decay -> kDecayB)
//   +0x1C (+28)  float mood column          (Args26 field 28, decay -> kDecayB)
//   +0x20 (+32)  float mood column          (Args26 field 32, decay -> kDecayC)
//   +0x24 (+36)  dword zeroed (delta pkt2)  / +44 dword zeroed / +40 word zeroed
//   +0x28 (+40)  word  (delta pkt2 width2)
//   +0x2C (+44)  dword (delta pkt2; cleared to 0)
//   +0x5C (+92)  float "%G" wealth column   (Args26 field 124, negated)
//   +0x5C..0x6C? +92.. 8 dwords @ +92 (v66+46 words .. +46+16): the 8-slot
//                inventory/asset array cleared via delta pkt2 (4-byte, value 0)
//   +0x194 (+404..+428) seven dwords cleared (delta pkt1 f3..f8, value 0)
//   +0x1B0 (+432,+433) two bytes cleared (delta pkt1 f9,f10)
// Field WIDTHS/COUNTS/OFFSETS below are taken verbatim from the AppendDeltaField
// call sequence in the binary.
struct InheritPerson {
    u8 bytes[536];
    i32 id() const {
        i32 v; for (int i=0;i<4;++i) reinterpret_cast<u8*>(&v)[i]=bytes[4+i]; return v;
    }
};

// Injected leaves the emission body calls. Kept tiny so the body is testable in
// isolation; the shipping game installs the real bridge.
struct InheritEmitCtx {
    CommandQueue* queue   = nullptr;   // the lockstep queue
    DeltaWriter*  delta   = nullptr;   // the delta build globals (one per session)

    // VIBE_Util_RandNext @0x5cb8bc — the LCG draw. The binary returns the 15-bit
    // value (HIWORD(state)&0x7FFF) typed as a pointer, then casts it back to int;
    // here we expose that draw directly (range 0..0x7FFF).
    u32 (*randNext)() = nullptr;

    // The five recovered float constants from the mood/skill recompute. The body
    // builds new column values from these and the live record, then emits the
    // (new - old) delta as an Args26 absolute value (field id + i32 bits).
    //   flt_62681C, flt_626820, flt_626824, flt_626828, flt_62682C: the skill
    //     reroll curve  v = draw*A*B + C ;  out = v*(sign(v)*v)*D + E - old.
    //   flt_626830 / flt_626834: the two mood reset targets (24/28 -> 830, 32 -> 834).
    float c62681C = 0, c626820 = 0, c626824 = 0, c626828 = 0, c62682C = 0;
    float c626830 = 0, c626834 = 0;
};

// gilde.exe 0x58d29c..0x58d6fa — the `if (publish)` packet-emission block of
// VIBE_Person_DistributeInheritance. Serializes the dead person's record into
// the lockstep stream:
//   * Delta packet #1: 10 fields (status byte, flags word, 7 cleared asset
//     dwords @+404.., two cleared bytes @+432/+433) -> QueueRequestState22.
//   * Delta packet #2: the 8-dword asset array @+92.. cleared, dword @+36 set,
//     dword @+44 cleared, word @+40 set -> QueueRequestState22.
//   * Args26 scalars: field 124 = -wealth(+92 float); field 20 = skill reroll
//     delta; fields 28/24 = mood-target-830 delta; field 32 = mood-target-834
//     delta.
// `person` is the live dead-person record (delta "old" source); the body writes
// (new - old) deltas relative to it. Returns nothing (the original's `result`
// is the message-string tail, emitted elsewhere).
void EmitInheritanceDelta(InheritEmitCtx& ctx, InheritPerson& person);

}  // namespace guild::sim
