#include "sim/he.h"
#include "sim/handler_entry.h"

// he.cpp — implementation half of the "He" subsystem. he.h recovers the per-NPC
// He/handler-record field layout (the +82/+112/+172 record the NpcAction steps
// drive); handler_entry.{h,cpp} recovers the handler-entry TABLE ops (alloc/find/
// free/filter/run). This TU carries the one He function that operates on the
// separate 45-byte-stride "He entity" table plus its recovered score table:
// VIBE_He_SumPlayerHandlerValues @0x4c3a78.

namespace guild::sim {

// ===========================================================================
// unk_631E98 — 26-entry × 36-byte score table. Sum reads column +12 (dword
// index 3). Recovered byte-for-byte via get_bytes(0x631E98, 936). The trailing
// dword (index 8) is a 0x46xxxx string pointer in the original; preserved as the
// recovered value for completeness (it does not affect any computation here).
// ===========================================================================
const i32 kHeScoreTable[kHeScoreEntries][kHeScoreStride / 4] = {
    { 0x100, 0x0, 0x4,  0x0, 0x0, 0x1, 0x2,  0x118, 0x464660 },
    { 0x201, 0x0, 0x1,  0x0, 0x0, 0x0, 0x0,  0x218, 0x4647f8 },
    { 0x302, 0x0, 0x2,  0x2, 0x6, 0x7, 0x0,  0x11a, 0x464a30 },
    { 0x203, 0x0, 0x1,  0x2, 0x6, 0x1, 0x0,  0x112, 0x464ad4 },
    { 0x204, 0x0, 0x1,  0x4, 0xa, 0x1, 0x0,  0x116, 0x464c00 },
    { 0x105, 0x0, 0x4,  0x0, 0x0, 0x0, 0x2,  0x10e, 0x464d0c },
    { 0x106, 0x0, 0x4,  0x0, 0x0, 0x0, 0x2,  0x10a, 0x464d84 },
    { 0x107, 0x0, 0x4,  0x0, 0x0, 0x0, 0x2,  0x10b, 0x464ddc },
    { 0x8,   0x8, 0x12, 0x0, 0x0, 0x0, 0xa,  0x10f, 0x464e4c },
    { 0x9,   0x6, 0x12, 0x0, 0x0, 0x0, 0xa,  0x20f, 0x46505c },
    { 0xa,   0x6, 0xa,  0x0, 0x0, 0x0, 0x8,  0x115, 0x4652a8 },
    { 0xb,   0x5, 0xb,  0x0, 0x0, 0x0, 0x8,  0x10c, 0x4653c0 },
    { 0xc,   0xa, 0x19, 0x0, 0x0, 0x0, 0x10, 0x20e, 0x4654a4 },
    { 0xd,   0xa, 0x19, 0x3, 0xa, 0x5, 0x10, 0x110, 0x465588 },
    { 0x10e, 0x0, 0x4,  0x0, 0x0, 0x0, 0x2,  0x119, 0x46566c },
    { 0x20f, 0x0, 0x1,  0x3, 0xc, 0x1, 0x0,  0x10d, 0x4657b0 },
    { 0x210, 0x0, 0x1,  0x3, 0xc, 0x1, 0x0,  0x219, 0x4658f8 },
    { 0x211, 0x0, 0x1,  0x3, 0xc, 0x1, 0x0,  0x112, 0x4659e0 },
    { 0x212, 0x0, 0x1,  0x3, 0xc, 0x1, 0x0,  0x113, 0x465ac8 },
    { 0x213, 0x0, 0x1,  0x2, 0xc, 0x1, 0x0,  0x213, 0x465bc0 },
    { 0x214, 0x0, 0x1,  0x5, 0xc, 0x1, 0x1,  0x111, 0x465ccc },
    { 0x215, 0x0, 0x1,  0x4, 0xc, 0x1, 0x1,  0x114, 0x465dd8 },
    { 0x216, 0x0, 0x1,  0x5, 0xc, 0x1, 0x1,  0x117, 0x465efc },
    { 0x217, 0x0, 0x1,  0x5, 0xc, 0x1, 0x1,  0x216, 0x466020 },
    { 0x218, 0x0, 0x1,  0x5, 0xc, 0x1, 0x1,  0x217, 0x466138 },
    { 0x219, 0x0, 0x1,  0x5, 0xc, 0x1, 0x1,  0x214, 0x46625c },
};

// gilde.exe 0x4c3a78 — VIBE_He_SumPlayerHandlerValues(playerId@eax).
//   for (i = 0; i != 23040; i += 45)
//     if (owner[i]==playerId && state[i]==1) { a=action[i]; if(a<26) v6=score[a];
//                                              total += v6[3]; }
//   The `v6` 36-byte frame is reused (not re-cleared) when action >= 26, so the
//   previous match's score row carries forward — faithfully preserved below.
int He_SumPlayerHandlerValues(const HeEntityTable& table, i32 playerId) {
    int total = 0;
    const i32* row = nullptr;   // v6 — carries forward when action >= 26
    for (u32 e = 0; e < HeEntityTable::kCount; ++e) {
        const u8* entry = table.bytes + 45 * e;
        i32 owner = *reinterpret_cast<const i32*>(entry);
        i32 state = *reinterpret_cast<const i32*>(entry + 15);
        if (owner == playerId && state == 1) {
            // gilde.exe reads the action column as a SIGNED char (`char v4`) and
            // tests `v4 < 26` (signed). For 0..25 this indexes the score table; for
            // 26..127 it leaves `v6` carrying the previous row. For 128..255 the
            // original's signed compare is TRUE and it qmemcpy's from a NEGATIVE
            // index (memory before unk_631E98) — an OOB read of data outside this
            // tree. We reproduce the signed comparison faithfully; for in-range
            // action bytes (0..25) the index is taken, otherwise the row carries
            // forward. The negative-index region is a BOUNDARY (data not in tree).
            i8 action = static_cast<i8>(entry[6]);   // char v4 (signed)
            if (action < static_cast<i8>(kHeScoreEntries)) {
                if (action >= 0)
                    row = kHeScoreTable[action];
                // else: original reads kHeScoreTable[negative] (OOB, out of tree) —
                // left as a documented boundary; `row` carries forward.
            }
            if (row)
                total += row[3];   // v6[3] — column +12
        }
    }
    return total;
}

} // namespace guild::sim
