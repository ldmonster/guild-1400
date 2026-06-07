#include "sim/command_inherit.h"
#include "sim/command_builders2.h"   // QueueRequestArgs26 (opcode 26)

#include <cstring>

namespace guild::sim {

namespace {
inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }

// Read a 4-byte float column out of the live record (unaligned, UB-free).
inline float rdF(const InheritPerson& p, u32 off) {
    float f; std::memcpy(&f, p.bytes + off, 4); return f;
}
// Reinterpret a float's bits as the i32 the binary feeds Args26 (SLODWORD(v46)).
inline i32 fbits(float f) { i32 v; std::memcpy(&v, &f, 4); return v; }
} // namespace

// gilde.exe 0x494604 — VIBE_Command_EnqueueCmd15 (opcode 15).
i32 EnqueueCmd15(CommandQueue& q, i32 a1, i32 a2, i32 a3, u8 a4) {
    CommandPacket p{};
    p.opcode() = 15;
    put32(p, 0x10, a1);          // v6 = a1
    put32(p, 0x14, a2);          // v7 = a2
    p.bytes[0x1C] = a4;          // v8 = a4 (byte)
    put32(p, 0x1D, a3);          // v9 = a3
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4948c8 — VIBE_Command_QueueRequestSlotReset28 (opcode 28).
// The packet itself carries only the opcode; the 248-byte body travels through
// the StagePendingBlock fragment side-channel (reassembled by the receiver).
i32 QueueRequestSlotReset28(CommandQueue& q, PendingState& pending,
                            SlotResetScratch& scratch, i32 a2Unused) {
    (void)a2Unused;                      // v6 // [ebp-4]: stored, never on the wire

    CommandPacket p{};
    p.opcode() = 28;                     // v5[0] = 28

    // for (p = a1+8; p != a1+16; ++p) if (!a1[14]) a1[14] = -1;
    // The loop body's test/store both target a1[14]; the iteration just runs the
    // do/while a1..a1+8 times. Net effect: a1[14] becomes -1 iff it was 0.
    for (u32* it = scratch.words + 8; it != scratch.words + 16; ++it) {
        if (scratch.words[14] == 0)
            scratch.words[14] = 0xFFFFFFFFu;
    }

    // StagePendingBlock(0xF8, a1): stage the 248-byte body for fragmentation.
    StagePendingBlock(pending, 0xF8, &scratch);

    return q.EnqueuePacket(p);
}

// gilde.exe 0x494750 — VIBE_Command_QueueRequestState22 (opcode 22), real variant.
//   v1[0] = 22;
//   qmemcpy(v2 /*+0x10*/, &dword_11AA3E0, 124);  // entity_id|field_count|payload
//   return EnqueuePacket(v1);
// The 124-byte global block is laid out:
//   +0 .. +3   dword_11AA3E0  entity id
//   +4         byte_11AA3E4   field count
//   +5 ..      unk_11AA3E5    delta payload (triplets), up to 119 bytes
i32 QueueRequestState22FromDelta(CommandQueue& q, const DeltaWriter& dw) {
    CommandPacket p{};
    p.opcode() = 22;

    // Rebuild the 124-byte global image exactly: id(4) | count(1) | payload(119).
    u8 block[124];
    std::memset(block, 0, sizeof(block));
    u32 id = dw.entity_id();
    block[0] = static_cast<u8>(id);
    block[1] = static_cast<u8>(id >> 8);
    block[2] = static_cast<u8>(id >> 16);
    block[3] = static_cast<u8>(id >> 24);
    block[4] = dw.field_count();
    u32 n = dw.cursor();
    if (n > 119) n = 119;
    std::memcpy(block + 5, dw.payload(), n);

    std::memcpy(p.bytes + 0x10, block, sizeof(block));
    return q.EnqueuePacket(p);
}

// gilde.exe 0x58d29c..0x58d6fa — DistributeInheritance packet-emission body.
void EmitInheritanceDelta(InheritEmitCtx& ctx, InheritPerson& person) {
    CommandQueue& q = *ctx.queue;
    DeltaWriter&  dw = *ctx.delta;
    const i32 personId = person.id();
    const u8* base = person.bytes;        // (_WORD)v66 base for the offset deltas

    // ---- Delta packet #1 (0x58d2ad .. 0x58d50e) -----------------------------
    // The "new" values are the local temps the binary seeds before the deltas:
    //   v80[0]=100 (byte)   v77=16 (word)
    //   v57=3 (dword @404)  v58..v62 = 0 (dwords @408..424)
    //   v78=0 (byte @432)   v79=0 (byte @433)
    u8  new_status = 100;                  // LOBYTE(v80[0]) = 100
    u16 new_flags  = 16;                   // v77 = 16
    i32 new404     = 3;                    // v57 = 3
    i32 new_zero   = 0;                    // v58..v62, v63, v64 = 0
    u8  new_byte0  = 0;                    // LOBYTE(v78) / LOBYTE(v79)

    dw.BeginDeltaPacket(base, static_cast<u32>(personId));  // (int)v66, v66[1]
    // The binary passes (value_ptr in ecx, offset in bx). offset == raw byte
    // offset because dword_11AA474 == v66 (BeginDeltaPacket stashed it).
    dw.AppendDeltaField(1, 1, 8,   &new_status); // f1: status byte @+8
    dw.AppendDeltaField(2, 1, 10,  &new_flags);  // f2: flags word @+10
    dw.AppendDeltaField(4, 1, 404, &new404);     // f3: dword @+404 = 3
    dw.AppendDeltaField(4, 1, 408, &new_zero);   // f4: dword @+408 = 0
    dw.AppendDeltaField(4, 1, 412, &new_zero);   // f5: @+412
    dw.AppendDeltaField(4, 1, 416, &new_zero);   // f6: @+416
    dw.AppendDeltaField(4, 1, 420, &new_zero);   // f7: @+420
    dw.AppendDeltaField(4, 1, 424, &new_zero);   // f8: @+424
    dw.AppendDeltaField(1, 1, 432, &new_byte0);  // f9: byte @+432 = 0
    dw.AppendDeltaField(1, 1, 433, &new_byte0);  // f10: byte @+433 = 0
    QueueRequestState22FromDelta(q, dw);

    // ---- Delta packet #2 (0x58d51f .. 0x58d5ff) -----------------------------
    //   v24 = v66 + 46 (words) => byte offset 92; loop 8 times, +4 each => the
    //         8-dword asset array @+92..+120 cleared to 0 (v63 currently 0... no,
    //         v63 == v62 (-1) here; the binary clears with the *current* v63 value
    //         which was set to -1 in pkt1's locals (v63=-1 @0x58d311)).
    //   then v64 @+36, v63:=0 @+44, v76 (word) @+40.
    i32 v63 = -1;                          // v63 = -1 (set at 0x58d311)
    i32 v64 = 0;                           // v64 = 0  (set at 0x58d2e5)
    u16 v76 = 0;                           // v76 = 0  (set at 0x58d318)

    dw.BeginDeltaPacket(base, static_cast<u32>(personId)); // *(v23+4) == person id
    u32 off = 92;                          // v24 = v66 + 46 words == byte 92
    for (int i = 0; i < 8; ++i) {          // do { ... } while (v22 < 8)
        dw.AppendDeltaField(4, 1, static_cast<u16>(off), &v63); // asset dword
        off += 4;                          // v24 += 2 words
    }
    dw.AppendDeltaField(4, 1, 36, &v64);   // dword @+36
    v63 = 0;                               // v63 = 0 (0x58d5c3)
    dw.AppendDeltaField(4, 1, 44, &v63);   // dword @+44 = 0
    dw.AppendDeltaField(2, 1, 40, &v76);   // word  @+40
    QueueRequestState22FromDelta(q, dw);

    // ---- Args26 scalar commands (0x58d616 .. 0x58d6fa) ----------------------
    // field 124 = -(*(float*)(v66+92))  (wealth column negated), bits as i32.
    {
        float neg = -rdF(person, 92);      // *(float*)v66 = -*((float*)v66 + 31)
        QueueRequestArgs26(q, personId, 124, fbits(neg));
    }

    // field 20 = skill reroll delta. draw = randNext(); curve as recovered:
    //   v73 = (double)(int)draw * 62681C * 626820 + 626824;
    //   sign = v73 >= 0 ? +1 : -1;
    //   out  = v73 * (sign*v73) * 626828 + 62682C - *((float*)v66 + 5);  // +20
    {
        i32 draw = static_cast<i32>(ctx.randNext());
        double v73 = static_cast<double>(draw) * ctx.c62681C * ctx.c626820 + ctx.c626824;
        float sign = (v73 >= 0.0) ? 1.0f : -1.0f;
        float old20 = rdF(person, 20);     // *((float*)v66 + 5)
        float out = static_cast<float>(v73) * (sign * static_cast<float>(v73)) * ctx.c626828
                  + ctx.c62682C - old20;
        QueueRequestArgs26(q, personId, 20, fbits(out));
    }

    // field 28 = 626830 - *(float*)(v66+28);  field 24 = 626830 - *(float*)(v66+24);
    // field 32 = 626834 - *(float*)(v66+32).  Each emitted as the person id arg.
    {
        float out28 = ctx.c626830 - rdF(person, 28);
        QueueRequestArgs26(q, personId, 28, fbits(out28));
        float out24 = ctx.c626830 - rdF(person, 24);
        QueueRequestArgs26(q, personId, 24, fbits(out24));
        float out32 = ctx.c626834 - rdF(person, 32);
        QueueRequestArgs26(q, personId, 32, fbits(out32));
    }
}

}  // namespace guild::sim
