#include "sim/command_apply8.h"

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Cross-module leaf hooks (inert defaults).
// ===========================================================================
namespace {

i32  DefaultLocalPlayerMoney()        { return 0; }
void DefaultCopyState23Blob(void* d)  { std::memset(d, 0, 124); }
void DefaultCopyState24Blob(void* d)  { std::memset(d, 0, 124); }
i32  DefaultObjectTemplate34(void* d) { std::memset(d, 0, 0x2D); return 0; }
u32  DefaultRandNext()                { return 0; }
int  DefaultGesetzGetRecord(int, CommandApply8Hooks::LawRecord*) { return 0; }
void DefaultGesetzRequestApply(int, int, int) {}

CommandApply8Hooks g_hooks = {
    &DefaultLocalPlayerMoney,
    &DefaultCopyState23Blob,
    &DefaultCopyState24Blob,
    &DefaultObjectTemplate34,
    &DefaultRandNext,
    &DefaultGesetzGetRecord,
    &DefaultGesetzRequestApply,
};

inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }

} // namespace

void SetCommandApply8Hooks(const CommandApply8Hooks* hooks) {
    if (hooks) {
        g_hooks = *hooks;
        if (!g_hooks.localPlayerMoney)   g_hooks.localPlayerMoney   = &DefaultLocalPlayerMoney;
        if (!g_hooks.copyState23Blob)    g_hooks.copyState23Blob    = &DefaultCopyState23Blob;
        if (!g_hooks.copyState24Blob)    g_hooks.copyState24Blob    = &DefaultCopyState24Blob;
        if (!g_hooks.objectTemplate34)   g_hooks.objectTemplate34   = &DefaultObjectTemplate34;
        if (!g_hooks.randNext)           g_hooks.randNext           = &DefaultRandNext;
        if (!g_hooks.gesetzGetRecord)    g_hooks.gesetzGetRecord    = &DefaultGesetzGetRecord;
        if (!g_hooks.gesetzRequestApply) g_hooks.gesetzRequestApply = &DefaultGesetzRequestApply;
    } else {
        g_hooks = {
            &DefaultLocalPlayerMoney, &DefaultCopyState23Blob, &DefaultCopyState24Blob,
            &DefaultObjectTemplate34, &DefaultRandNext, &DefaultGesetzGetRecord,
            &DefaultGesetzRequestApply,
        };
    }
}
const CommandApply8Hooks& GetCommandApply8Hooks() { return g_hooks; }

// ===========================================================================
// Simple opcode request builders.
// ===========================================================================

// gilde.exe 0x4946f4 — VIBE_Command_QueueRequest20 (opcode 20).
i32 QueueRequest20(CommandQueue& q, i32 a1, i16 a2) {
    CommandPacket p{};
    p.opcode() = 20;
    put32(p, 0x10, a1);                          // v4 = a1
    p.put16(0x14, static_cast<u16>(a2));         // v5 = a2
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494b98 — VIBE_Command_QueueRequestPair36 (opcode 36).
i32 QueueRequestPair36(CommandQueue& q, i32 a1, i32 a2) {
    CommandPacket p{};
    p.opcode() = 36;
    put32(p, 0x10, a1);                          // v4 = a1
    put32(p, 0x14, a2);                          // v5 = a2
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494790 — VIBE_Command_QueueRequestState23 (opcode 23).
i32 QueueRequestState23(CommandQueue& q) {
    CommandPacket p{};
    p.opcode() = 23;
    g_hooks.copyState23Blob(p.bytes + 0x10);     // qmemcpy(v2, &dword_11AA3E0, 124)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4947d0 — VIBE_Command_QueueRequestState24 (opcode 24).
i32 QueueRequestState24(CommandQueue& q) {
    CommandPacket p{};
    p.opcode() = 24;
    g_hooks.copyState24Blob(p.bytes + 0x10);     // qmemcpy(v2, &dword_11AA360, 124)
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494c58 — VIBE_Command_QueueRequest40 (opcode 40).
i32 QueueRequest40(CommandQueue& q, PendingState& pending, const void* src) {
    StagePendingBlock(pending, 0x114, src);      // VIBE_Command_StagePendingBlock(0x114, a1)
    CommandPacket p{};
    p.opcode() = 40;
    return EmitWithPendingBlock(q, pending, p);  // EnqueuePacket links the staged block
}

// gilde.exe 0x494b28 — VIBE_Command_QueueRequestObject34 (opcode 34).
i32 QueueRequestObject34(CommandQueue& q, const void* templateSrc, i32 tail) {
    (void)tail; // v4[38] lands at packet +0xA8 (outside the 153-byte record) — dropped.
    CommandPacket p{};
    p.opcode() = 34;                              // LOBYTE(v3[0]) = 34
    // qmemcpy(v4, a1, 0x2D) into payload, then overwrite v4[0] with the scene id.
    u8 tmp[0x2D];
    std::memcpy(tmp, templateSrc, 0x2D);
    i32 sceneId = g_hooks.objectTemplate34(tmp); // fills/overrides as the global would
    std::memcpy(p.bytes + 0x10, tmp, 0x2D);
    put32(p, 0x10, sceneId);                      // v4[0] = dword_632240
    // *(dword*)((char*)&v4[9] + 1) = 1  ==  payload byte 0x25.
    put32(p, 0x10 + 0x25, 1);
    // *(dword*)((char*)&v4[10] + 1) = RandNext()  ==  payload byte 0x29.
    p.put32(0x10 + 0x29, g_hooks.randNext());
    return q.EnqueuePacket(p);
}

// gilde.exe 0x49441c — VIBE_Command_EnqueueBuildingActionStart (opcode 5).
i32 EnqueueBuildingActionStart(CommandQueue& q, const char* name) {
    CommandPacket p{};
    p.opcode() = 5;                               // v3[0] = 5
    // VIBE_Util_StrNCopyPad(v4, a1, 128): copy up to 128 chars, NUL-pad to 128.
    std::memset(p.bytes + 0x10, 0, 129);
    if (name)
        for (u32 i = 0; i < 128 && name[i]; ++i)
            p.bytes[0x10 + i] = static_cast<u8>(name[i]);
    return q.EnqueuePacket(p);
}

// gilde.exe 0x493e88 — VIBE_Command_SendPlayerMoneyState.
i32 SendPlayerMoneyState(CommandQueue& q) {
    // v2[0] = dword_12CE914[134 * word_63CC5C]; return QueueRequestFlagBlob32(19, v2).
    i32 blob[31];
    std::memset(blob, 0, sizeof blob);
    blob[0] = g_hooks.localPlayerMoney();
    return QueueRequestFlagBlob32(q, 19, blob);
}

// gilde.exe 0x594c8c — VIBE_Command_RequestBuildOp90_Thunk.
i32 RequestBuildOp90_Thunk(CommandQueue& q, i32 a, i32 b) {
    // return VIBE_Command_RequestBuildOp90(a2, a1)  ->  (b, a) in the original's
    // register mapping (a@eax becomes RequestBuildOp90's a1, swapped with a2@edx).
    return RequestBuildOp90(q, b, a);
}

// ===========================================================================
// Justice-severity clamp cores + builders.
// ===========================================================================

i32 JusticeClampAbsolute(i32 value, i32 min, i32 max) {
    i32 v10 = (value <= max) ? value : max;       // v10 = min(value, max)
    i32 v11;
    if (v10 <= min) {
        v11 = min;                                 // clamp up to min
    } else {
        v11 = max;
        if (value <= max)
            v11 = value;                           // in-band: keep value
    }
    return v11;
}

i32 JusticeClampDelta(i32 current, int sign, i32 delta, i32 min, i32 max) {
    i32 v16 = current + sign * delta;              // target
    i32 v17 = (v16 <= max) ? v16 : max;            // min(target, max)
    i32 v18;
    if (v17 <= min) {
        v18 = min;                                 // clamp up to min
    } else {
        if (v16 > max)
            v16 = max;
        v18 = v16;                                 // in-band target
    }
    return v18;
}

// gilde.exe 0x4fbd00 — VIBE_Command_QueueSetJusticeSeverity (clamp + emit half).
i32 QueueSetJusticeSeverity(int lawType, i32 requestedValue) {
    CommandApply8Hooks::LawRecord rec{};
    if (!g_hooks.gesetzGetRecord(lawType, &rec))
        return 0;                                  // no record (orig returns result==0)
    // Reject out-of-band only when the law's adjustable flag (v13[1]) is set.
    if ((requestedValue < rec.min || requestedValue > rec.max) && rec.adjustable)
        return 0;
    i32 applied = JusticeClampAbsolute(requestedValue, rec.min, rec.max);
    g_hooks.gesetzRequestApply(0, lawType, applied);
    return 1;
}

// gilde.exe 0x4fbe04 — VIBE_Command_QueueAdjustJusticeSeverity (clamp + emit half).
i32 QueueAdjustJusticeSeverity(int lawType, int sign, i32 delta, i32 current) {
    CommandApply8Hooks::LawRecord rec{};
    if (!g_hooks.gesetzGetRecord(lawType, &rec))
        return 0;
    // target = current + sign*delta, clamped to the law's [min,max] band (v23 is
    // the current severity read from the record body in the original).
    i32 applied = JusticeClampDelta(current, sign, delta, rec.min, rec.max);
    g_hooks.gesetzRequestApply(0, lawType, applied);
    return 1;
}

} // namespace guild::sim
