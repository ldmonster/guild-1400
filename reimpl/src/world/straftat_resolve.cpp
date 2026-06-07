#include "world/straftat_resolve.h"

#include "world/crime.h"
#include "sim/entity.h"   // g_persons (word_12CE910), kPersonCapacity
#include "sim/person.h"   // PersonGetByte / PersonGetDword / PersonSetDword

// Faithful 1:1 port of gilde.exe 0x4c36ec / 0x4c39a4. The crime/evidence parallel
// globals (dword_11BC760 id, dword_11BC785 provenState, dword_11BC776 perp;
// dword_11C2160 evidence-owner, dword_11C2164 evidence-crimeId) are the same
// g_crimeTable / g_evidence* tables modeled in crime.h. The wanted-flag sweep
// walks the live Person array (g_persons, stride 536), reading the kind byte at
// +0x02 (byte_12CE912) and clearing bits in the dword at +0x1E4 (dword_12CEAF4).

namespace guild::world {

// Person-record field offsets the originals reach by raw byte offset (folded into
// the per-field globals byte_12CE912 / dword_12CEAF4). Local to this module since
// the canonical PersonField enum (sim/types.h) does not yet name +0x1E4.
namespace {
constexpr int kPfKindByte    = 0x02; // byte_12CE912 — kind/class byte
constexpr int kPfWantedFlags  = 0x1E4; // dword_12CEAF4 — office wanted-flag bitfield
} // namespace

// ===========================================================================
// gilde.exe 0x4c36ec — VIBE_Straftat_ClearWantedFlagOnNpcs
//   (byte-identical twin: 0x4c3838 VIBE_Straftat_ClearWantedFlagOnNpcs2).
// ===========================================================================
int StraftatClearWantedFlagOnNpcs(i32 mask) {
    int touched = 0;
    // Original: for (off = 0; off != 0x64800; off += 0x218)  (768 records, stride
    // 536). kind byte at record+2; wanted dword at record+0x1E4.
    for (int i = 0; i < sim::kPersonCapacity; ++i) {
        sim::Person& p = sim::g_persons[i];
        u8 kind = sim::PersonGetByte(&p, kPfKindByte); // byte_12CE912[off]
        if (kind == 6 || kind == 7) {
            i32 flags = sim::PersonGetDword(&p, kPfWantedFlags); // dword_12CEAF4[off]
            sim::PersonSetDword(&p, kPfWantedFlags, flags & ~mask); // &= ~mask
            ++touched;
        }
    }
    return touched;
}

// ===========================================================================
// gilde.exe 0x4c39a4 — VIBE_Straftat_UpdateMatchingRecords.
// ===========================================================================
int StraftatUpdateMatchingRecords(i32 ownerKey, i32 perpMatch, int mode,
                                  i32 newState) {
    int modified = 0; // esi

    if (mode) {
        // Evidence-driven pass. Walk the 2048 evidence pairs (ebx steps 8 bytes ==
        // 2 dwords == one pair); g_evidence* index 2*k mirrors dword_*[8*k].
        for (int k = 0; k < kEvidenceDwords; k += 2) {
            if (g_evidenceOwner[k] != ownerKey) // dword_11C2160[ebx] == a1 ?
                continue;
            // Find the crime record whose id == this pair's crime-id. The original
            // starts the scan at record index 0 (eax = a1 ^ owner == 0, since
            // owner == ownerKey here) and steps by 45 until id matches or the
            // table (512 records) is exhausted.
            i32 wantId = g_evidenceCrimeId[k]; // dword_11C2164[ebx]
            int idx = -1;
            for (int r = 0; r < kCrimeCount; ++r) {
                if (g_crimeTable[r].id == wantId) {
                    idx = r;
                    break;
                }
            }
            if (idx < 0)
                continue; // scan ran off the end -> no matching record
            // crime.provenState == 1 && perpMatch == crime.perpetrator
            CrimeRecord& c = g_crimeTable[idx];
            if (c.provenState == 1 && perpMatch == c.perpetrator) {
                ++modified;                 // esi += 1
                c.provenState = newState;   // dword_11BC785[eax] = edi
            }
        }
    } else {
        // State-rewrite pass: every record whose provenState == newState is reset
        // to 1 and counted.
        for (int r = 0; r < kCrimeCount; ++r) {
            if (g_crimeTable[r].provenState == newState) { // edi == dword_11BC785[ebx]
                ++modified;                       // esi += 1
                g_crimeTable[r].provenState = 1;  // dword_11BC785[ebx] = 1
            }
        }
    }
    return modified;
}

} // namespace guild::world
