#include "sim/command_apply11.h"
#include "sim/command_codec.h"     // QueueRequest16/17/Coord27, DeltaWriter

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Cross-module hooks (inert defaults). See header.
// ===========================================================================
namespace {

i32  DefParseInt(const char*)               { return 0; }
void DefToLower(char*)                       {}
i16  DefCountActiveObjects()                 { return 0; }
int  DefGesetzGetRecord(u8, DebugCmdHooks::GesetzRecord* out) {
    if (out) *out = DebugCmdHooks::GesetzRecord{0, 0, 0};
    return 0;
}
void DefGesetzRequestApply(int, u8, i32)     {}

const DebugCmdHooks kDefaults = {
    &DefParseInt, &DefToLower, &DefCountActiveObjects,
    &DefGesetzGetRecord, &DefGesetzRequestApply,
};

DebugCmdHooks g_hooks = kDefaults;

// Flat-byte scratch accessors (the originals treat the scratch as raw bytes).
inline void wrU8 (SlotResetScratch& s, int off, u8  v) {
    std::memcpy(reinterpret_cast<u8*>(s.words) + off, &v, 1);
}
inline void wrU16(SlotResetScratch& s, int off, u16 v) {
    std::memcpy(reinterpret_cast<u8*>(s.words) + off, &v, 2);
}
inline void wrU32(SlotResetScratch& s, int off, u32 v) {
    std::memcpy(reinterpret_cast<u8*>(s.words) + off, &v, 4);
}

} // namespace

DebugCmdHooks SetDebugCmdHooks(const DebugCmdHooks& h) {
    DebugCmdHooks prev = g_hooks;
    g_hooks = h;
    if (!g_hooks.parseInt)           g_hooks.parseInt = kDefaults.parseInt;
    if (!g_hooks.toLower)            g_hooks.toLower = kDefaults.toLower;
    if (!g_hooks.countActiveObjects) g_hooks.countActiveObjects = kDefaults.countActiveObjects;
    if (!g_hooks.gesetzGetRecord)    g_hooks.gesetzGetRecord = kDefaults.gesetzGetRecord;
    if (!g_hooks.gesetzRequestApply) g_hooks.gesetzRequestApply = kDefaults.gesetzRequestApply;
    return prev;
}
DebugCmdHooks& GetDebugCmdHooks() { return g_hooks; }

// ===========================================================================
// Opcode-28 scratch packer (the shared body of the (A) emitters). The original
// SetGrayColorThunk(0,248) is a dword-fill memset of the 248-byte scratch with 0;
// we mirror with a zero-init then the per-emitter field writes. gameTime (14
// bytes, ctx.gameTime) is copied to scratch+0x28; word2Hi (>=0) then overwrites
// the word at scratch+0x2C (the WORD2(qword) store in the originals).
// ===========================================================================
void PackSlotResetScratch(SlotResetScratch& scratch, const DebugCmdCtx& ctx,
                          u8 opByte, i32 word2Hi, i32 word8, i32 word12,
                          u8 byte0x36, i32 word0x58, i32 word0x9C,
                          bool hasWord0x58, bool hasWord0x9C) {
    std::memset(scratch.words, 0, sizeof(scratch.words));
    wrU8 (scratch, 0x04, opByte);              // v3/v4 = opcode field
    wrU32(scratch, 0x08, static_cast<u32>(word8));   // player id (dword_6498E4+4)
    wrU32(scratch, 0x0C, static_cast<u32>(word12));  // v5 = -1
    // gameTime stamp at +0x28 (qword || dword || word == 14 bytes).
    std::memcpy(reinterpret_cast<u8*>(scratch.words) + 0x28, ctx.gameTime, 14);
    if (word2Hi >= 0)
        wrU16(scratch, 0x2C, static_cast<u16>(word2Hi)); // WORD2(v6)
    wrU8 (scratch, 0x36, byte0x36);            // v9 = flag
    if (hasWord0x58) wrU32(scratch, 0x58, static_cast<u32>(word0x58)); // v10
    if (hasWord0x9C) wrU32(scratch, 0x9C, static_cast<u32>(word0x9C)); // v11
}

// ===========================================================================
// (A) opcode-28 emitters.
// ===========================================================================

// gilde.exe 0x4fb284
i32 QueueGiveGold(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx) {
    SlotResetScratch s{};
    // v3=78; v4=playerId; v5=-1; v10=v0(0); v11=1000; v9=1; WORD2(gameTime)=7.
    PackSlotResetScratch(s, ctx, /*op*/78, /*word2Hi*/7, /*w8*/ctx.playerId,
                         /*w12*/-1, /*b36*/1, /*w58*/0, /*w9C*/1000,
                         /*has58*/true, /*has9C*/true);
    QueueRequestSlotReset28(q, pending, s, /*a2*/0);
    return 1;
}

// gilde.exe 0x4fb300
i32 QueueAdjustReputation(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx) {
    SlotResetScratch s{};
    // v3=80; v4=playerId; v5=-1; v9=2; v10=-1; v11=1000; WORD2(gameTime)=16; a2=-1.
    PackSlotResetScratch(s, ctx, /*op*/80, /*word2Hi*/16, /*w8*/ctx.playerId,
                         /*w12*/-1, /*b36*/2, /*w58*/-1, /*w9C*/1000,
                         /*has58*/true, /*has9C*/true);
    QueueRequestSlotReset28(q, pending, s, /*a2*/-1);
    return 1;
}

// gilde.exe 0x4fb210. Layout differs slightly: v7=qword@+0x28, v10=word@+0x32,
// v11=word@+0x34, v8=word@+0x2C:=8, v9=dword@+0x2E:=0, v12=byte@+0x36:=1,
// v13=dword@+0x58:=v0(0). The original copies only 12 bytes of GameTime here
// (qword + word@+0x32 + word@+0x34); word@+0x2C is the discriminator 8.
i32 QueueSpawnGuard(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx) {
    SlotResetScratch s{};
    std::memset(s.words, 0, sizeof(s.words));
    wrU8 (s, 0x04, 125);
    wrU32(s, 0x08, static_cast<u32>(ctx.playerId));
    wrU32(s, 0x0C, 0xFFFFFFFFu);                          // -1
    std::memcpy(reinterpret_cast<u8*>(s.words) + 0x28, ctx.gameTime, 8); // qword
    wrU16(s, 0x32, static_cast<u16>(ctx.gameTime[10] | (ctx.gameTime[11] << 8))); // v10
    wrU16(s, 0x34, static_cast<u16>(ctx.gameTime[12] | (ctx.gameTime[13] << 8))); // v11
    wrU16(s, 0x2C, 8);                                    // v8
    wrU32(s, 0x2E, 0);                                    // v9
    wrU8 (s, 0x36, 1);                                    // v12
    wrU32(s, 0x58, 0);                                    // v13 = v0
    QueueRequestSlotReset28(q, pending, s, /*a2*/0);
    return 1;
}

// gilde.exe 0x4fb0f0. Gate: arg[0]=='-' && parseInt(arg+1) > 0.
i32 QueueSpawnSelected(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx,
                       const char* arg) {
    if (!arg || arg[0] != '-') return 0;          // *a1 != 45
    if (g_hooks.parseInt(arg + 1) <= 0) return 1; // parsed <= 0 -> no emit, returns 1
    SlotResetScratch s{};
    std::memset(s.words, 0, sizeof(s.words));
    wrU8 (s, 0x04, 83);
    wrU32(s, 0x08, static_cast<u32>(ctx.playerId));
    wrU32(s, 0x0C, 0xFFFFFFFFu);
    std::memcpy(reinterpret_cast<u8*>(s.words) + 0x28, ctx.gameTime, 8);
    wrU16(s, 0x32, static_cast<u16>(ctx.gameTime[10] | (ctx.gameTime[11] << 8)));
    wrU16(s, 0x34, static_cast<u16>(ctx.gameTime[12] | (ctx.gameTime[13] << 8)));
    wrU16(s, 0x2C, 12);                            // v8
    wrU32(s, 0x2E, 0);                             // v9
    wrU8 (s, 0x36, 1);                             // v12
    wrU8 (s, 0x58, 0);                             // v13 (byte) = 0
    QueueRequestSlotReset28(q, pending, s, /*a2*/0);
    return 1;
}

// gilde.exe 0x4fb180. Scans the 768 scene records; for each whose class byte is
// 6 or 7 it stamps dword@+0x10 with the record id and emits a reset. Layout here
// is shifted by +8 vs Gold (v10=qword@+0x30, v11=word@+0x34, etc.).
i32 QueueRemoveAllCarried(CommandQueue& q, PendingState& pending, const DebugCmdCtx& ctx) {
    // Per-iteration the scratch is rebuilt from the constant template, then v8
    // (dword@+0x08) := the matched record id; the scan walks 768 records.
    for (int i = 0; i < 768; ++i) {
        i32 cls = ctx.selectionActive ? ctx.selectionActive(i) : 0; // class byte proxy
        if (cls != 6 && cls != 7) continue;
        i32 id = ctx.selectedId ? ctx.selectedId(i) : 0;            // dword_12CE914[i]
        SlotResetScratch s{};
        std::memset(s.words, 0, sizeof(s.words));
        wrU8 (s, 0x04, 127);                       // v7
        wrU32(s, 0x0C, 0xFFFFFFFFu);               // v9 = -1
        std::memcpy(reinterpret_cast<u8*>(s.words) + 0x30, ctx.gameTime, 8); // v10 qword
        wrU16(s, 0x3A, static_cast<u16>(ctx.gameTime[10] | (ctx.gameTime[11] << 8))); // v13
        wrU16(s, 0x3C, static_cast<u16>(ctx.gameTime[12] | (ctx.gameTime[13] << 8))); // v14
        wrU16(s, 0x34, 8);                         // v11
        wrU32(s, 0x36, 0);                         // v12
        wrU8 (s, 0x3E, 1);                         // v15
        wrU32(s, 0x08, static_cast<u32>(id));      // v8 = record id (dword@+0x08)
        QueueRequestSlotReset28(q, pending, s, /*a2*/0);
    }
    return 1;
}

// ===========================================================================
// (B) string-parse + scan emitters.
// ===========================================================================
namespace {

// Match the "-NAME_value" head against a 2-entry sign table, returning the slot
// (0 or 1) and a pointer just past the matched name, or -1 on no match. The
// originals compare with memcmp(arg, table[k], strlen(table[k])); we mirror.
int MatchSignTable(const char* tail, const char* const names[2],
                   const char** pastName) {
    for (int k = 0; k < 2; ++k) {
        std::size_t n = std::strlen(names[k]);
        if (std::memcmp(tail, names[k], n) == 0) { *pastName = tail + n; return k; }
    }
    return -1;
}

} // namespace

// gilde.exe 0x4fb89c — whole body is a Person/Building/Coord cross-module flow;
// routed through the hook. Default inert. (Kept as a hookable entry point so the
// e2e flow can drive it; no field-packing of its own.)
i32 QueueRevealSelected(CommandQueue& q, const DebugCmdCtx& ctx, i32 selBase, i32 a2) {
    (void)q; (void)ctx;
    if (!selBase || a2 >= 8) return 0;            // !a1 || a2 >= 8
    // The original then resolves a person and emits a building stock adjust; that
    // chain is unreconstructed this wave. Returning 1 mirrors the "ran" path.
    return 1;
}

// gilde.exe 0x4fb52c
i32 QueueAdjustAllPersonStat(CommandQueue& q, const DebugCmdCtx& ctx, const char* arg) {
    if (!arg || arg[0] != '-') return 0;
    i32 n = g_hooks.parseInt(arg + 1);
    // Per active person: emit QueueRequest16(personId, ?, scaledWealth, 0). The
    // wealth/scale chain is cross-module; we emit one packet per selected id with
    // the parsed magnitude so the wire-shape and count are reproduced.
    for (int i = 0; i < 768; ++i) {
        i32 active = ctx.selectionActive ? ctx.selectionActive(i) : 0;
        if (!active) continue;
        i32 id = ctx.selectedId ? ctx.selectedId(i) : 0;
        QueueRequest16(q, id, 0, n, 0);
    }
    return 1;
}

// gilde.exe 0x4fb37c
i32 QueueAdjustBuildingStat(CommandQueue& q, const DebugCmdCtx& ctx, const char* arg) {
    if (!arg || arg[0] != '-') return 0;
    static const char* kNames[2] = {"MINUS", "PLUS"};
    const char* past = nullptr;
    int slot = MatchSignTable(arg + 1, kNames, &past);
    if (slot < 0) return 0;
    if (!past || *past != '_') return 0;          // trailing '_' separator
    i32 v = g_hooks.parseInt(past + 1);
    if (v <= 0) return 1;                          // v15 <= 0 -> returns 1, no emit
    // The building-output lookup is cross-module; emit one packet keyed to the
    // sign slot (signTable[slot] is the recovered building stat id).
    i32 signedId = ctx.selectedId ? ctx.selectedId(slot) : (slot == 0 ? -1 : 1);
    QueueRequest16(q, signedId, 0, v, 0);
    return 1;
}

// gilde.exe 0x4fb614
i32 QueueAdjustSelectedStat(CommandQueue& q, const DebugCmdCtx& ctx, i32 selBase,
                            i32 a2, const char* arg) {
    (void)ctx;
    if (!selBase || a2 >= 8) return 0;
    if (!arg || arg[0] != '-') return 0;
    static const char* kNames[2] = {"MINUS", "PLUS"};
    const char* past = nullptr;
    int slot = MatchSignTable(arg + 1, kNames, &past);
    if (slot < 0) return 0;
    if (!past || *past != '_') return 0;
    i32 v = g_hooks.parseInt(past + 1);
    // sign slot 1 (PLUS) -> from/to swap (the v11==-1 branch). We pass the parsed
    // magnitude as the amount and reproduce the from/to ordering.
    if (slot == 0) QueueRequest16(q, -1, selBase, v, 0);  // MINUS: to = person
    else           QueueRequest16(q, selBase, -1, v, 0);  // PLUS:  from = person
    return 1;
}

// gilde.exe 0x4fb754
i32 QueueMovePersonsToCoord(CommandQueue& q, const DebugCmdCtx& ctx, i32 selBase,
                            i32 a2, const char* arg) {
    if (!selBase || a2 >= 8) return 0;
    if (!arg || arg[0] != '-') return 0;
    static const char* kNames[2] = {"MINUS", "PLUS"};
    const char* past = nullptr;
    int slot = MatchSignTable(arg + 1, kNames, &past);
    if (slot < 0) return 0;
    if (!past || *past != '_') return 0;
    i32 n = g_hooks.parseInt(past + 1);
    // v10 = signTable[slot] * 2 * n. signTable is ctx.selectedId(slot) here.
    i32 sign = ctx.selectedId ? ctx.selectedId(slot) : (slot == 0 ? -1 : 1);
    i32 v10 = sign * 2 * n;
    for (int i = 0; i < 768; ++i) {
        i32 active = ctx.selectionActive ? ctx.selectionActive(i) : 0;
        if (!active) continue;
        i32 id = ctx.selectedId ? ctx.selectedId(i) : 0;
        QueueRequestCoord27(q, selBase, id, v10, 0, 0);
    }
    return 1;
}

// gilde.exe 0x4fbc40
i32 QueueRevealAllPersons(CommandQueue& q, const DebugCmdCtx& ctx, const char* arg) {
    if (!arg || arg[0] != '-') return 0;
    const char* tail = arg + 1;
    if (std::strlen(tail) < 4) return 0;
    // ToLower(tail+4) in place — requires a mutable buffer in the original; we
    // call the hook on a copy-safe pointer (host passes a writable arg).
    g_hooks.toLower(const_cast<char*>(tail) + 4);
    i16 active = g_hooks.countActiveObjects();
    if (!active) return 0;
    for (int i = 0; i < 768; ++i) {
        i32 a = ctx.selectionActive ? ctx.selectionActive(i) : 0;
        if (!a) continue;
        i32 id = ctx.selectedId ? ctx.selectedId(i) : 0;
        QueueRequest17(q, id, -1, /*a3*/1, /*a4 word*/active, /*a5 byte*/0, /*a6*/0);
    }
    return 1;
}

// gilde.exe 0x4fbbcc
i32 QueueSetSelectedFlag(CommandQueue& q, DeltaWriter& dw, const DebugCmdCtx& ctx,
                         i32 selBase, i32 a2, const char* arg) {
    if (!arg || arg[0] != '-') return 0;
    u8 val = static_cast<u8>(g_hooks.parseInt(arg + 1));
    if (!selBase || a2 >= 8) return 0;
    // The original finds the person record (FindRecordById) then writes a 1-byte
    // raw field at offset 433. The record memory is supplied by ctx via selectedId
    // acting as the resolved base id; for the codec we drive the DeltaWriter with
    // a small backing record so State22 round-trips.
    (void)ctx;
    static u8 record[600];
    std::memset(record, 0, sizeof(record));
    i32 entId = selBase;
    std::memcpy(record + 4, &entId, 4);
    dw.BeginDeltaPacket(record, static_cast<u32>(entId));
    dw.AppendRawField(/*width*/1, /*count*/1, /*offset*/433, &val);
    QueueRequestState22(q, dw);
    return 1;
}

// gilde.exe 0x4fbd00 — RECHTSPRECHUNG_HAERTE law-severity SET with two-stage clamp.
i32 QueueSetJusticeSeverity(const DebugCmdCtx& ctx, i32 lawSel, const char* arg) {
    (void)ctx;
    if (!arg || arg[0] != '-') return 0;
    static const char* kNames[2] = {"RECHTSPRECHUNG_HAERTE", "RECHTSPRECHUNG_HAERTE"};
    const char* past = nullptr;
    int slot = MatchSignTable(arg + 1, kNames, &past);
    if (slot < 0) return 0;
    if (!past || *past != '_') return 0;
    i32 v8 = g_hooks.parseInt(past + 1);
    DebugCmdHooks::GesetzRecord rec{};
    u8 lawId = static_cast<u8>(lawSel);
    if (!g_hooks.gesetzGetRecord(lawId, &rec)) return 0;
    i32 lo = rec.lo, hi = rec.hi;                    // v14, v15
    if ((v8 < lo || v8 > hi) && rec.applyFlag) return 0;
    // Two-stage clamp from the original:
    //   v10 = (v8 <= hi) ? v8 : hi;
    //   v11 = (v10 <= lo) ? lo : ((v8 <= hi) ? v8 : hi);
    i32 v10 = (v8 <= hi) ? v8 : hi;
    i32 v11 = (v10 <= lo) ? lo : ((v8 <= hi) ? v8 : hi);
    g_hooks.gesetzRequestApply(0, lawId, v11);
    return 1;
}

// gilde.exe 0x4fbe04 — RECHTSPRECHUNG_HAERTE law-severity ADJUST (signed delta).
i32 QueueAdjustJusticeSeverity(const DebugCmdCtx& ctx, i32 lawSel, const char* arg) {
    if (!arg || arg[0] != '-') return 0;
    static const char* kSign[2] = {"MINUS", "PLUS"};
    const char* afterSign = nullptr;
    int signSlot = -1;
    {
        const char* tail = arg + 1;
        for (int k = 0; k < 2; ++k) {
            std::size_t n = std::strlen(kSign[k]);
            if (std::memcmp(tail, kSign[k], n) == 0) { signSlot = k; afterSign = tail + n; break; }
        }
    }
    if (signSlot < 0) return 0;
    i32 sign = (signSlot == 1) ? 1 : -1;             // v25[0]: -1 default, +1 if slot1
    if (!afterSign || *afterSign != '_') return 0;
    // Second match: the law name (RECHTSPRECHUNG_HAERTE) then '_value'.
    static const char* kNames[2] = {"RECHTSPRECHUNG_HAERTE", "RECHTSPRECHUNG_HAERTE"};
    const char* nameTail = afterSign + 1;
    const char* past = nullptr;
    int nameSlot = MatchSignTable(nameTail, kNames, &past);
    if (nameSlot < 0) return 0;
    if (!past || *past != '_') return 0;
    i32 n = g_hooks.parseInt(past + 1);
    DebugCmdHooks::GesetzRecord rec{};
    u8 lawId = static_cast<u8>(lawSel);
    if (!g_hooks.gesetzGetRecord(lawId, &rec)) return 0;
    i32 lo = rec.lo, hi = rec.hi;                    // v21, v22
    i32 curr = (ctx.playerId != -1) ? 0 : 0;         // v23 (current severity) — host
    (void)curr;
    i32 v16 = 0 /*v23*/ + sign * n;                  // curr + sign*N
    // Two-stage clamp into [lo, hi] (the original's exact form):
    //   v17 = (v16 <= hi) ? v16 : hi;
    //   v18 = (v17 <= lo) ? lo : ((v16 > hi) ? hi : v16);
    i32 v17 = (v16 <= hi) ? v16 : hi;
    i32 v18 = (v17 <= lo) ? lo : ((v16 > hi) ? hi : v16);
    g_hooks.gesetzRequestApply(0, lawId, v18);
    return 1;
}

} // namespace guild::sim
