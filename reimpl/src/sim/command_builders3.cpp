#include "sim/command_builders3.h"

#include <cstdio>
#include <cstring>

namespace guild::sim {

namespace {
inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
inline void put16(CommandPacket& p, u32 off, i16 v) { p.put16(off, static_cast<u16>(v)); }

// Raw little-endian dword read from a byte buffer (the original derefs the @<eax>
// pointer at unaligned offsets, e.g. *(_DWORD *)(a1 + 1)).
inline i32 rd32(const void* base, u32 off) {
    i32 v;
    std::memcpy(&v, static_cast<const u8*>(base) + off, 4);
    return v;
}
} // namespace

// gilde.exe 0x4953d8 — VIBE_Command_RequestBuildOp65 (opcode 65).
i32 RequestBuildOp65Blob(CommandQueue& q, const void* src) {
    CommandPacket p{};
    p.opcode() = 65;
    std::memcpy(p.bytes + 0x10, src, 56);                       // v4 <- src[0..55]
    std::memcpy(p.bytes + 0x48, static_cast<const u8*>(src) + 56, 2); // v5 <- src[56..57]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495414 — VIBE_Command_RequestBuildOp66 (opcode 66).
i32 RequestBuildOp66(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 66;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495434 — VIBE_Command_RequestBuildOp67 (opcode 67).
i32 RequestBuildOp67(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 67;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495454 — VIBE_Command_RequestBuildOp68 (opcode 68).
i32 RequestBuildOp68Blob(CommandQueue& q, const void* src) {
    CommandPacket p{};
    p.opcode() = 68;
    std::memcpy(p.bytes + 0x10, src, 4);                        // v4 <- src[0..3]
    std::memcpy(p.bytes + 0x14, static_cast<const u8*>(src) + 4, 3); // v5 <- src[4..6]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495490 — VIBE_Command_RequestBuildOp69 (opcode 69).
i32 RequestBuildOp69Blob(CommandQueue& q, const void* src) {
    CommandPacket p{};
    p.opcode() = 69;
    std::memcpy(p.bytes + 0x10, src, 12);                        // v4 <- src[0..11]
    std::memcpy(p.bytes + 0x1C, static_cast<const u8*>(src) + 12, 3); // v5 <- src[12..14]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4954cc — VIBE_Command_RequestBuildOp70 (opcode 70).
i32 RequestBuildOp70Blob(CommandQueue& q, const void* src) {
    CommandPacket p{};
    p.opcode() = 70;
    std::memcpy(p.bytes + 0x10, src, 8);                         // v4 <- src[0..7]
    std::memcpy(p.bytes + 0x18, static_cast<const u8*>(src) + 8, 1); // v5 <- src[8] (1 byte)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495508 — VIBE_Command_RequestBuildOp71 (opcode 71). a3 not on wire.
i32 RequestBuildOp71(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 71;
    put32(p, 0x10, a1);   // v6 = a1
    put32(p, 0x14, a2);   // v7 = a2
    put32(p, 0x18, a4);   // v8 = a4
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495530 — VIBE_Command_RequestBuildOp72 (opcode 72).
i32 RequestBuildOp72(CommandQueue& q, i32 a1, i8 a2) {
    CommandPacket p{};
    p.opcode() = 72;
    put32(p, 0x10, a1);                    // v4 = a1
    p.bytes[0x14] = static_cast<u8>(a2);   // v5 = a2
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4955c0 — VIBE_Command_RequestBuildOp74 (opcode 74).
i32 RequestBuildOp74(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 74;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4955e0 — VIBE_Command_RequestBuildOp75 (opcode 75).
i32 RequestBuildOp75Blob(CommandQueue& q, const void* src) {
    CommandPacket p{};
    p.opcode() = 75;
    std::memcpy(p.bytes + 0x10, src, 128);  // v4 <- src[0..127]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495954 — VIBE_Command_RequestBuildOp83 (opcode 83).
i32 RequestBuildOp83(CommandQueue& q, const i32* src5) {
    CommandPacket p{};
    p.opcode() = 83;
    put32(p, 0x10, src5[0]);   // v3 = *a1
    put32(p, 0x14, src5[1]);   // v4 = a1[1]
    put32(p, 0x18, src5[2]);   // v5 = a1[2]
    put32(p, 0x1C, src5[3]);   // v6 = a1[3]
    put32(p, 0x20, src5[4]);   // v7 = a1[4]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495980 — VIBE_Command_RequestBuildOp84 (opcode 84).
i32 RequestBuildOp84(CommandQueue& q, const i32* src3) {
    CommandPacket p{};
    p.opcode() = 84;
    put32(p, 0x10, src3[0]);   // v3 = *a1
    put32(p, 0x14, src3[1]);   // v4 = a1[1]
    put32(p, 0x18, src3[2]);   // v5 = a1[2]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495a90 — VIBE_Command_RequestBuildOp86 (opcode 86).
i32 RequestBuildOp86Blob(CommandQueue& q, const void* src) {
    CommandPacket p{};
    p.opcode() = 86;
    std::memcpy(p.bytes + 0x10, src, 40);  // v4 <- src[0..39]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495ac0 — VIBE_Command_RequestBuildOp87 (opcode 87).
i32 RequestBuildOp87(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 87;
    put32(p, 0x10, a1);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495ba4 — VIBE_Command_RequestBuildOp92 (opcode 92).
i32 RequestBuildOp92(CommandQueue& q, const i32* src) {
    CommandPacket p{};
    p.opcode() = 92;
    put32(p, 0x10, src[0]);   // v3 = *a1
    put32(p, 0x14, src[1]);   // v4 = a1[1]
    // v5 = *((_WORD *)a1 + 4) — the u16 at byte offset 8 of the source.
    put16(p, 0x18, static_cast<i16>(rd32(src, 8) & 0xFFFF));
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495bf8 — VIBE_Command_RequestBuildOp94 (opcode 94).
i32 RequestBuildOp94(CommandQueue& q, const void* base, const void* blob28) {
    CommandPacket p{};
    p.opcode() = 94;
    put32(p, 0x14, rd32(base, 1));          // v4 = *(_DWORD *)(a1 + 1)
    std::memcpy(p.bytes + 0x18, blob28, 28); // v5 <- a2[0..27]
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495c3c — VIBE_Command_RequestBuildOp95 (opcode 95).
i32 RequestBuildOp95(CommandQueue& q, const i32* src, const void* rec) {
    CommandPacket p{};
    p.opcode() = 95;
    put32(p, 0x10, src[0]);          // v4 = *a1
    put32(p, 0x14, rd32(rec, 4));    // v5 = *(_DWORD *)(a2 + 4)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x49561c — VIBE_Command_RequestCreateGebaeude (opcode 76).
i32 RequestCreateGebaeude(CommandQueue& q, i32 a1, i8 a2,
                          const void* meta12, const float* pos) {
    CommandPacket p{};
    p.opcode() = 76;
    put32(p, 0x10, a1);                     // v7 = a1
    p.bytes[0x18] = static_cast<u8>(a2);    // v8 = a2
    std::memcpy(p.bytes + 0x19, pos, 16);   // v9 <- a4[0..15] (transform, 16 bytes)
    std::memcpy(p.bytes + 0x29, meta12, 12);// v10 <- a3[0..11]
    // The original sprintf's a debug line ("cm_RequestCreateGeb(): pos %f %f %f")
    // into an out-of-packet stack buffer (v5[256]); the formatted text never
    // reaches the wire. Reproduced into a discarded local for fidelity.
    char dbg[256];
    std::snprintf(dbg, sizeof(dbg), "cm_RequestCreateGeb(): pos %f   %f   %f",
                  static_cast<double>(pos[0]), static_cast<double>(pos[1]),
                  static_cast<double>(pos[2]));
    (void)dbg;
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494be4 — VIBE_Command_RequestSendCutInfo (opcode 38).
i32 RequestSendCutInfo(CommandQueue& q, i32 a1, i32 a2, i32 gameTick) {
    CommandPacket p{};
    p.opcode() = 38;
    put32(p, 0x10, a1);   // v5 = a1
    put32(p, 0x14, a2);   // v6 = a2
    // Discarded debug sprintf (out-of-packet buffer), reproduced for fidelity.
    char dbg[256];
    std::snprintf(dbg, sizeof(dbg), "cm_RequestSendCutInfo(): PlayerId:%i Time:%i",
                  a1, gameTick);
    (void)dbg;
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495b00 — VIBE_Command_RequestCutsceneReady (opcode 89).
i32 RequestCutsceneReady(CommandQueue& q, const void* src68) {
    CommandPacket p{};
    p.opcode() = 89;
    std::memcpy(p.bytes + 0x10, src68, 68);  // v4 <- a1[0..67]
    // Discarded debug sprintf (out-of-packet buffer), reproduced for fidelity.
    char dbg[140];
    std::snprintf(dbg, sizeof(dbg),
                  "cm_RequestCutsceneReady(): requesting for id %i", rd32(src68, 0));
    (void)dbg;
    return q.EnqueuePacket(p);
}

} // namespace guild::sim
