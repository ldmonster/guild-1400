#include "sim/command_builders2.h"

#include <cstring>

namespace guild::sim {

namespace {
inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
inline void put16(CommandPacket& p, u32 off, i16 v) { p.put16(off, static_cast<u16>(v)); }

// VIBE_Util_StrNCopyPad(dst, src, n): copy up to n chars, NUL-pad to n+1.
void StrNCopyPad(u8* dst, const char* src, u32 n) {
    std::memset(dst, 0, n + 1);
    if (!src) return;
    for (u32 i = 0; i < n && src[i]; ++i)
        dst[i] = static_cast<u8>(src[i]);
}
} // namespace

// --- opcode 26 -------------------------------------------------------------
i32 QueueRequestArgs26(CommandQueue& q, i32 a1, i32 a2, i32 a3) {
    CommandPacket p{};
    p.opcode() = 26;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a3);
    return q.EnqueuePacket(p);
}

// --- opcodes 35/41/42/51: two-dword pair (a1@+0x10, a2@+0x14) ---------------
static i32 EmitPair(CommandQueue& q, u8 op, i32 a1, i32 a2) {
    CommandPacket p{};
    p.opcode() = op;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    return q.EnqueuePacket(p);
}
i32 QueueRequestPair35(CommandQueue& q, i32 a1, i32 a2) { return EmitPair(q, 35, a1, a2); }
i32 QueueRequestPair41(CommandQueue& q, i32 a1, i32 a2) { return EmitPair(q, 41, a1, a2); }
i32 QueueRequestPair42(CommandQueue& q, i32 a1, i32 a2) { return EmitPair(q, 42, a1, a2); }
i32 QueueRequestPair51(CommandQueue& q, i32 a1, i32 a2) { return EmitPair(q, 51, a1, a2); }

// --- opcodes 37/43/52/54: a1@+0x10, a2@+0x14, a4@+0x18 (a3 not on wire) -----
static i32 EmitQuad3(CommandQueue& q, u8 op, i32 a1, i32 a2, i32 a4) {
    CommandPacket p{};
    p.opcode() = op;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a4);
    return q.EnqueuePacket(p);
}
i32 QueueRequestQuad37(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) { (void)a3; return EmitQuad3(q, 37, a1, a2, a4); }
i32 QueueRequestQuad43(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) { (void)a3; return EmitQuad3(q, 43, a1, a2, a4); }
i32 QueueRequestQuad52(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) { (void)a3; return EmitQuad3(q, 52, a1, a2, a4); }
i32 QueueRequestQuad54(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) { (void)a3; return EmitQuad3(q, 54, a1, a2, a4); }

// --- opcodes 46/60: a1@+0x10, a2@+0x14, a4@+0x18, a3@+0x1C (all on wire) ----
static i32 EmitQuad4(CommandQueue& q, u8 op, i32 a1, i32 a2, i32 a3, i32 a4) {
    CommandPacket p{};
    p.opcode() = op;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a4);
    put32(p, 0x1C, a3);
    return q.EnqueuePacket(p);
}
i32 QueueRequestQuad46(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) { return EmitQuad4(q, 46, a1, a2, a3, a4); }
i32 QueueRequestQuad60(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) { return EmitQuad4(q, 60, a1, a2, a3, a4); }

// --- opcodes 58/59: single dword (a1@+0x10) --------------------------------
i32 QueueRequestSingle58(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 58;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}
i32 QueueRequestSingle59(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 59;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// --- opcode 55 -------------------------------------------------------------
// v5(a1)@+0x10; v6[48](a2 byte)@+0x44; StrNCopyPad(v6,name,47) into the 48-byte
// field at +0x14 (so the flag byte sits one past the 47-char name + NUL).
i32 QueueRequestFlag55(CommandQueue& q, i32 a1, i8 a2, const char* name) {
    CommandPacket p{};
    p.opcode() = 55;
    put32(p, 0x10, a1);                 // v5 = a1
    StrNCopyPad(p.bytes + 0x14, name, 47); // VIBE_Util_StrNCopyPad(v6, a3, 47)
    p.bytes[0x44] = static_cast<u8>(a2);   // v6[48] = a2
    return q.EnqueuePacket(p);
}

// --- opcode 30 -------------------------------------------------------------
// Only emitted when the appointment GameTime is still in the future; the original
// calls VIBE_GameTime_Compare(appt, &qword_13CE852) and returns -1 if it is 0.
// Payload: appt[0]@+0x10, appt[1]@+0x14, appt[2]@+0x18, appt[3].lo word@+0x1C.
i32 QueueRequestPerm30(CommandQueue& q, i32 a1, i32 a2, i32 a3, i16 a4, bool apptValid) {
    if (!apptValid)
        return -1;                       // GameTime_Compare(...) == 0
    CommandPacket p{};
    p.opcode() = 30;
    put32(p, 0x10, a1);                  // v6 = *a1
    put32(p, 0x14, a2);                  // v7 = *(a1+1)
    put32(p, 0x18, a3);                  // v8 = *(a1+2)
    put16(p, 0x1C, a4);                  // v9 = *((WORD*)(a1+2)+2)
    return q.EnqueuePacket(p);
}

// --- opcode 28 (large pending-block speech buffer) -------------------------
// Allocates a 248+bodyLen scratch (header || body), stages it as a pending block
// (StagePendingBlock(bodyLen+248, scratch)), then enqueues an opcode-28 header
// whose +12 is linked to the generated fragment chain by EmitWithPendingBlock.
i32 QueueRequestBuffer28(CommandQueue& q, PendingState& pending,
                         const void* header, const void* body, u16 bodyLen,
                         bool personFound) {
    if (!personFound)                    // !VIBE_Person_FindRecordById(header->id)
        return -1;

    // Build the staged block exactly as the original scratch buffer: 248-byte
    // header struct followed by bodyLen body bytes. StagePendingBlock caps total
    // at 0x3FE, so guard the body length (the original masks (bodyLen+248)&0xFFFF).
    u32 totalLen = static_cast<u32>(bodyLen) + 0xF8; // 248
    u8 scratch[2 + kPendingMaxBlock];
    if (totalLen > kPendingMaxBlock)
        totalLen = kPendingMaxBlock;
    std::memset(scratch, 0, sizeof(scratch));
    std::memcpy(scratch, header, 0xF8);
    if (bodyLen)
        std::memcpy(scratch + 0xF8, body,
                    (totalLen > 0xF8) ? (totalLen - 0xF8) : 0);
    StagePendingBlock(pending, static_cast<u16>(totalLen), scratch);

    CommandPacket p{};
    p.opcode() = 28;
    return EmitWithPendingBlock(q, pending, p);
}

} // namespace guild::sim
