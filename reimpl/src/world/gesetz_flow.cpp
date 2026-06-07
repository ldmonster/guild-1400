#include "world/gesetz_flow.h"

#include <cstring>

#include "world/crime.h"
#include "world/law.h"

// Faithful 1:1 port of the VIBE_Gesetz_* flow functions (gilde.exe 0x4c247c..).
// The law table is g_lawTable (law.cpp). The enact path mutates the threshold
// field (record +24 == dword_631EB0[9*id]); the clamp bounds are the record's +4
// and +8 dwords (v5[1]/v5[2]). The save/load formats are reproduced field-by-
// field against a sequential byte stream standing in for the VFS.

namespace guild::world {

// ===========================================================================
// Command / notify hooks (mock).
// ===========================================================================
namespace {
constexpr int kCmdLogCap = 16;
GesetzCommand g_cmdLog[kCmdLogCap];
int g_cmdLogCount = 0;
void DefaultCmdHook(const GesetzCommand& c, void*) {
    if (g_cmdLogCount < kCmdLogCap) g_cmdLog[g_cmdLogCount++] = c;
}
GesetzCommandHook g_cmdHook = &DefaultCmdHook;
void* g_cmdCtx = nullptr;

constexpr int kNotifyLogCap = 16;
GesetzNotifyEvent g_notifyLog[kNotifyLogCap];
int g_notifyLogCount = 0;
void DefaultNotifyHook(i32 m, u8 l, i32 t, void*) {
    if (g_notifyLogCount < kNotifyLogCap)
        g_notifyLog[g_notifyLogCount++] = GesetzNotifyEvent{m, l, t};
}
GesetzNotifyHook g_notifyHook = &DefaultNotifyHook;
void* g_notifyCtx = nullptr;

// Read the record's +4 / +8 dwords (the clamp bounds) from g_lawTable[id].
i32 LawField(u8 id, int byteOffset) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(&g_lawTable[id]) + byteOffset, 4);
    return v;
}
// The +28 / +29 bytes FindRecordByPair keys on.
u8 LawByte(u8 id, int byteOffset) {
    return *(reinterpret_cast<const u8*>(&g_lawTable[id]) + byteOffset);
}
} // namespace

void GesetzSetCommandHook(GesetzCommandHook hook, void* ctx) {
    g_cmdHook = hook ? hook : &DefaultCmdHook;
    g_cmdCtx = hook ? ctx : nullptr;
}
void GesetzCommandLogReset() {
    g_cmdHook = &DefaultCmdHook; g_cmdCtx = nullptr; g_cmdLogCount = 0;
}
const GesetzCommand* GesetzCommandLog(int* outCount) {
    if (outCount) *outCount = g_cmdLogCount;
    return g_cmdLog;
}
void GesetzSetNotifyHook(GesetzNotifyHook hook, void* ctx) {
    g_notifyHook = hook ? hook : &DefaultNotifyHook;
    g_notifyCtx = hook ? ctx : nullptr;
}
void GesetzNotifyLogReset() {
    g_notifyHook = &DefaultNotifyHook; g_notifyCtx = nullptr; g_notifyLogCount = 0;
}
const GesetzNotifyEvent* GesetzNotifyLog(int* outCount) {
    if (outCount) *outCount = g_notifyLogCount;
    return g_notifyLog;
}

// ===========================================================================
// gilde.exe 0x4c247c — VIBE_Gesetz_RequestApply.
// ===========================================================================
int GesetzRequestApply(u8 lawId, int value, const GesetzPerson& person) {
    if (lawId >= 26)                    // a2 >= 26
        return -1;

    // v5 = the 36-byte record; v5[1] == +4, v5[2] == +8.
    i32 lo = LawField(lawId, 4);        // v5[1]
    i32 hi = LawField(lawId, 8);        // v5[2]

    i32 v3;                             // initiator id
    if (person.present) {
        if (!person.valid)              // *(a1) == 0xFFFF
            return -1;
        v3 = person.ownerId;            // *(a1+4)
    } else {
        v3 = -1;                        // a1 == 0 -> v3 = -1
    }

    // Clamp value into [lo, hi].
    if (value >= lo) {
        if (value > hi)
            value = hi;
    } else {
        value = lo;
    }

    GesetzCommand cmd{70, v3, lawId, value};
    g_cmdHook(cmd, g_cmdCtx);
    return 0; // VIBE_Command_RequestBuildOp70 result (committed)
}

// ===========================================================================
// gilde.exe 0x4c24f8 — VIBE_Gesetz_ApplyAndNotify.
// ===========================================================================
int GesetzApplyAndNotify(const GesetzApplyCmd& cmd, i32 localMasterId,
                         bool initiatorResolves) {
    if (cmd.lawId >= 26)               // *(a1+4) >= 26 (signed char compare)
        return 0;
    // dword_631EB0[9*id] = *(a1+5) : write the new threshold into the law table.
    g_lawTable[cmd.lawId].threshold = cmd.newThreshold;

    // Notify the master unless the initiator IS the local master, or doesn't resolve.
    if (cmd.initiatorId == localMasterId || !initiatorResolves)
        return 1;
    g_notifyHook(cmd.initiatorId, cmd.lawId, cmd.newThreshold, g_notifyCtx);
    return 1;
}

// ===========================================================================
// gilde.exe 0x4c258c — VIBE_Gesetz_FindRecordByPair.
// ===========================================================================
int GesetzFindRecordByPair(u8 personOffice358, u8 op, LawRecord* out) {
    for (int v5 = 0; v5 < 936; v5 += kLawStride) {
        u8 id = static_cast<u8>(v5 / kLawStride);
        u8 b28 = LawByte(id, 28);     // byte_631EB4[v5]
        if (personOffice358 == b28) { // *(a1+358) == v6
            u8 b29 = LawByte(id, 29); // byte_631EB5[v5]
            if (op == b29) {
                std::memcpy(out, &g_lawTable[id], sizeof(LawRecord));
                return 1;
            }
        }
    }
    return 0;
}

// ===========================================================================
// Stream helpers (model VIBE_Vfs_WriteStream / _ReadStreamBool).
// ===========================================================================
bool GesetzStreamWrite(GesetzStream& s, const void* src, std::size_t n) {
    if (!s.data || s.pos + n > s.size)
        return false;
    std::memcpy(s.data + s.pos, src, n);
    s.pos += n;
    return true;
}
bool GesetzStreamRead(GesetzStream& s, void* dst, std::size_t n) {
    if (!s.data || s.pos + n > s.size)
        return false;
    std::memcpy(dst, s.data + s.pos, n);
    s.pos += n;
    return true;
}

namespace {
// Per-field offsets the save/load path writes for a crime record.
bool WriteCrime(GesetzStream& s, const CrimeRecord& c) {
    const u8* p = reinterpret_cast<const u8*>(&c);
    return GesetzStreamWrite(s, p + 0, 4)   // id
        && GesetzStreamWrite(s, p + 4, 14)  // +4..17
        && GesetzStreamWrite(s, p + 18, 4)  // +18
        && GesetzStreamWrite(s, p + 22, 4)  // perpetrator
        && GesetzStreamWrite(s, p + 26, 2)  // wanted
        && GesetzStreamWrite(s, p + 28, 1)  // location
        && GesetzStreamWrite(s, p + 29, 4)  // +29
        && GesetzStreamWrite(s, p + 33, 4)  // target
        && GesetzStreamWrite(s, p + 37, 4); // provenState
}
bool ReadCrime(GesetzStream& s, CrimeRecord& c) {
    u8* p = reinterpret_cast<u8*>(&c);
    return GesetzStreamRead(s, p + 0, 4)
        && GesetzStreamRead(s, p + 4, 14)
        && GesetzStreamRead(s, p + 18, 4)
        && GesetzStreamRead(s, p + 22, 4)
        && GesetzStreamRead(s, p + 26, 2)
        && GesetzStreamRead(s, p + 28, 1)
        && GesetzStreamRead(s, p + 29, 4)
        && GesetzStreamRead(s, p + 33, 4)
        && GesetzStreamRead(s, p + 37, 4);
}
} // namespace

// ===========================================================================
// gilde.exe 0x4c25e0 — VIBE_Gesetz_SaveState.
// ===========================================================================
int GesetzSaveState(GesetzStream& s) {
    if (!s.data)
        return 0;
    i32 lawCount = 26;
    i32 marker = 0; // dword_632240 (an engine-side marker; 0 in isolation)
    i32 crimeCount = 0;
    i32 evidenceCount = 0;

    if (!GesetzStreamWrite(s, &lawCount, 4))
        return 0;
    for (int i = 0; i < kLawCount; ++i) {
        i32 threshold = g_lawTable[i].threshold; // dword_631EB0[9*i]
        if (!GesetzStreamWrite(s, &threshold, 4))
            return 0;
    }
    if (!GesetzStreamWrite(s, &marker, 4))
        return 0;

    // Count active crimes (id != -1) and active evidence pairs.
    for (int i = 0; i < kCrimeCount; ++i)
        if (g_crimeTable[i].id != -1)
            ++crimeCount;
    for (int j = 0; j < kEvidenceDwords; j += 2)
        if (g_evidenceOwner[j] != -1 && g_evidenceCrimeId[j] != -1)
            ++evidenceCount;

    if (!GesetzStreamWrite(s, &crimeCount, 4) ||
        !GesetzStreamWrite(s, &evidenceCount, 4))
        return 0;

    for (int i = 0; i < kCrimeCount; ++i) {
        if (g_crimeTable[i].id != -1) {
            if (!WriteCrime(s, g_crimeTable[i]))
                return 0;
        }
    }
    for (int j = 0; j < kEvidenceDwords; j += 2) {
        if (g_evidenceOwner[j] != -1 && g_evidenceCrimeId[j] != -1) {
            if (!GesetzStreamWrite(s, &g_evidenceOwner[j], 4))
                return 0;
            if (!GesetzStreamWrite(s, &g_evidenceCrimeId[j], 4))
                return 0;
        }
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x4c28d8 — VIBE_Gesetz_LoadState.
// ===========================================================================
int GesetzLoadState(GesetzStream& s, u32 formatVersion) {
    if (!s.data)
        return 0;
    i32 lawCount = 0;
    if (!GesetzStreamRead(s, &lawCount, 4))
        return 0;
    if (lawCount != 26)
        return 0;
    for (int i = 0; i < kLawCount; ++i) {
        i32 threshold;
        if (!GesetzStreamRead(s, &threshold, 4))
            return 0;
        g_lawTable[i].threshold = threshold;
    }
    i32 marker;
    if (!GesetzStreamRead(s, &marker, 4))
        return 0;

    i32 crimeCount = 128;       // v10 default
    i32 evidenceCount = 512;    // v11[0] default
    if (formatVersion >= 0x10041u) {
        if (!GesetzStreamRead(s, &crimeCount, 4))
            return 0;
        if (!GesetzStreamRead(s, &evidenceCount, 4))
            return 0;
    }

    int loaded = 0;
    for (; loaded < crimeCount; ++loaded) {
        if (!ReadCrime(s, g_crimeTable[loaded]))
            return 0;
    }
    // LABEL_27: clear the remainder of the crime table (id/perp/target=-1, ...).
    for (int i = loaded; i < kCrimeCount; ++i) {
        g_crimeTable[i].id          = -1;
        g_crimeTable[i].perpetrator = -1;
        g_crimeTable[i].target      = -1; // dword_11BC745/49 region (+33)
        g_crimeTable[i].provenState = 0;  // dword_11BC758 (+37) cleared via word write
        g_crimeTable[i].wanted      = 0;
    }
    // Reset the evidence table to -1 (VIBE_Light_SetGrayColorThunk fill), then read.
    for (int j = 0; j < kEvidenceDwords; ++j) {
        g_evidenceOwner[j]   = -1;
        g_evidenceCrimeId[j] = -1;
    }
    for (int e = 0; e < evidenceCount; ++e) {
        i32 owner, crimeId;
        if (!GesetzStreamRead(s, &owner, 4))
            return 0;
        if (!GesetzStreamRead(s, &crimeId, 4))
            return 0;
        g_evidenceOwner[2 * e]   = owner;
        g_evidenceCrimeId[2 * e] = crimeId;
    }
    return 1;
}

} // namespace guild::world
