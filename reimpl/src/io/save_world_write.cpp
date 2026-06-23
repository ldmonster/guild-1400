// gilde.exe — guild::io  (MODULE: world WRITE driver + Amt table serializers)
//
// 1:1 reconstructions:
//   VIBE_Amt_SaveAemter          @0x483198
//   VIBE_Amt_LoadAemter          @0x4832d0
//   VIBE_Save_WriteGameFile      @0x5a348c  (PARTIAL path — composes the
//       already-reconstructed per-table writers in the recovered order)
//
// REUSED (extern, never redefined — ODR):
//   io::SaveWriteScenarioBlock / SaveWriteScalarBlock / SaveVersionGet (io/save)
//   io::SaveWriteMapTileTable                                  (io/save_tables)
//   io::SaveWritePersonTable / SaveWriteGameStateHeaderPreamble /
//     SaveWritePersonSceneRecord                               (io/save_person)
//   io::SaveWriteBuildingTable                                 (io/save_building)
//   io::SaveWriteBuildingSlotTables                            (io/save_serial3)
#include "io/save_world_write.h"

#include <cstring>

#include "io/save_building.h"   // SaveWriteBuildingTable @0x5a45bc
#include "io/save_serial3.h"    // SaveWriteBuildingSlotTables @0x5a5c1c
#include "io/save_tables.h"     // SaveWriteMapTileTable @0x5a3fa4

namespace guild::io {

namespace {
inline bool WR(VfsHandle* h, const void* p, guild::u32 n) {
    return VfsWriteStream(p, n, h, 1) == n;
}
inline bool RD(VfsHandle* h, void* p, guild::u32 n) {
    return VfsReadStream(p, n, h, 1) == n;
}

// gilde.exe 0x5a4980/0x5a4985 — VIBE_Save_WriteGameStateHeader's live-slot scan.
// The disasm is unambiguous: `add eax, 218h` (stride 536 bytes) and `cmp eax,
// 64800h` (terminate at 411648 bytes) => 411648/536 == 768 person records. The
// shared io::kPersScanBytes (save_person.h) carries the Hex-Rays word-scaled
// artifact (205824 == 411648/2, half the records), so this driver uses the byte-
// accurate span recovered from the disassembly instead.
constexpr guild::u32 kPersScanBytesExact = 411648;  // 0x64800 == 536 * 768
} // namespace

// ===========================================================================
// gilde.exe 0x483198 — VIBE_Amt_SaveAemter.
//   v5[0] = 37; write(v5, 4);
//   v2 = byte_B59848; for v3 in 0..36, v2 += 24:
//     write(v2+0, 1) write(v2+4, 4) write(v2+8, 1)
//     write(v2+12, 4) write(v2+16, 1) write(v2+20, 4)
// (The original also sprintf's "amt_fio_SaveAemter(): count is %i" into a stack
// scratch buffer that is never used — no observable side effect.)
// ===========================================================================
bool AmtSaveAemter(VfsHandle* h, const guild::u8* holderBase) {
    if (!h || !holderBase)
        return false;                          // original: if (!result) return 0
    guild::u32 count = kAemterCount;           // v5[0] = 37
    if (!WR(h, &count, 4))
        return false;
    const guild::u8* r = holderBase;           // v2 = byte_B59848
    for (guild::u32 i = 0; i < kAemterCount; ++i, r += kAemterStride) {
        if (!WR(h, r + 0, 1) || !WR(h, r + 4, 4) || !WR(h, r + 8, 1)
            || !WR(h, r + 12, 4) || !WR(h, r + 16, 1) || !WR(h, r + 20, 4))
            return false;
    }
    return true;
}

// ===========================================================================
// gilde.exe 0x4832d0 — VIBE_Amt_LoadAemter.
//   read count (4); if count != 37 -> sprintf error scratch, return 0;
//   for v3 in 0..36, v2 += 24: read the same six fields.
// ===========================================================================
bool AmtLoadAemter(VfsHandle* h, guild::u8* holderBase) {
    if (!h || !holderBase)
        return false;
    guild::u32 count = 0;
    if (!RD(h, &count, 4))
        return false;
    if (count != kAemterCount)                 // *(_DWORD*)v5 == 37 required
        return false;                          // (error sprintf is a dead scratch)
    guild::u8* r = holderBase;                 // v2 = byte_B59848
    for (guild::u32 i = 0; i < kAemterCount; ++i, r += kAemterStride) {
        if (!RD(h, r + 0, 1) || !RD(h, r + 4, 4) || !RD(h, r + 8, 1)
            || !RD(h, r + 12, 4) || !RD(h, r + 16, 1) || !RD(h, r + 20, 4))
            return false;
    }
    return true;
}

// ===========================================================================
// gilde.exe 0x5a4938 — VIBE_Save_WriteGameStateHeader (preamble + records).
// See the header for the pointer->id degeneration notes.
// ===========================================================================
bool SaveWriteGameStateSection(VfsHandle* h, const GameStateHeaderPreamble& p,
                               const guild::u8* personBase) {
    if (!h || !personBase)
        return false;
    guild::u32 live = 0;                           // v12: re-count live slots
    for (guild::u32 o = 0; o != kPersScanBytesExact; o += kPersStride) {
        guild::i16 marker;
        std::memcpy(&marker, personBase + o, 2);
        if (marker != (guild::i16)-1)
            ++live;
    }
    if (!SaveWriteGameStateHeaderPreamble(h, p, live))
        return false;
    for (guild::u32 o = 0; o != kPersScanBytesExact; o += kPersStride) {
        const guild::u8* rec = personBase + o;
        guild::i16 marker;
        std::memcpy(&marker, rec, 2);
        if (marker == (guild::i16)-1)
            continue;                              // free slot
        PersonSceneLinks links{};
        std::memcpy(&links.idAt91, rec + 364, 4);
        std::memcpy(&links.idAt92, rec + 368, 4);
        std::memcpy(&links.idAt95, rec + 380, 4);
        std::memcpy(&links.idAt97, rec + 388, 4);
        if (!SaveWritePersonSceneRecord(h, rec, links))
            return false;
    }
    return true;
}

// ===========================================================================
// gilde.exe 0x5a348c — VIBE_Save_WriteGameFile, PARTIAL path.
// ===========================================================================
bool SaveWriteGameFilePartial(VfsHandle* h, const PartialSaveEnv& env) {
    if (!h || !env.tileBase || !env.objBase || !env.counterBase
        || !env.personBase || !env.slotBase || !env.cityInfoBase
        || !env.officeBase)
        return false;

    // 1. header + thumbnail (forces + stamps version 0x10045 — kSaveVersionWriter).
    if (!SaveWriteScenarioBlock(h, env.header, env.thumbnail))
        return false;
    const guild::u32 version = SaveVersionGet();   // == 0x10045 after the header

    // 2. scalar block (the 12/13 fields @0x5a3501.. in the exact order).
    if (!SaveWriteScalarBlock(h, env.scalars))
        return false;

    // 3. map-tile / scene-node index table @0x5a3fa4.
    if (!SaveWriteMapTileTable(h, env.tileBase))
        return false;

    // 4. object / building array + trailing extra-96 table @0x5a4134.
    if (!SaveWritePersonTable(h, env.objBase, env.extra96Base, env.extra96Count))
        return false;

    // 5. building / counter table @0x5a45bc (write gates at `version`; at
    //    0x10045 every gate is taken — byte-identical to the original writer).
    if (!SaveWriteBuildingTable(h, env.counterBase, nullptr, version))
        return false;

    // 6. person / scene records @0x5a4938: preamble + per-live-slot body.
    if (!SaveWriteGameStateSection(h, env.preamble, env.personBase))
        return false;

    // 7. building-slot tables @0x5a5c1c (5 city-slot tables + 4 city-info).
    if (!SaveWriteBuildingSlotTables(h, env.slotBase, env.cityInfoBase, version))
        return false;

    // 8. Amt office-holder table @0x483198.
    if (!AmtSaveAemter(h, env.officeBase))
        return false;

    // 9. VIBE_Save_RelinkPersonExtraData @0x5a3f14 (scene-object sidecar via
    //    VIBE_WorldIo_SaveSceneObjects @0x5e65b8) writes NOTHING into this
    //    stream; deferred (universe/scene-coupled — see the module report).
    // 10. (flags & 2) -> partial: done.
    return true;
}

} // namespace guild::io
