#include "world/straftat_table.h"

#include <cstring>

// Faithful 1:1 port of the VIBE_StraftatTable_* / VIBE_Mission_TrackCrimeProgress
// accessors. The original walks byte_122FEC0 with a 9-dword (36-byte) stride and a
// 1152-dword bound; the parallel global dword_122FEC4 is the same record's +4
// source key. We model the table as a real StraftatTableRecord[128] and the
// original's "base + 36*i" pointer arithmetic becomes field access on
// g_crimeTrackTable[i]. The "return a record pointer / null" originals become
// "return a record index / -1".

namespace guild::world {

StraftatTableRecord g_crimeTrackTable[kStraftatTableCount];

u8  g_crimeTrackStaging8[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // qword_13CE852
i32 g_crimeTrackStaging16   = 0;                        // unk_13CE85A
u16 g_crimeTrackStaging20   = 0;                        // unk_13CE85E

void StraftatTableReset() {
    std::memset(g_crimeTrackTable, 0, sizeof(g_crimeTrackTable)); // type==0 -> free
}

namespace {
struct StraftatTableInit {
    StraftatTableInit() { StraftatTableReset(); }
} g_crimeTrackTableInit;
} // namespace

// gilde.exe 0x53846c — VIBE_StraftatTable_FindBySource.
int StraftatTableFindBySource(i32 sourceKey) {
    // Original loop: v2 = 0; while (!byte_122FEC0[v2*4] || dword_122FEC4[v2] != *(src+4))
    //   { v2 += 9; if (v2 >= 1152) return 0; }  return &byte_122FEC0[v2*4];
    // v2 steps in dwords (9 per record); v2/9 is the record index.
    for (int i = 0; i < kStraftatTableCount; ++i) {
        const StraftatTableRecord& rec = g_crimeTrackTable[i];
        if (rec.type != 0 && rec.source == sourceKey) // occupied && key match
            return i;
    }
    return -1;
}

// gilde.exe 0x5384a0 — VIBE_StraftatTable_FindAndInit.
int StraftatTableFindAndInit(i32 sourceKey) {
    int idx = StraftatTableFindBySource(sourceKey);
    if (idx >= 0) {
        StraftatTableRecord& rec = g_crimeTrackTable[idx];
        std::memcpy(rec.staging8, g_crimeTrackStaging8, 8); // = qword_13CE852
        rec.staging16 = g_crimeTrackStaging16;              // = unk_13CE85A
        rec.staging20 = g_crimeTrackStaging20;              // = unk_13CE85E
        rec.field18   = 0;                                // result[+6 dword] = 0
        rec.progress  = 0;                                // result[+7 dword] = 0
        rec.field20   = 0;                                // result[32] = 0
    }
    return idx;
}

// gilde.exe 0x538524 — VIBE_StraftatTable_ContainsSource.
int StraftatTableContainsSource(i32 sourceKey) {
    for (int i = 0; i < kStraftatTableCount; ++i) {
        const StraftatTableRecord& rec = g_crimeTrackTable[i];
        if (rec.type != 0 && rec.source == sourceKey) // occupied && key match
            return 1;
    }
    return 0;
}

// gilde.exe 0x539054 — VIBE_Mission_TrackCrimeProgress.
int MissionTrackCrimeProgress(i32 sourceKey, u8 crimeType) {
    int idx = StraftatTableFindBySource(sourceKey);
    if (idx < 0)
        return 0; // !result -> return 0

    // Accept only the tracked crime-type codes. Recovered from the original's
    // unsigned compare chain (cmp dl,17h / 0Bh / 1Ch / 28h / 13h):
    //   accepted: 0x0B, 0x13, 0x17, 0x1C, 0x28; everything else -> return 0.
    bool tracked;
    if (crimeType < 0x17u) {
        if (crimeType < 0x0Bu)
            tracked = false;                 // < 0x0B
        else if (crimeType == 0x0Bu)
            tracked = true;                  // == 0x0B
        else
            tracked = (crimeType == 0x13u);  // 0x0C..0x16: only 0x13
    } else if (crimeType == 0x17u) {
        tracked = true;                      // == 0x17
    } else if (crimeType < 0x1Cu) {
        tracked = false;                     // 0x18..0x1B
    } else if (crimeType == 0x1Cu) {
        tracked = true;                      // == 0x1C
    } else {
        tracked = (crimeType == 0x28u);      // > 0x1C: only 0x28
    }
    if (!tracked)
        return 0;

    StraftatTableRecord& rec = g_crimeTrackTable[idx];
    if (rec.type == crimeType)       // (unsigned __int8)*result == v2
        ++rec.progress;              // ++*((_DWORD*)result + 7)
    return 1;                        // return (char*)1
}

} // namespace guild::world
