#include "sim/he_recon3_worldpos.h"
#include "sim/npcaction.h"   // NpcClock() — the global 14-byte game clock image

#include <cstring>

namespace guild::sim {

// gilde.exe 0x4c6d39..0x4c6e08 — the fixed kind-byte decision tree. Each `al`
// value either falls through to loc_4C6D59 (RESET) or returns unchanged. The set
// below is the exhaustive list of kinds that RESET (traced from the cmp/jcc chain;
// all comparisons unsigned). Everything else leaves the record untouched.
bool He_WorldPosKindResets(u8 kind) {
    switch (kind) {
        case 0x16: case 0x18: case 0x19: case 0x1A:
        case 0x2D: case 0x2E: case 0x2F:
        case 0x35: case 0x36: case 0x37: case 0x38:
        case 0x41:
        case 0x45: case 0x46: case 0x47: case 0x4A: case 0x4B:
        case 0x5E: case 0x5F: case 0x60:
        case 0x69: case 0x6A:
        case 0x6F:
        case 0x7E:
            return true;
        default:
            return false;
    }
}

namespace {
// The RESET action shared by loc_4C6D20 and loc_4C6D59:
//   *(_DWORD *)(rec+112) = -2; *(_QWORD *)(rec+82) = qword_13CE852;
//   *(_DWORD *)(rec+90) = unk_13CE85A; *(_WORD *)(rec+94) = unk_13CE85E;
// i.e. state := -2 and the 14-byte global clock image is copied into the
// appointment slot at +82 (He_ApptTime).
inline void HeWorldPosReset(HeRecord* rec, const GameTime& clock) {
    He_State(rec) = -2;          // record+112 = 0xFFFFFFFE
    He_ApptTime(rec) = clock;    // record+82 = 14-byte clock image (movsd/movsd/movsd/movsw)
}
} // namespace

// gilde.exe 0x4c6cdc — VIBE_He_UpdateHandlerWorldPos.
u8 He_UpdateHandlerWorldPos(HeRecord* record, const HeWorldPosTable& table) {
    u8* rec = HeBytes(record);

    // if ( *(_WORD *)(rec+8) == 0xFFFF ) return;  (loc_4C6D1B: no owning row)
    u16 row = He_CityIndex(record);              // record+8 (word)
    if (row == 0xFFFF)
        return static_cast<u8>(reinterpret_cast<std::uintptr_t>(record)); // al = (record base)

    const GameTime& clock = table.clock ? *table.clock : NpcClock();

    // Read the 536-stride person/entity row (dword_12CE914 id, byte_12CE912 marker,
    // byte_12CE918 suppress). The inert default reports a row that never matches the
    // record id (-> stale-row RESET path), which exercises the reset logic headless.
    HeWorldPosRow r{};
    bool have = table.lookup ? table.lookup(row, &r) : false;

    i32 recId = He_CityId(record);               // record+12 (dword)

    // if ( recId != row.id || row.marker == 0x0F ) goto RESET;  (loc_4C6D20)
    if (!have || recId != r.id || r.marker == 0x0F) {
        HeWorldPosReset(record, clock);
        // The original's eax here is 536*row (the table byte offset). al = low byte.
        u32 byteOff = 536u * row;
        return static_cast<u8>(byteOff);
    }

    // if ( row.suppress != 0 ) return;  (jbe loc_4C6D39 only when suppress == 0)
    if (r.suppress != 0) {
        u32 byteOff = 536u * row;
        return static_cast<u8>(byteOff);     // al = low byte of 536*row, record untouched
    }

    // loc_4C6D39: switch on the kind byte record[0].
    u8 kind = rec[0];                            // mov al, [esi]
    if (He_WorldPosKindResets(kind)) {
        HeWorldPosReset(record, clock);
    }
    return kind;                                 // al = kind byte on every switch exit
}

} // namespace guild::sim
