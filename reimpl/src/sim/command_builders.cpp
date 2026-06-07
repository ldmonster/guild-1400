#include "sim/command_builders.h"

#include <cstring>

namespace guild::sim {

namespace {
inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
inline void put16(CommandPacket& p, u32 off, i16 v) { p.put16(off, static_cast<u16>(v)); }

// HE_NULL-class string copy (the original copies 2 bytes per iteration off the
// source, stopping at a NUL in either byte). For a normal C string this is just
// a byte copy including the terminator. Faithful to the unrolled 2-byte loop.
void HeStrCopy(u8* dst, const char* src) {
    if (!src) { dst[0] = 0; return; }
    const u8* s = reinterpret_cast<const u8*>(src);
    u8* d = dst;
    while (true) {
        u8 a = s[0];
        d[0] = a;
        if (!a) break;
        u8 b = s[1];
        d[1] = b;
        if (!b) break;
        s += 2; d += 2;
    }
}

// VIBE_Util_StrNCopyPad(dst, src, 31): copy up to 31 chars, NUL-pad the field.
void StrNCopyPad(u8* dst, const char* src, u32 n) {
    std::memset(dst, 0, n + 1);
    if (!src) return;
    u32 i = 0;
    for (; i < n && src[i]; ++i) dst[i] = static_cast<u8>(src[i]);
}
} // namespace

// gilde.exe 0x4949c4 — VIBE_Command_QueueRequestEntity29 (opcode 29).
// The original snapshots a HeRecord slice into the payload and also stages a
// 160-byte block (h+172) into the separate pending-fragment buffer via
// StagePendingBlock; that side-channel is the fragment-append mechanism owned by
// the pending-packet generator (command.cpp) and is NOT part of this packet's own
// payload, so it is omitted here (documented). The in-packet snapshot fields are
// translated faithfully.
i32 QueueRequestEntity29(CommandQueue& q, i8 a1, HeRecord* h) {
    CommandPacket p{};
    p.opcode() = 29;
    const u8* a2 = reinterpret_cast<const u8*>(h);
    put32(p, 0x10, *reinterpret_cast<const i32*>(a2 + 4));    // *((_DWORD*)a2+1)  id
    put32(p, 0x14, *reinterpret_cast<const i32*>(a2 + 68));   // *((_DWORD*)a2+17)
    put32(p, 0x18, *reinterpret_cast<const i32*>(a2 + 72));   // *((_DWORD*)a2+18)
    put32(p, 0x1C, *reinterpret_cast<const i32*>(a2 + 76));   // *((_DWORD*)a2+19)
    put16(p, 0x20, *reinterpret_cast<const i16*>(a2 + 80));   // *((_WORD*)a2+40)
    put32(p, 0x22, *reinterpret_cast<const i32*>(a2 + 82));   // *(a2+82)  appt day
    put32(p, 0x26, *reinterpret_cast<const i32*>(a2 + 86));   // *(a2+86)
    put32(p, 0x2A, *reinterpret_cast<const i32*>(a2 + 90));   // *(a2+90)
    put16(p, 0x2E, *reinterpret_cast<const i16*>(a2 + 94));   // *((_WORD*)a2+47)
    put32(p, 0x30, *reinterpret_cast<const i32*>(a2 + 96));   // *((_DWORD*)a2+24)
    put32(p, 0x34, *reinterpret_cast<const i32*>(a2 + 100));  // *((_DWORD*)a2+25)
    put32(p, 0x38, *reinterpret_cast<const i32*>(a2 + 104));  // *((_DWORD*)a2+26)
    put16(p, 0x3C, *reinterpret_cast<const i16*>(a2 + 108));  // *((_WORD*)a2+54)
    p.bytes[0x3E] = static_cast<u8>(a1);                      // v22 = a1
    // v3 = &aHeNull[32 * *a2]; the original copies a HE_NULL-class label string
    // selected by the record's kind byte (*a2). The label table is a GUI/text
    // resource; in isolation we emit the empty string (HE_NULL == ""), keeping the
    // terminator at +0x3F.
    p.bytes[0x3F] = 0;
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494b04 — VIBE_Command_QueueRequestPair33 (opcode 33).
i32 QueueRequestPair33(CommandQueue& q, i32 a1, i32 a2) {
    CommandPacket p{};
    p.opcode() = 33;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494e4c — VIBE_Command_QueueRequestSingle49 (opcode 49).
i32 QueueRequestSingle49(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 49;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495098 — VIBE_Command_QueueRequestQuad56 (opcode 56).
// a3 is held in a stack slot outside the packet and does not reach the wire.
i32 QueueRequestQuad56(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 56;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a4);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495100 — VIBE_Command_QueueRequestPair57 (opcode 57). a2@+0x10,
// a1@+0x14.
i32 QueueRequestPair57(CommandQueue& q, i32 a1, i32 a2) {
    CommandPacket p{};
    p.opcode() = 57;
    put32(p, 0x10, a2);   // v4 = a2
    put32(p, 0x14, a1);   // v5 = a1
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495ae0 — VIBE_Command_RequestBuildOp88 (opcode 88).
i32 RequestBuildOp88(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 88;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495b7c — VIBE_Command_RequestBuildOp91 (opcode 91). a3 not on wire.
i32 RequestBuildOp91(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 91;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a4);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495bd0 — VIBE_Command_RequestBuildOp93 (opcode 93). a3 not on wire.
i32 RequestBuildOp93(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 93;
    put32(p, 0x10, a1);
    put32(p, 0x14, a2);
    put32(p, 0x18, a4);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495554 — VIBE_Command_RequestBuildOp73Str (opcode 73).
//   v12[0]=73; v13(a2)@+0x17; v14(a4)@+0x1B; v15(a3)@+0x1F? no — v15@-85=+0x1F,
//   v16(a5)@-84=+0x20, v17(a6 word)@-83=+0x21, v12[20]=a1 byte@+0x14, name@+0x27.
i32 RequestBuildOp73Str(CommandQueue& q, i8 a1, i32 a2, i8 a3, i32 a4,
                        i8 a5, i16 a6, const char* name) {
    CommandPacket p{};
    p.opcode() = 73;
    p.bytes[0x14] = static_cast<u8>(a1);   // v12[20] = a1
    put32(p, 0x17, a2);                    // v13 = a2
    put32(p, 0x1B, a4);                    // v14 = a4
    p.bytes[0x1F] = static_cast<u8>(a3);   // v15 = a3
    p.bytes[0x20] = static_cast<u8>(a5);   // v16 = a5
    put16(p, 0x21, a6);                    // v17 = a6
    HeStrCopy(p.bytes + 0x27, name);       // name (HE_NULL-class copy)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494f0c — VIBE_Command_QueueRequestNamedObject53 (opcode 53).
i32 QueueRequestNamedObject53(CommandQueue& q, i32 a1, i32 a2, const char* obj,
                              i32 a4, i8 a5, const char* name, bool personStamped) {
    // Early-out: FindRecordById(a1) && !record->+8  =>  return -1.
    if (!personStamped)
        return -1;
    CommandPacket p{};
    p.opcode() = 53;
    put32(p, 0x10, a1);                    // v19 = a1
    put32(p, 0x14, a2);                    // v20 = a2
    put32(p, 0x18, a4);                    // v21 = a4
    p.bytes[0x1C] = static_cast<u8>(a5);   // v22 = a5
    StrNCopyPad(p.bytes + 0x3D, name, 31); // v24 = name (StrNCopyPad,31)
    HeStrCopy(p.bytes + 0x1D, obj);        // v23 = obj  (HE_NULL-class copy)
    return q.EnqueuePacket(p);
}

} // namespace guild::sim
