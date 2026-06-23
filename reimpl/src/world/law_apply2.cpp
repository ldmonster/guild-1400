// Office promotion-list builder + remaining self-contained office rule leaves.
// Faithful 1:1 port from gilde.exe. See law_apply2.h for the layout notes.
//
// Holder-table field access (gilde.exe globals, 24-byte stride; slot index k):
//   byte_B59850[24*k]  office type   -> g_officeHolders[k].type
//   dword_B59854[6*k]  rank-level    -> g_officeHolders[k].rank   (the original
//                       indexes dword_B59854[v11/4] with v11 the *byte* offset,
//                       so v11/4 == 6*k; same dword)
//   byte_B59858[24*k]  state         -> g_officeHolders[k].state
//   dword_B5984C[6*k]  city/owner    -> g_officeHolders[k].city
// byte_62EC92[12*type] is OfficeDefBookCat(type) (record byte +2 == bookCat).
#include "world/law_apply2.h"

#include <cstring>
#include <vector>

#include "world/office.h"

namespace guild::world {

// The empty-slot cost seed: the original stores the int literal -1027080192 into
// the slot's cost dword, whose float reinterpretation is the bit pattern below.
// (0xC2C80000 == -100.0f.) Seeded slots therefore never beat a real candidate in
// the (category, cost) comparison and stay at the unfilled tail of the list.
static float EmptyCostSeed() {
    const i32 bits = -1027080192;  // 0xC2C80000
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;  // -100.0f
}

// Shared insertion-sort core for BuildPromotionList / ...Filtered. `rankInside`
// selects the filtered variant's behavior: when true the rank>=4 holders set
// *outRankBlocked instead of gating the holder out. Mirrors the two originals,
// which are byte-for-byte identical except for that gate placement.
static int BuildPromotionImpl(const OfficePerson& person, int maxCount,
                              PromotionEntry* out, int* outRankBlocked,
                              bool rankInside) {
    int rankBlocked = 0;  // v34 (filtered) — 1 if a rank>=4 holder was skipped
    int count = 0;        // v33 / v36

    // gilde.exe 0x47f434 / 0x47f1ea: the `a2 <= 0` early return happens BEFORE the
    // `if (v29) *v29 = v34` write, so *outRankBlocked is left UNTOUCHED here.
    if (maxCount <= 0) {
        return 0;
    }

    // Zero then seed each of the `maxCount` records: type=0, category=0,
    // cost = -100.0f. (The original's split memset is a compiler artifact of the
    // same full zero-fill; the seed loop below sets the meaningful fields.)
    std::memset(out, 0, sizeof(PromotionEntry) * static_cast<size_t>(maxCount));
    const float emptyCost = EmptyCostSeed();
    for (int i = maxCount - 1; i >= 0; --i) {
        out[i].cost     = emptyCost;  // dword[1] = -1027080192
        out[i].type     = 0;          // byte[0]
        out[i].category = 0;          // byte[8]
    }

    const int last = maxCount - 1;  // v35 / v38

    for (int k = 0; k < kOfficeHolderCount; ++k) {  // v11: 0,24,..,696
        const OfficeHolder& h = g_officeHolders[k];

        if (h.state != 3) continue;            // byte_B59858[v11] == 3

        float cost = 0.0f;                     // v27 / v28
        if (rankInside) {
            // Filtered: CanPromoteRank first, then the rank gate routes to the flag.
            if (!OfficeCanPromoteRank(person, h.type, &cost)) continue;
            if (h.rank >= 4) {                 // dword_B59854[v12/4] >= 4
                rankBlocked = 1;
                continue;
            }
        } else {
            if (h.rank >= 4) continue;         // dword_B59854[v11/4] < 4
            if (!OfficeCanPromoteRank(person, h.type, &cost)) continue;
        }

        const u8 newType = h.type;             // byte_B59850[v11]
        const u8 newCat  = OfficeDefBookCat(newType);  // byte_62EC92[12*type]

        // Dedup: scan all slots; skip if `newType` already present (matches the
        // record's +0 type byte). v13 in the original.
        bool dup = false;                      // v13 / v14
        for (int j = last; j >= 0; --j) {      // walks &out[v31] down by 12
            if (dup) break;
            dup = (out[j].type == newType);    // *v14 == byte_B59850[v11]
        }
        if (dup) continue;

        // Find the insertion index. Scan from the end; stop at the first slot
        // whose (category, cost) is strictly greater than the new entry's:
        //   break when  slotCat >= newCat && (slotCat != newCat || slotCost > newCost)
        int pos = last;       // v15 / v16-index
        bool found = false;   // v16 / v17
        for (int j = last; j >= 0; --j) {      // v17 = &out[v30] down by 12
            const u8    slotCat  = out[j].category;  // *((BYTE*)v17 + 8)
            const u8    cmpCat   = newCat;           // byte_62EC92[..]
            const float slotCost = out[j].cost;      // v17[1]
            if (slotCat >= cmpCat &&
                (slotCat != cmpCat || slotCost > static_cast<double>(cost))) {
                pos   = j;
                found = true;
                break;
            }
            pos = j - 1;  // mirrors --v15 (after the break it holds the index)
        }

        // LABEL_25: decide whether to insert. The original's fused condition:
        //   ( pos == -1 && front-slot(out[0]) (cat,cost) <= new ) || found
        bool doInsert;
        if (!found) {
            // pos walked to -1 (scan exhausted). The front-slot tie/less test:
            //   out[0].cat < newCat || (out[0].cat == newCat && out[0].cost <= newCost)
            if (pos == -1) {
                pos = 0;  // v15 = 0
                const u8    frontCat  = out[0].category;  // v34[8]
                const u8    cmpCat    = newCat;
                const float frontCost = out[0].cost;      // *((float*)v34 + 1)
                doInsert = (frontCat < cmpCat) ||
                           (frontCat == cmpCat &&
                            frontCost <= static_cast<double>(cost));
            } else {
                doInsert = false;
            }
        } else {
            doInsert = true;
        }

        if (doInsert) {
            ++count;
            // Shift slots [pos .. last-1] down by one (toward the tail), opening
            // the slot at `pos`. Mirrors the v22>v15 copy loop exactly.
            if (pos < last) {
                for (int j = last; j > pos; --j) {  // v22 from v35 down to >v15
                    out[j].category = out[j - 1].category;  // +20 -> +32
                    out[j].cost     = out[j - 1].cost;      // float +4 -> +7
                    out[j].type     = out[j - 1].type;      // +12 -> +24
                }
            }
            out[pos].category = newCat;   // byte +8
            out[pos].cost     = cost;     // float +4
            out[pos].type     = newType;  // byte +0
        }
    }

    if (rankInside && outRankBlocked) *outRankBlocked = rankBlocked;

    if (count >= maxCount) return maxCount;  // capped
    return count;
}

// gilde.exe 0x47f1cc — VIBE_Office_BuildPromotionList.
int OfficeBuildPromotionList(const OfficePerson& person, int maxCount,
                             PromotionEntry* out) {
    return BuildPromotionImpl(person, maxCount, out, nullptr, /*rankInside=*/false);
}

// gilde.exe 0x47f410 — VIBE_Office_BuildPromotionListFiltered.
int OfficeBuildPromotionListFiltered(const OfficePerson& person, int maxCount,
                                     int* outRankBlocked, PromotionEntry* out) {
    return BuildPromotionImpl(person, maxCount, out, outRankBlocked,
                              /*rankInside=*/true);
}

// gilde.exe 0x47dfec — VIBE_Office_GetHolderEntryByCity.
// Scans the holder table for a slot whose +4 owner equals the person's ownerId.
// The original walks dword_B5984C with v8 stepping by 6 dwords (== 24 bytes ==
// one slot), bound v8 < 180 (== 30 slots * 6 dwords). On a hit it copies the
// 24-byte holder record and the decoded OfficeDef of that slot's type.
int OfficeGetHolderEntryByCity(const OfficePerson& person, OfficeDef* defOut,
                               OfficeHolder* holderOut) {
    // Gate: valid record (a1 != null && marker != 0xFFFF) and holds an office
    // (+358 != 0). OfficePerson::valid models the non-null + non-0xFFFF marker.
    if (!person.valid || person.officeType == 0) {
        return 0;
    }
    const i32 ownerId = person.ownerId;  // *(a1 + 4)

    int hit = -1;
    if (ownerId == g_officeHolders[0].city) {  // v7 == dword_B5984C[0]
        hit = 0;
    } else {
        for (int idx = 1; idx < kOfficeHolderCount; ++idx) {  // v8 += 6 (one slot)
            if (ownerId == g_officeHolders[idx].city) {       // == dword_B5984C[v8]
                hit = idx;
                break;
            }
        }
    }
    if (hit < 0) return 0;

    std::memcpy(holderOut, &g_officeHolders[hit], sizeof(OfficeHolder));
    // Decode the def block for the slot's type (def base + 2 + 12*type).
    OfficeGetDefinition(g_officeHolders[hit].type, defOut);
    return 1;
}

// gilde.exe 0x46b8a8 — VIBE_Office_EvalApplyForCandidacy.
// if (msg.opcode == 10 && ApplyForCandidacy(...)) return 10; else reject-stub(0).
u8 OfficeEvalApplyForCandidacy(u8 opcode, bool applyOk) {
    if (opcode == 10 && applyOk) {
        return 10;
    }
    return 0;  // VIBE_Interaction_EvalRejectStub() == 0
}

// ---------------------------------------------------------------------------
// TryPromoteCharacter command hook (models VIBE_Command_RequestBuildOp92).
// ---------------------------------------------------------------------------
namespace {
int DefaultPromoteHook(const OfficePromoteCommand&, void*) { return 0; }
OfficePromoteCommandHook g_promoteHook = &DefaultPromoteHook;
void* g_promoteCtx = nullptr;
std::vector<OfficePromoteCommand> g_promoteLog;
}  // namespace

void OfficeSetPromoteCommandHook(OfficePromoteCommandHook hook, void* ctx) {
    g_promoteHook = hook ? hook : &DefaultPromoteHook;
    g_promoteCtx = ctx;
}
void OfficePromoteCommandLogReset() { g_promoteLog.clear(); }
const OfficePromoteCommand* OfficePromoteCommandLog(int* outCount) {
    if (outCount) *outCount = static_cast<int>(g_promoteLog.size());
    return g_promoteLog.empty() ? nullptr : g_promoteLog.data();
}

// gilde.exe 0x47ebd4 — VIBE_Office_TryPromoteCharacter.
int OfficeTryPromoteCharacter(const OfficePerson& p,
                              const OfficePerson& fromCity,
                              const OfficePerson& toCity) {
    // a1/a2/a3 all non-zero (modeled by all three person views being valid).
    if (!p.valid || !fromCity.valid || !toCity.valid) return -1;

    OfficeDef    defFrom{}, defTo{};
    OfficeHolder holdFrom{}, holdTo{};
    if (!OfficeGetHolderEntryByCity(fromCity, &defFrom, &holdFrom)) return -1;
    if (!OfficeGetHolderEntryByCity(toCity, &defTo, &holdTo)) return -1;

    // The original writes the decoded def block starting at &v8+2, so:
    //   v9   (def block byte +2 == word0 byte +2)  == bookCat
    //   v8>>24 (v8 byte +3 == def block byte +1 == word0 byte +1) == reqCode
    // Both must match between the two resolved slots.
    const u8 catFrom = static_cast<u8>(defFrom.word0 >> 16);  // bookCat (byte +2)
    const u8 catTo   = static_cast<u8>(defTo.word0 >> 16);
    if (catFrom != catTo) return -1;                          // v9 != v11

    const u8 reqFrom = static_cast<u8>(defFrom.word0 >> 8);   // reqCode (byte +1)
    const u8 reqTo   = static_cast<u8>(defTo.word0 >> 8);
    if (reqFrom != reqTo) return -1;                          // v8>>24 != v10>>24

    OfficePromoteCommand cmd{};
    cmd.personId = p.ownerId;       // *(a1 + 4)
    cmd.fromType = holdFrom.holder; // v13 = v7[0] == holder char-id (+0) of from slot
    cmd.toType   = holdTo.holder;   // v14 = v6[0] == holder char-id (+0) of to slot
    cmd.tag      = 6;               // v15 = 6
    g_promoteLog.push_back(cmd);
    return g_promoteHook(cmd, g_promoteCtx);
}

} // namespace guild::world
