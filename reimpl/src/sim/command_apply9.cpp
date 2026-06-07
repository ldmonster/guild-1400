#include "sim/command_apply9.h"

#include <cctype>
#include <cstring>

namespace guild::sim {

// ===========================================================================
// Cross-module leaf hooks (inert defaults).
// ===========================================================================
namespace {

// --- WaitHooks -------------------------------------------------------------
void           DefaultWaitPump() {}
CommandPacket* DefaultReceivedHead() { return nullptr; }
CommandPacket* DefaultNextNode(CommandPacket*) { return nullptr; }
u32            DefaultGameTick() { return 0; }

WaitHooks g_wait = { &DefaultWaitPump, &DefaultReceivedHead, &DefaultNextNode, &DefaultGameTick };

// --- CheckHooks ------------------------------------------------------------
int DefaultPersonQueryBeginFlag90(int, int, u8*) { return 0; }
int DefaultOfficeCanRunFor(int, int) { return 0; }
int DefaultOfficePrereqMet(int) { return 0; }

CheckHooks g_check = {
    &DefaultPersonQueryBeginFlag90, &DefaultOfficeCanRunFor, &DefaultOfficePrereqMet,
};

// --- BuilderHooks ----------------------------------------------------------
i32  DefaultOp80SnapshotDword() { return -1; }
i32  DefaultCombatUnitField9(i32, bool* found) { if (found) *found = false; return 0; }
void DefaultErrorLog(const char*) {}

BuilderHooks g_build = {
    &DefaultOp80SnapshotDword, &DefaultCombatUnitField9, &DefaultErrorLog,
};

// --- Op85Hooks -------------------------------------------------------------
int DefaultOp85UnitXform(i32, Op85UnitXform*) { return 0; }
Op85Hooks g_op85 = { &DefaultOp85UnitXform };

// --- SelectionHooks --------------------------------------------------------
// Faithful clone of VIBE_Util_ParseInt @0x5dc070: skip leading whitespace,
// optional +/-, accumulate decimal digits, negate on '-'. The original uses a
// ctype table (bit 2 = space, bit 0x20 = digit); std::isspace/std::isdigit are
// behavioural equivalents for the ASCII inputs this consumes.
i32 DefaultParseInt(const char* s) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(s);
    while (std::isspace(*p)) ++p;
    bool neg = false;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); ++p; }
    int v = 0;
    while (std::isdigit(*p)) { v = 10 * v + (*p - '0'); ++p; }
    return neg ? -v : v;
}
int DefaultSelectedPersonRecord(int, int, i32* outId) { if (outId) *outId = 0; return 0; }
i32 DefaultWorldActiveCount() { return 0; }
int DefaultRevealableSlot(int, i32* outId) { if (outId) *outId = 0; return 0; }
u8  DefaultRevealFlagByte() { return 0; }

SelectionHooks g_sel = {
    &DefaultParseInt, &DefaultSelectedPersonRecord, &DefaultWorldActiveCount,
    &DefaultRevealableSlot, &DefaultRevealFlagByte,
};

inline void put32(CommandPacket& p, u32 off, i32 v) { p.put32(off, static_cast<u32>(v)); }
inline void wr32(u8* p, u32 v) { p[0]=u8(v); p[1]=u8(v>>8); p[2]=u8(v>>16); p[3]=u8(v>>24); }

} // namespace

void SetWaitHooks(const WaitHooks* h) {
    if (h) {
        g_wait = *h;
        if (!g_wait.pump)         g_wait.pump = &DefaultWaitPump;
        if (!g_wait.receivedHead) g_wait.receivedHead = &DefaultReceivedHead;
        if (!g_wait.nextNode)     g_wait.nextNode = &DefaultNextNode;
        if (!g_wait.gameTick)     g_wait.gameTick = &DefaultGameTick;
    } else {
        g_wait = { &DefaultWaitPump, &DefaultReceivedHead, &DefaultNextNode, &DefaultGameTick };
    }
}
const WaitHooks& GetWaitHooks() { return g_wait; }

void SetCheckHooks(const CheckHooks* h) {
    if (h) {
        g_check = *h;
        if (!g_check.personQueryBeginFlag90) g_check.personQueryBeginFlag90 = &DefaultPersonQueryBeginFlag90;
        if (!g_check.officeCanRunFor)        g_check.officeCanRunFor = &DefaultOfficeCanRunFor;
        if (!g_check.officePrereqMet)        g_check.officePrereqMet = &DefaultOfficePrereqMet;
    } else {
        g_check = { &DefaultPersonQueryBeginFlag90, &DefaultOfficeCanRunFor, &DefaultOfficePrereqMet };
    }
}
const CheckHooks& GetCheckHooks() { return g_check; }

void SetBuilderHooks(const BuilderHooks* h) {
    if (h) {
        g_build = *h;
        if (!g_build.op80SnapshotDword) g_build.op80SnapshotDword = &DefaultOp80SnapshotDword;
        if (!g_build.combatUnitField9)  g_build.combatUnitField9 = &DefaultCombatUnitField9;
        if (!g_build.errorLog)          g_build.errorLog = &DefaultErrorLog;
    } else {
        g_build = { &DefaultOp80SnapshotDword, &DefaultCombatUnitField9, &DefaultErrorLog };
    }
}
const BuilderHooks& GetBuilderHooks() { return g_build; }

void SetOp85Hooks(const Op85Hooks* h) {
    if (h && h->unitXform) g_op85 = *h; else g_op85 = { &DefaultOp85UnitXform };
}
const Op85Hooks& GetOp85Hooks() { return g_op85; }

void SetSelectionHooks(const SelectionHooks* h) {
    if (h) {
        g_sel = *h;
        if (!g_sel.parseInt)             g_sel.parseInt = &DefaultParseInt;
        if (!g_sel.selectedPersonRecord) g_sel.selectedPersonRecord = &DefaultSelectedPersonRecord;
        if (!g_sel.worldActiveCount)     g_sel.worldActiveCount = &DefaultWorldActiveCount;
        if (!g_sel.revealableSlot)       g_sel.revealableSlot = &DefaultRevealableSlot;
        if (!g_sel.revealFlagByte)       g_sel.revealFlagByte = &DefaultRevealFlagByte;
    } else {
        g_sel = { &DefaultParseInt, &DefaultSelectedPersonRecord, &DefaultWorldActiveCount,
                  &DefaultRevealableSlot, &DefaultRevealFlagByte };
    }
}
const SelectionHooks& GetSelectionHooks() { return g_sel; }

// ===========================================================================
// Ring reset.
// ===========================================================================

// Shared body for the byte-identical twins QueueReset / QueueResetAlt.
static i32 RingResetBody(RingResetState& s) {
    s.Clear();
    constexpr u32 stride = RingResetState::kStride; // 153

    // Ring pass: 0x2000 slots. v0 = slot index, v1 = byte offset (= v0*153).
    // The link tables dword_10783F1 (+0x91) and dword_10783F5 (+0x95) are
    // indexed by the same byte offset v1 = v0*153; unk_10783F9 (the next-base)
    // sits at +0x99 == one full stride (153) past the slot base.
    //   prev link (+0x91): 0 for slot 0, else base of slot (v0-1) -> (v0-1)*153.
    //   next link (+0x95): 0 for slot >= 0x1FFF, else (0x99 + v0*153)
    //     i.e. &unk_10783F9 stepping by 153 — the next slot's base, faithful to
    //     the binary's pointer arithmetic.
    for (u32 v0 = 0; v0 < RingResetState::kSlots; ++v0) {
        u32 v1 = v0 * stride;
        s.ring[v1] = 0;                               // byte_1078360[v1] = 0
        u32 prev = (v0 != 0) ? (v0 - 1) * stride : 0; // *(&dword_10783F1 + v1)
        wr32(s.ring + v1 + kLPrev, prev);             // +0x91
        u32 next = (v0 >= 0x1FFF) ? 0u : (0x99 + v0 * stride); // *(&dword_10783F5 + v1)
        wr32(s.ring + v1 + kLNext, next);             // +0x95
    }

    s.head       = 0;          // dword_11AA49C = (int)byte_1078360 (offset 0 here)
    s.received   = 0;          // dword_11AA498 = 0
    s.reasmFlag  = 0;          // byte_11AA4A4 = 0
    s.sendCount  = 0;          // dword_11AA494 = 0
    s.pendingHead = 0;         // dword_11AA46C = 0

    // ACK pass: result steps by 10 from 10 to 327680 (32768 entries).
    // entry+2 (ring index) = -1; entry+8 (status byte) = 1.
    i32 result = 0;
    do {
        result += 10;
        // *(int*)(&dword_B5FB58 + result) = -1  ==  entry[(result/10)-1].ring (+2)
        wr32(s.ack + (result - 10) + 2, 0xFFFFFFFFu);
        // byte_B5FB56[result] = 1  ==  entry[(result/10)-1].status (+8)
        s.ack[(result - 10) + 8] = 1;
    } while (result != 327680);
    return result;
}

// gilde.exe 0x493308 — VIBE_Command_QueueReset.
i32 QueueReset(RingResetState& s) { return RingResetBody(s); }
// gilde.exe 0x4933c0 — VIBE_Command_QueueResetAlt (byte-identical twin).
i32 QueueResetAlt(RingResetState& s) { return RingResetBody(s); }

// ===========================================================================
// WaitForPacketType.
// ===========================================================================

// gilde.exe 0x493f34 — VIBE_Command_WaitForPacketType(wanted, timeoutTicks).
CommandPacket* WaitForPacketType(u8 wanted, u32 timeoutTicks) {
    u32 start = g_wait.gameTick();           // dword_62EB38
    u32 deadline = timeoutTicks + start;     // v3 = a2 + dword_62EB38
    if (deadline <= start)                   // a2 + tick <= tick  (timeout 0 / wrap)
        return nullptr;
    for (;;) {
        g_wait.pump();                       // FlushSendQueue(); ReceiveAndQueue();
        CommandPacket* node = g_wait.receivedHead(); // dword_11AA498
        if (node) {
            while (node->opcode() != wanted) {       // *result != a1
                node = g_wait.nextNode(node);        // result = *(char**)(result + 149)
                if (!node) break;
            }
            if (node) return node;
        }
        if (deadline <= g_wait.gameTick())   // v3 <= dword_62EB38
            return nullptr;
    }
}

// ===========================================================================
// EncodeFlagState.
// ===========================================================================

// gilde.exe 0x5681cc — the bit-classification ladder (pure).
i32 EncodeFlagStateMask(u32 flag4) {
    if (flag4 == 0) return 0;
    u8 b44 = static_cast<u8>(flag4 & 0xFF);
    u8 b45 = static_cast<u8>((flag4 >> 8) & 0xFF);
    u8 b46 = static_cast<u8>((flag4 >> 16) & 0xFF);
    u8 b47 = static_cast<u8>((flag4 >> 24) & 0xFF);
    u16 w46 = static_cast<u16>((flag4 >> 16) & 0xFFFF);

    if (b44 & 0x0F)           return 7;
    if (b44 & 0xF0)           return 48;
    if (b45 & 0x0F)           return 768;
    if (b45 & 0x30)           return 4096;
    if (flag4 & 0x1C000)      return 49152;
    if (b46 & 0x0E)           return 0x20000;
    if (b46 & 0x70)           return 0x100000;
    if (w46 & 0x180)          return 0x800000;     // (int)&unk_800000
    if (b47 & 0x1E)           return 100663296;    // 0x6000000
    return 0;                                      // v2 stays 0
}

i32 EncodeFlagState(CommandQueue& q, i32 personId, u32 flag4) {
    if (flag4) {
        i32 mask = EncodeFlagStateMask(flag4);
        // VIBE_Command_QueueRequestArgs25(*(a1+4), 44, 0, 4, v2)
        // codec signature: (q, a1, a2, a3, a4, a5) -> (personId, 44, 0, 4, mask)
        QueueRequestArgs25(q, personId, 44, 0, 4, mask);
    }
    return 1;
}

// ===========================================================================
// Check* predicates.
// ===========================================================================

// gilde.exe 0x496124 — VIBE_Command_CheckObjectFlagClear.
int CheckObjectFlagClear(int cmd16, int a2) {
    u8 flag90 = 0;
    int found = g_check.personQueryBeginFlag90(a2, cmd16, &flag90);
    // return !Begin || (Begin[90] & 2) == 0;
    return (!found || (flag90 & 2) == 0) ? 1 : 0;
}

// gilde.exe 0x49614c — VIBE_Command_CheckCanRunForOffice.
int CheckCanRunForOffice(int officePtr, int a2) {
    return g_check.officeCanRunFor(officePtr, a2) == 0 ? 1 : 0; // !CanRunForOffice(...)
}

// gilde.exe 0x496160 — VIBE_Command_CheckOfficePrerequisites.
int CheckOfficePrerequisites(int officePtr) {
    return g_check.officePrereqMet(officePtr) == 0 ? 1 : 0; // CheckPrerequisitesMet(...) == 0
}

// ===========================================================================
// EnqueuePacket builders.
// ===========================================================================

// gilde.exe 0x4958d0 — VIBE_Command_RequestBuildOp81 (opcode 81). The
// byte-identical twin of RequestBuildOp80 @0x495874 (reconstructed in
// combat_packets.cpp); only the opcode differs.
//   v3[0] = 81; v4 = a1; qmemcpy(v5, a2, 44);
//   v6 = dword_6315C0 ? *(dword*)dword_6315C0 : -1;  // payload +0x40
//   return EnqueuePacket(v3);
i32 RequestBuildOp81(CommandQueue& q, i32 a1, const void* body44) {
    CommandPacket p{};
    p.opcode() = 81;
    put32(p, 0x10, a1);                          // v4 = a1
    std::memcpy(p.bytes + 0x14, body44, 44);     // qmemcpy(v5, a2, 44)
    put32(p, 0x40, g_build.op80SnapshotDword()); // v6
    return q.EnqueuePacket(p);
}

// gilde.exe 0x4959a8 — VIBE_Command_RequestBuildOp85Unit (opcode 85).
i32 RequestBuildOp85Unit(CommandQueue& q, i32 unit, i8 mode, const void* body44) {
    CommandPacket p{};
    p.opcode() = 85;                             // v6[0] = 85
    // v7 = *(_DWORD*)dword_6315C0  (payload +0x10). The snapshot pointer is
    // never null in the unit path, but we route through the same hook.
    put32(p, 0x10, g_build.op80SnapshotDword());
    p.bytes[0x14] = static_cast<u8>(mode);       // v8 = a2
    // qmemcpy(v16, a3, 44)  — v16 begins at payload +0x15.
    std::memcpy(p.bytes + 0x15, body44, 44);

    if (unit) {
        Op85UnitXform xf{};
        if (g_op85.unitXform(unit, &xf)) {       // requires *(unit+388) chain valid
            // v9..v14 -> payload +0x15..+0x2C (six dwords); v15 -> *(a1+36) @ +0x31.
            for (int i = 0; i < 6; ++i) wr32(p.bytes + 0x15 + i * 4, u32(xf.d[i]));
            put32(p, 0x31, xf.field36);
            if (mode == 2) {                     // carried-unit field-9 overwrite
                bool found = false;
                // original reads v16[4] (payload +0x25) as the unit id arg.
                i32 unitId = static_cast<i32>(p.get32(0x15 + 4 * 4)); // v16[4] @ +0x25
                i32 f9 = g_build.combatUnitField9(unitId, &found);
                if (found) wr32(p.bytes + 0x15 + 10 * 4, u32(f9)); // v16[10] @ +0x3D
            }
        }
    }
    return q.EnqueuePacket(p);
}

// gilde.exe 0x494dd8 — VIBE_Command_RequestChrMoveToUniverse (opcode 48).
i32 RequestChrMoveToUniverse(CommandQueue& q, i32 a1, i32 a2, const char* name, i32 a4) {
    CommandPacket p{};
    p.opcode() = 48;                             // v9[0] = 48
    put32(p, 0x10, a1);                          // v10 = a1
    put32(p, 0x14, a2);                          // v11 = a2
    put32(p, 0x18, a4);                          // v12 = a4
    if (std::strlen(name) >= 0x20) {
        g_build.errorLog("cm: RequestChrMoveToUniverse — name too long");
        return q.EnqueuePacket(p);               // name NOT copied
    }
    // HE_NULL-class 2-byte-stride copy into v13 (payload +0x1C): copy bytes
    // pairwise, stopping at the first NUL in either lane.
    u8* dst = p.bytes + 0x1C;
    const char* src = name;
    for (;;) {
        char c0 = src[0];
        dst[0] = static_cast<u8>(c0);
        if (!c0) break;
        char c1 = src[1];
        dst[1] = static_cast<u8>(c1);
        src += 2; dst += 2;
        if (!c1) break;
    }
    return q.EnqueuePacket(p);
}

// ===========================================================================
// Selection-driven builders.
// ===========================================================================

// gilde.exe 0x4fbbcc — VIBE_Command_QueueSetSelectedFlag.
i32 QueueSetSelectedFlag(CommandQueue& q, DeltaWriter& dw, int sel, int idx,
                         const char* arg) {
    if (static_cast<u8>(arg[0]) != 45) return 0;         // *a2 != '-'
    u8 flagVal = static_cast<u8>(g_sel.parseInt(arg + 1)); // v6[0] = ParseInt(a2+1)
    i32 entityId = 0;
    int rec = g_sel.selectedPersonRecord(sel, idx, &entityId); // FindRecordById(...)
    if (!rec) return 0;
    // BeginDeltaPacket(rec, *(rec+4)); AppendRawField(1,1,433,&v6); QueueRequestState22
    dw.BeginDeltaPacket(reinterpret_cast<const void*>(static_cast<uintptr_t>(rec)),
                        static_cast<u32>(entityId));
    dw.AppendRawField(1, 1, 433, &flagVal);
    QueueRequestState22(q, dw);
    return 1;
}

// gilde.exe 0x4fbc40 — VIBE_Command_QueueRevealAllPersons.
i32 QueueRevealAllPersons(CommandQueue& q, const char* arg) {
    if (static_cast<u8>(arg[0]) != 45) return 0;         // *a1 != '-'
    const char* tail = arg + 1;
    if (std::strlen(tail) < 4) return 0;                 // strlen(v2) < 4
    // VIBE_Util_ToLower(v2+4) — lowercases the remainder in place. Behavioural
    // no-op for our emit logic; the original mutates the console buffer. Skipped
    // (we do not own the caller's buffer); documented.
    i32 active = g_sel.worldActiveCount();               // CountActiveObjects()
    if (!active) return 0;
    for (int i = 0; i < 768; ++i) {
        i32 entityId = 0;
        if (g_sel.revealableSlot(i, &entityId)) {
            // QueueRequest17(dword_12CE914[i], -1, 1, active, byte_6477A1, 0)
            QueueRequest17(q, entityId, -1, 1, static_cast<i16>(active),
                           g_sel.revealFlagByte(), 0);
        }
    }
    return 1;
}

} // namespace guild::sim
