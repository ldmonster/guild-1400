#include "sim/command_apply7.h"

#include <cstring>

namespace guild::sim {

// dword_631284 — game speed level. This module owns it (the SetGameSpeed family
// is its only writer in the recon set).
i32 g_gameSpeed = 0;

namespace {
inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
inline void put16(CommandPacket& p, u32 off, i16 v) { p.put16(off, static_cast<u16>(v)); }

// HE_NULL-class string copy: the original copies 2 source bytes per iteration,
// stopping at a NUL in either byte. For a normal C string this is a byte copy
// including the terminator. Faithful to the unrolled 2-byte loop.
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

// VIBE_Util_StrNCopyPad(dst, src, n): copy up to n chars, NUL-pad to n bytes.
void StrNCopyPad(u8* dst, const char* src, u32 n) {
    std::memset(dst, 0, n);
    if (!src) return;
    for (u32 i = 0; i < n && src[i]; ++i) dst[i] = static_cast<u8>(src[i]);
}
} // namespace

// gilde.exe 0x494ab4 — VIBE_Command_QueueRequestFlagBlob32 (opcode 32).
i32 QueueRequestFlagBlob32(CommandQueue& q, i8 flag, const void* blob) {
    CommandPacket p{};
    p.opcode() = 32;
    p.bytes[0x10] = static_cast<u8>(flag);   // v3[16] = a1
    if (blob)
        std::memcpy(&p.bytes[0x11], blob, 124); // qmemcpy(v4, a2, 124)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x493478 — VIBE_Command_EnqueueKeepAlive (opcode 3).
i32 EnqueueKeepAlive(CommandQueue& q) {
    CommandPacket p{};
    p.opcode() = 3;                          // v1[0] = 3
    put32(p, 0x10, -1288263715);             // v2 = -1288263715 (0xB325939D)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x49444c — VIBE_Command_EnqueueBuildingActionEnd (opcode 6).
i32 EnqueueBuildingActionEnd(CommandQueue& q, const char* name) {
    CommandPacket p{};
    p.opcode() = 6;                          // v2[0] = 6
    StrNCopyPad(&p.bytes[0x10], name, 128);  // StrNCopyPad(v3, a1, 128)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x49447c — VIBE_Command_EnqueueTargetedAction (opcode 10).
i32 EnqueueTargetedAction(CommandQueue& q, u8 a1, i32 a2, i16 a3, const void* blob) {
    CommandPacket p{};
    p.opcode() = 10;                         // v5[0] = 10
    if (!a1)                                 // if (!a1) return -1;
        return -1;
    put16(p, 0x14, static_cast<i16>(a1));    // v6 = a1
    put32(p, 0x16, a2);                      // v7 = a2
    put16(p, 0x1A, a3);                      // v8 = a3
    if (blob) {
        std::memcpy(&p.bytes[0x20], blob, 48); // qmemcpy(v10, a4, 48)
        put32(p, 0x1C, 1);                     // v9 = 1
    } else {
        // VIBE_Light_SetGrayColorThunk(0, 48): clears the 48-byte v10 field.
        std::memset(&p.bytes[0x20], 0, 48);
        put32(p, 0x1C, 0);                     // v9 = 0
    }
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494548 — VIBE_Command_EnqueueTradeRequest (opcode 12).
i32 EnqueueTradeRequest(CommandQueue& q, i32 a1, i32 a2, i8 a3, i16 a4, i32 a5,
                        i8 a6, i8 a7, const char* name1, const char* name2) {
    CommandPacket p{};
    p.opcode() = 12;                         // v10[0] = 12
    put32(p, 0x14, a1);                      // v11 = a1
    put32(p, 0x18, a2);                      // v12 = a2
    put16(p, 0x1C, a4);                      // v13 = a4
    p.bytes[0x1E] = static_cast<u8>(a3);     // v14 = a3
    put32(p, 0x1F, a5);                      // v15 = a5
    p.bytes[0x23] = static_cast<u8>(a6);     // v16 = a6
    p.bytes[0x24] = static_cast<u8>(a7);     // v17 = a7
    StrNCopyPad(&p.bytes[0x25], name1, 16);  // StrNCopyPad(v18, a8, 16)
    StrNCopyPad(&p.bytes[0x35], name2, 16);  // StrNCopyPad(v19, a9, 16)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4945c0 — VIBE_Command_EnqueueCmd13 (opcode 13).
i32 EnqueueCmd13(CommandQueue& q, i32 a1, i32 a2) {
    CommandPacket p{};
    p.opcode() = 13;                         // v3[0] = 13
    put32(p, 0x10, a1);                      // v4 = a1
    put32(p, 0x14, a2);                      // v5 = a2
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4945e4 — VIBE_Command_EnqueueCmd14 (opcode 14).
i32 EnqueueCmd14(CommandQueue& q, i16 a1) {
    CommandPacket p{};
    p.opcode() = 14;                         // v2[0] = 14
    put16(p, 0x10, a1);                      // v3 = a1
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4946a4 — VIBE_Command_QueueRequest18 (opcode 18).
// a3 lands in the [ebp-4] slot (outside the packet) and is dropped.
i32 QueueRequest18(CommandQueue& q, i32 a1, i16 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 18;                         // v5[0] = 18
    put32(p, 0x10, a1);                      // v6 = a1
    put16(p, 0x14, a2);                      // v7 = a2
    put32(p, 0x16, a4);                      // v8 = a4
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4946cc — VIBE_Command_QueueRequest19 (opcode 19).
// a3 lands in the [ebp-4] slot (outside the packet) and is dropped.
i32 QueueRequest19(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 19;                         // v5[0] = 19
    put32(p, 0x10, a1);                      // v6 = a1
    put32(p, 0x14, a2);                      // v7 = a2
    put32(p, 0x18, a4);                      // v8 = a4
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494718 — VIBE_Command_QueueRequestBlob21 (opcode 21).
i32 QueueRequestBlob21(CommandQueue& q, i32 a1, i16 a2, const void* blob) {
    CommandPacket p{};
    p.opcode() = 21;                         // v4[0] = 21
    put32(p, 0x10, a1);                      // v5 = a1
    put16(p, 0x14, a2);                      // v6 = a2
    std::memcpy(&p.bytes[0x16], blob, 31);   // qmemcpy(v7, a3, 31)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494a9c — VIBE_Command_QueueRequest31 (opcode 31).
i32 QueueRequest31(CommandQueue& q) {
    CommandPacket p{};
    p.opcode() = 31;                         // v1[0] = 31
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494cf0 — VIBE_Command_QueueRequestMixed44 (opcode 44).
i32 QueueRequestMixed44(CommandQueue& q, i32 a1, i8 a2, i16 a3, i8 a4, i8 a5, i32 a6) {
    CommandPacket p{};
    p.opcode() = 44;                         // v7[0] = 44
    put32(p, 0x14, a1);                      // v8 = a1
    p.bytes[0x18] = static_cast<u8>(a2);     // v9 = a2
    p.bytes[0x19] = static_cast<u8>(a4);     // v10 = a4
    put16(p, 0x1A, a3);                      // v11 = a3
    p.bytes[0x1C] = static_cast<u8>(a5);     // v12 = a5
    put32(p, 0x1D, a6);                      // v13 = a6
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494d34 — VIBE_Command_QueueRequestMixed45 (opcode 45).
i32 QueueRequestMixed45(CommandQueue& q, i32 a1, i32 a2, i32 a3) {
    CommandPacket p{};
    p.opcode() = 45;                         // v4[0] = 45
    put32(p, 0x14, a1);                      // v5 = a1
    put32(p, 0x18, a2);                      // v6 = a2
    put16(p, 0x1C, static_cast<i16>(a3));    // v7 = a3 (low word)
    p.bytes[0x1E] = static_cast<u8>(static_cast<u32>(a3) >> 16); // v8 = BYTE2(a3)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494d90 — VIBE_Command_QueueRequestString47 (opcode 47).
// The name pointer is the original's `a3` (ecx).
i32 QueueRequestString47(CommandQueue& q, i32 a1, i32 a2, const char* name, i32 a4) {
    CommandPacket p{};
    p.opcode() = 47;                         // v9[0] = 47
    put32(p, 0x10, a1);                      // v10 = a1
    put32(p, 0x14, a2);                      // v11 = a2
    put32(p, 0x18, a4);                      // v12 = a4
    HeStrCopy(&p.bytes[0x1C], name);         // 2-byte-stride copy into &v13
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494e6c — VIBE_Command_QueueRequestVectors50 (opcode 50).
// a3 lands in the [ebp-4] slot (outside the packet) and is dropped.
i32 QueueRequestVectors50(CommandQueue& q, i32 a1, const i32* vecA, i32 a3, const i32* vecB) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 50;                         // v5[0] = 50
    put32(p, 0x10, a1);                      // v6 = a1
    if (vecA) {
        put32(p, 0x14, vecA[0]);             // v7 = *a2
        put32(p, 0x18, vecA[1]);             // v8 = a2[1]
        put32(p, 0x1C, vecA[2]);             // v9 = a2[2]
    }
    if (vecB) {
        put32(p, 0x24, vecB[0]);             // v10 = *a4
        put32(p, 0x28, vecB[1]);             // v11 = a4[1]
        put32(p, 0x2C, vecB[2]);             // v12 = a4[2]
    }
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495230 — VIBE_Command_QueueRequestString62 (opcode 62).
i32 QueueRequestString62(CommandQueue& q, i32 a1, const char* name) {
    CommandPacket p{};
    p.opcode() = 62;                         // v7[0] = 62
    put32(p, 0x10, a1);                      // v8 = a1
    HeStrCopy(&p.bytes[0x14], name);         // 2-byte-stride copy into &v9
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495274 — VIBE_Command_QueueRequestFlagBlob63 (opcode 63).
i32 QueueRequestFlagBlob63(CommandQueue& q, const void* blob, i8 flag) {
    CommandPacket p{};
    p.opcode() = 63;                         // v3[0] = 63
    p.bytes[0x10] = static_cast<u8>(flag);   // v3[16] = a2
    std::memcpy(&p.bytes[0x11], blob, 128);  // qmemcpy(v4, a1, 128)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4956c8 — VIBE_Command_RequestBuildOp77 (opcode 77).
i32 RequestBuildOp77(CommandQueue& q, i32 a1) {
    CommandPacket p{};
    p.opcode() = 77;                         // v2[0] = 77
    put32(p, 0x10, a1);                      // v3 = a1
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4956e8 — VIBE_Command_RequestBuildOp78DualStr (opcode 78).
i32 RequestBuildOp78DualStr(CommandQueue& q, const char* name1, i32 a2,
                            const char* name2, i32 a4) {
    CommandPacket p{};
    p.opcode() = 78;                         // v13[0] = 78
    HeStrCopy(&p.bytes[0x10], name1);        // 2-byte-stride copy into &v14
    put32(p, 0x28, a2);                      // v15 = a2
    put32(p, 0x2C, a4);                      // v16 = a4
    if (name2)
        HeStrCopy(&p.bytes[0x30], name2);    // 2-byte-stride copy into &v17
    else
        p.bytes[0x30] = 0;                   // v17 = 0
    return q.EnqueuePacket(p);
}

// gilde.exe 0x49592c — VIBE_Command_RequestBuildOp82 (opcode 82).
// a3 lands in the [ebp-4] slot (outside the packet) and is dropped.
i32 RequestBuildOp82(CommandQueue& q, i32 a1, i32 a2, i32 a3, i32 a4) {
    (void)a3;
    CommandPacket p{};
    p.opcode() = 82;                         // v5[0] = 82
    put32(p, 0x10, a1);                      // v6 = a1
    put32(p, 0x14, a2);                      // v7 = a2
    put32(p, 0x18, a4);                      // v8 = a4
    return q.EnqueuePacket(p);
}

// gilde.exe 0x495b58 — VIBE_Command_RequestBuildOp90 (opcode 90).
i32 RequestBuildOp90(CommandQueue& q, i32 a1, i32 a2) {
    CommandPacket p{};
    p.opcode() = 90;                         // v3[0] = 90
    put32(p, 0x10, a1);                      // v4 = a1
    put32(p, 0x14, a2);                      // v5 = a2
    return q.EnqueuePacket(p);
}

// gilde.exe 0x493dec — VIBE_Command_SetGameSpeed.
i32 SetGameSpeed(CommandQueue& q, u32 level) {
    if (level >= 4) {
        level = 4;
        if (g_gameSpeed == 4)
            return static_cast<i32>(level);
    } else if (static_cast<i32>(level) == g_gameSpeed) {
        return static_cast<i32>(level);
    }
    // HARDENING (wave-11): QueueRequestFlagBlob32 copies 124 bytes from the blob
    // (qmemcpy(v4, a2, 124)). The original passes a 4-byte stack local here, so it
    // copies 120 bytes of adjacent (indeterminate, unread) stack into the unused
    // payload tail. We pass a 124-byte buffer with the level in the first dword so
    // the observable packet (first dword @+0x11) is byte-identical and there is no
    // out-of-bounds read of the source.
    u8 blob[124] = {0};
    i32 lv = static_cast<i32>(level);        // v2[0] = result
    std::memcpy(blob, &lv, sizeof(lv));
    QueueRequestFlagBlob32(q, 18, blob);
    return static_cast<i32>(level);
}

// gilde.exe 0x493e2c — VIBE_Command_IncreaseGameSpeed.
void IncreaseGameSpeed(CommandQueue& q) {
    if (static_cast<u32>(g_gameSpeed) < 4) {
        // HARDENING (wave-11): 124-byte source (see SetGameSpeed) — level in dword 0.
        u8 blob[124] = {0};
        i32 lv = g_gameSpeed + 1;            // v2[0] = dword_631284 + 1
        std::memcpy(blob, &lv, sizeof(lv));
        QueueRequestFlagBlob32(q, 18, blob);
    }
}

// gilde.exe 0x493e58 — VIBE_Command_DecreaseGameSpeed.
void DecreaseGameSpeed(CommandQueue& q) {
    if (g_gameSpeed) {
        // HARDENING (wave-11): 124-byte source (see SetGameSpeed) — level in dword 0.
        u8 blob[124] = {0};
        i32 lv = g_gameSpeed - 1;            // v2[0] = dword_631284 - 1
        std::memcpy(blob, &lv, sizeof(lv));
        QueueRequestFlagBlob32(q, 18, blob);
    }
}

// gilde.exe 0x493e80 — VIBE_Command_GetGameSpeed.
i32 GetGameSpeed() {
    return g_gameSpeed;
}

} // namespace guild::sim
