// court_council2 — candidate-collection / category tally / candidate rating /
// court-trial verdict scoring. Faithful 1:1 port of the gilde.exe control flow.
// Cross-cluster leaves are routed through PersonRelCtx (the candidate scanners,
// shared with sim/person_relations) and CourtCouncilHooks (the court scorer +
// rating bars). The record-walk / scoring / sort / verdict logic is verbatim.
#include "world/court_council2.h"

#include "sim/entity.h"          // g_persons, kPersonCapacity, PersonFindRecordById
#include "sim/types.h"           // Person, kPersonStride
#include "sim/building_type.h"   // BuildingType_MapActionToCategory
#include "world/office.h"        // OfficeCollectSuccessorCandidates, OfficeDefBookCat

#include <cstring>

namespace guild::world {

using guild::sim::g_persons;
using guild::sim::kPersonCapacity;
using guild::sim::Person;
using guild::sim::PersonRelEntry;
using guild::sim::PersonResolveStatusFlags;
using guild::sim::GetPersonRelCtx;

// ===========================================================================
// Recovered scalar constants (exact bytes from gilde.exe .rdata / immediates).
//   flt_61A670 = 16.0   (0x41800000)
//   flt_61A66C = 0.06666667 (0x3D888889 == 1/15)
//   favor cutoff CollectNearest: SLODWORD 1107558400 == 33.0f
//   rating threshold EvaluateCourtTrial: 1065353216 == 1.0f (compared as int bits)
//   v44[c+7] float init: -1082130432 == -1.0f
//   unk_744180 used as a bitmask immediate: 0x744180 (def-kind participation set)
// ===========================================================================
static constexpr float kTrialScale16   = 16.0f;        // flt_61A670
static constexpr float kTrialScaleInv15 = 0.06666667f; // flt_61A66C (1/15)
static constexpr int   kFavorCutoffBits = 1107558400;  // 33.0f as signed int bits
static constexpr int   kRatingOneBits   = 1065353216;  // 1.0f as signed int bits
static constexpr float kRatingNegOne    = -1.0f;       // -1082130432
static constexpr unsigned kKindMask     = 0x744180u;   // &unk_744180 bit set

// Person record raw byte reads the originals fold into global symbols.
static inline u8 PByte(const Person* p, int off) {
    return reinterpret_cast<const u8*>(p)[off];
}
static inline i32 PDword(const Person* p, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + off, sizeof v); return v;
}

// PersonRelCtx bridges (favorability / eligibility) — same plumbing the sibling
// collectors in sim/person_relations.cpp use.
static float Favor(int a, int b, int mode) {
    const auto& c = GetPersonRelCtx();
    return c.personFavorability ? c.personFavorability(a, b, mode) : 0.0f;
}
static int Eligible(u16 ref, u16 cand, int filter) {
    const auto& c = GetPersonRelCtx();
    return c.evaluateEligibility ? c.evaluateEligibility(ref, cand, filter) : 0;
}

// Truncate-to-int (gilde.exe VIBE_Coord_ConvertX == std::trunc on the FPU).
static inline int Trunc(double x) {
    return static_cast<int>(x < 0 ? -(double)(unsigned long long)(-x)
                                  :  (double)(unsigned long long)(x));
}

// ===========================================================================
// Module-owned office category histogram (gilde.exe dword_B59820, 37 dwords).
// ===========================================================================
static int g_categoryCounts[256];  // dword_B59820 (only [0..36] used; sized for u8 idx)

// ===========================================================================
// gilde.exe 0x47fdfc — VIBE_Office_TallyCategoryCounts  (__thiscall, ecx = out).
// ===========================================================================
// WAVE-16 (MCP) — resolves the wave-12 "++out[bucket] could overrun a 40-int
// caller buffer" flag. The decompile shows the tally ALWAYS increments the fixed
// module global dword_B59820 (g_categoryCounts), NOT the caller pointer: the loop
// body is `++dword_B59820[(u8)BYTE2(*v5)]`. The `this`/eax argument is only used
// by `VIBE_Light_SetGrayColorThunk(0, 40, this)`, which zeroes 40 dwords AT THE
// CALLER BUFFER (it is a separate scratch region from the tally global). The
// function then returns dword_B59820. The bucket index is a full u8 (0..255), so
// the histogram global must hold 256 entries (g_categoryCounts[256] — correct).
// This function has NO xrefs in the binary (dead/indirect), so it is not on the
// live call tree; reconstructed verbatim for completeness.
int* OfficeTallyCategoryCounts(int* out) {
    std::memset(out, 0, 40 * sizeof(int));      // SetGrayColorThunk(0,40,this) -> caller buf
    for (int i = 0; i < kPersonCapacity; ++i) {
        const Person* rec = &g_persons[i];
        u8 kind = PByte(rec, 2);                 // v1[2]
        if (kind >= 2 && kind <= 7) {
            u8 office = PByte(rec, 359);         // v1[359]
            if (office) {
                u8 bucket = (office < 0x25)
                              ? OfficeDefBookCat(office)   // BYTE2(dword_62EC8E[3*office])
                              : OfficeDefBookCat(0);       // dword_62EC8E + 2
                ++g_categoryCounts[bucket];      // ++dword_B59820[(u8)bucket]  (the GLOBAL)
            } else {
                ++g_categoryCounts[0];           // ++dword_B59820[0]
            }
        }
    }
    return g_categoryCounts;                      // return dword_B59820
}

int* OfficeTallyCategoryCounts() { return OfficeTallyCategoryCounts(g_categoryCounts); }

// ===========================================================================
// gilde.exe 0x555d5c — VIBE_Office_CollectCategoryMatchedCandidates.
//   The original resolves the rank list via OfficeCollectCategoryRankList(refRec,
//   4, &v15); we accept the resolved list (codes + length) so the scan is
//   independent of the office-def table wiring. refRec's +358 byte gates the call.
// ===========================================================================
u32 OfficeCollectCategoryMatchedCandidates(Person* refRec, u32 capacity,
                                           int filter, PersonRelEntry* out,
                                           const u8* rankList, int rankListLen) {
    if (!PByte(refRec, 358))            // if (!*((_BYTE*)a1 + 358)) return 0
        return 0;
    if (rankListLen <= 0)               // result (rank count) == 0 -> return result
        return 0;

    u32 written = 0;                    // v4
    if (capacity) {
        // For each rank-list code (v20), scan all 768 person slots (v21) for a
        // live person whose +360 office byte == the code, fill the entry.
        for (int v20 = 0; v20 < rankListLen && written < capacity; ++v20) {
            for (int v21 = 0; v21 < kPersonCapacity; ++v21) {
                Person* p = &g_persons[v21];
                if (static_cast<u8>(rankList[v20]) == PByte(p, 360)) {
                    PersonRelEntry* e = &out[written];
                    e->person   = p;                         // *v9 = v8
                    e->relClass = static_cast<u8>(rankList[v20]); // v9[13]... (list code)
                    PersonResolveStatusFlags(e);
                    i32 candMarker = static_cast<u16>(e->person->marker); // (u16)**v9
                    ++written;                               // ++v4 (pre eligibility)
                    e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                              static_cast<u16>(candMarker), filter);
                    if (written >= capacity)
                        break;
                }
            }
        }
        return written;
    }

    // capacity == 0: COUNT live persons whose +360 office matches any list code.
    for (int i = 0; i < kPersonCapacity; ++i) {
        Person* p = &g_persons[i];
        u8 office = PByte(p, 360);
        if (office) {
            for (int j = 0; j < rankListLen; ++j) {
                if (static_cast<u8>(rankList[j]) == office) {
                    ++written;          // ++v4
                    break;              // matched this person; next person
                }
            }
        }
    }
    return written;
}

// ===========================================================================
// gilde.exe 0x555ba8 — VIBE_Office_CollectGuildSuccessorCandidates.
//   v24 = OfficeCollectSuccessorCandidates(office, 6, &v19) (office-pool ids),
//   then scan for up to 3 OTHER same-office live persons (v19[6..8]).
// ===========================================================================
u32 OfficeCollectGuildSuccessorCandidates(Person* refRec, u32 capacity,
                                          int filter, PersonRelEntry* out,
                                          const OfficePerson* pool, int poolCount) {
    u8 office = PByte(refRec, 360);
    if (!office)                        // if (!*((_BYTE*)a1 + 360)) return 0
        return 0;

    // OfficeCollectSuccessorCandidates fills up to 6 successor ids from the office
    // pool (the reconstructed callee filters via IsNextRankInCategory).
    i32 poolIds[6];
    int v24 = OfficeCollectSuccessorCandidates(
        office, pool, poolCount, /*max*/6,
        poolIds, /*outCapacity*/6);

    // Scan the 768 person slots for up to 3 OTHER live persons sharing office.
    Person* scanned[3] = {nullptr, nullptr, nullptr};   // v19[6..8]
    int v20 = 0;                                         // scan count
    for (int i = 0; i < kPersonCapacity && v20 < 3; ++i) {
        Person* p = &g_persons[i];
        if (p->marker != -1 &&
            static_cast<u16>(p->marker) != static_cast<u16>(refRec->marker) &&
            PByte(p, 360) == office) {
            scanned[v20] = p;                            // v19[v8+6] = v6
            ++v20;
        }
    }

    if (!capacity)                                       // if (!v26) return v20 + v24
        return static_cast<u32>(v20 + v24);

    u32 written = 0;                                     // v10
    // First fill from the office-pool successor ids (relClass 0).
    u32 cap = (capacity >= static_cast<u32>(v24)) ? static_cast<u32>(v24) : capacity;
    for (u32 k = 0; k < cap; ++k) {
        PersonRelEntry* e = &out[written];
        e->person = guild::sim::PersonFindRecordById(poolIds[k]); // v19[v25/4]
        e->relClass = 0;                                 // v11[11] = 0
        PersonResolveStatusFlags(e);
        if (e->person)
            e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                      static_cast<u16>(e->person->marker), filter);
        ++written;
    }
    // Then fill from the scanned same-office persons (relClass 1).
    if (v20 > 0) {
        for (int k = 0; k < v20 && written < capacity; ++k) {
            PersonRelEntry* e = &out[written];
            e->person = scanned[k];                      // v19[6]+v27
            e->relClass = 1;                             // v14[11] = 1
            PersonResolveStatusFlags(e);
            i32 candMarker = e->person ? static_cast<u16>(e->person->marker) : 0;
            ++written;
            e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                      static_cast<u16>(candMarker), filter);
        }
    }
    return written;
}

// ===========================================================================
// gilde.exe 0x555378 — VIBE_Office_CollectNearestCandidatesByDistance.
//   Insertion sort keeping the `capacity` LOWEST-favor candidates (favor stored
//   truncated in entry.relClass; sentinel pre-fill 100). Same-faction (+524 ==
//   refId at +4) scores 0. Favor cutoff <= 33.0f (compared as signed int bits).
// ===========================================================================
int OfficeCollectNearestCandidatesByDistance(Person* refRec, u32 capacity,
                                             int filter, PersonRelEntry* out) {
    int v5 = 0;
    const i32 refId = PDword(refRec, 4);             // *((_DWORD*)a1 + 1)
    const u16 refMarker = static_cast<u16>(refRec->marker);

    if (capacity) {
        std::memset(out, 0, sizeof(PersonRelEntry) * capacity); // SetGrayColorThunk(0,56*a2)
        for (u32 i = 0; i < capacity; ++i)
            out[i].relClass = 100;                   // pre-fill worst score 100

        for (int i = 0; i < kPersonCapacity; ++i) {
            Person* p = &g_persons[i];
            if (p == refRec || p->marker == -1)
                continue;
            u8 kind = PByte(p, 2);                   // v12[2]
            if (kind < 2 || kind > 7)
                continue;
            // v30 = same-faction ? 0 : favorability
            // VIBE_Ai_ComputePersonFavorability(*a1, *v12, 1) == (ref, candidate).
            float favor = (PDword(p, 524) == PDword(refRec, 4))
                            ? 0.0f
                            : Favor(refMarker, static_cast<u16>(p->marker), 1);
            int favorBits;
            std::memcpy(&favorBits, &favor, sizeof favorBits);
            // SLODWORD(v30) <= 33.0f bits  &&  worst kept score > v30
            if (favorBits <= kFavorCutoffBits &&
                static_cast<double>(out[capacity - 1].relClass) > favor) {
                int favorI = Trunc(favor);           // VIBE_Coord_ConvertX
                ++v5;
                // Insert at the tail, bubble toward the front while predecessor's
                // score is <= ours (descending toward front by score).
                out[capacity - 1].person   = p;      // *v16 = v17
                out[capacity - 1].relClass  = favorI; // *(v19+44)
                if (capacity != 1) {
                    u32 pos = capacity - 1;          // v18 = a2-1 (slot index)
                    while (pos > 0) {
                        PersonRelEntry& cur  = out[pos];
                        PersonRelEntry& prev = out[pos - 1];
                        if (prev.relClass <= cur.relClass)  // v20[11] <= v20[25]
                            break;                          // (while predecessor <= cur: move up)
                        // swap person + score with predecessor
                        Person* tmpP = prev.person;
                        int tmpS = prev.relClass;
                        prev.person = cur.person;
                        prev.relClass = cur.relClass;
                        cur.person = tmpP;
                        cur.relClass = tmpS;
                        --pos;
                    }
                }
            }
        }
        if (v5 >= static_cast<int>(capacity))
            v5 = static_cast<int>(capacity);
        for (int i = 0; i < v5; ++i) {
            PersonRelEntry* e = &out[i];
            PersonResolveStatusFlags(e);
            i32 candMarker = e->person ? static_cast<u16>(e->person->marker) : 0;
            e->eligibility = Eligible(refMarker, static_cast<u16>(candMarker), filter);
        }
        return v5;
    }

    // capacity == 0: COUNT favor <= 33.0f or same-faction.
    for (int i = 0; i < kPersonCapacity; ++i) {
        Person* p = &g_persons[i];
        if (p == refRec || p->marker == -1)
            continue;
        u8 kind = PByte(p, 2);
        if (kind < 2 || kind > 7)
            continue;
        float favor = Favor(refMarker, static_cast<u16>(p->marker), 1);
        int favorBits;
        std::memcpy(&favorBits, &favor, sizeof favorBits);
        if (favorBits <= kFavorCutoffBits || PDword(p, 524) == refId)
            ++v5;
    }
    return v5;
}

// ===========================================================================
// CourtCouncilHooks plumbing.
// ===========================================================================
static const CourtCouncilHooks kInertHooks{};
static const CourtCouncilHooks* g_hooks = &kInertHooks;
void SetCourtCouncilHooks(const CourtCouncilHooks* hooks) {
    g_hooks = hooks ? hooks : &kInertHooks;
}
const CourtCouncilHooks& GetCourtCouncilHooks() { return *g_hooks; }

static int HookSlotCount() {
    return g_hooks->buildingSlotCount ? g_hooks->buildingSlotCount() : 0;
}
static bool HookSlot(int i, CourtBuildingSlot* out) {
    return g_hooks->buildingSlot ? g_hooks->buildingSlot(i, out) : false;
}
static int HookAiNeeds(int category) {
    return g_hooks->aiNeedsComputeWeights ? g_hooks->aiNeedsComputeWeights(category) : 0;
}
static float HookCategoryRating(int category) {
    return g_hooks->categoryRating ? g_hooks->categoryRating(category) : 1.0f;
}
static int HookEligible(Person* accused) {
    return g_hooks->guildEligibility ? g_hooks->guildEligibility(accused) : 0;
}
static char HookSetBar(int widget, int value) {
    return g_hooks->setBarValue ? g_hooks->setBarValue(widget, value) : 1;
}
static int HookRandomMod4() {
    return g_hooks->randomMod4 ? g_hooks->randomMod4() : 0;
}

// ===========================================================================
// gilde.exe 0x556ba0 — VIBE_Office_ApplyCandidateRatingBars.
//   a1 = refPerson record (its kind byte at +2). For each of `count` 56-byte
//   slots, if the candidate is live + has a bar widget: both-holders -> flat 100,
//   else truncated favorability. Returns the last SetValueOrText low byte.
// ===========================================================================
char OfficeApplyCandidateRatingBars(Person* refPerson, u32 count, RatingSlot* entries) {
    char result = 0;
    if (!refPerson)
        return result;
    if (static_cast<u16>(refPerson->marker) == 0xFFFF)   // *a1 != 0xFFFF
        return result;
    for (u32 i = 0; i < count; ++i) {
        RatingSlot& s = entries[i];
        // *v5 && *(WORD*)*v5 != 0xFFFF && v5[2] != -1
        if (s.person && static_cast<u16>(s.person->marker) != 0xFFFF &&
            s.barWidget != -1) {
            u8 refKind  = PByte(refPerson, 2);           // *((_BYTE*)v3 + 2)
            u8 candKind = PByte(s.person, 2);            // *(_BYTE*)(*v5 + 2)
            if ((refKind == 6 || refKind == 7) &&
                (candKind == 6 || candKind == 7)) {
                result = HookSetBar(s.barWidget, 100);   // SetValueOrText(..,100,0)
            } else {
                float favor = Favor(static_cast<u16>(refPerson->marker),
                                    static_cast<u16>(s.person->marker), 1);
                int favorI = Trunc(favor);               // VIBE_Coord_ConvertX
                result = HookSetBar(s.barWidget, favorI);
            }
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4747dc — VIBE_NpcAction_EvaluateCourtTrial.
// ===========================================================================
char EvaluateCourtTrial(int a1, Person* accused, CourtVerdict* outVerdict, int a4) {
    if (outVerdict) {
        outVerdict->issued = false;
        outVerdict->category = 0;
        outVerdict->direction = 0;
    }
    // if (a1 || a4 || GetGuildEligibility(accused) != 1) return 0
    if (a1 || a4 || HookEligible(accused) != 1)
        return 0;

    const u16 accusedOwner = static_cast<u16>(accused->marker); // *a2 (the trial subject id)

    int support[7] = {0};      // v46[c]
    int total[7]   = {0};      // v45[c]
    int topSecurity[7] = {0};  // v44[c] holds the best-building security per cat

    const int n = HookSlotCount();
    for (int i = 0; i < n; ++i) {
        CourtBuildingSlot slot;
        if (!HookSlot(i, &slot))
            continue;
        if (!slot.alive)
            continue;
        if (slot.ownerWord == 0xFFFF)            // *(_WORD*)(rec+39) != 0xFFFF
            continue;
        // ((1 << def.kind) & 0x744180) != 0
        if ((kKindMask & (1u << slot.defKind)) == 0)
            continue;
        u8 cat = guild::sim::BuildingType_MapActionToCategory(slot.defKind);
        if (cat == 0 || cat > 7)
            continue;
        int c = cat - 1;                          // 0-based category bucket
        if (slot.ownerWord == accusedOwner)
            support[c] += slot.defSecurity;       // v46[c] += def+583
        total[c] += slot.defSecurity;             // v45[c] += def+583
        if (topSecurity[c]) {
            if (slot.defSecurity > topSecurity[c]) // higher security wins
                topSecurity[c] = slot.defSecurity;
        } else {
            topSecurity[c] = slot.defSecurity;
        }
    }

    int v24 = -1;   // worst-rated category (per the original's update)
    int v28 = -1;   // best-rated category
    float score[7];
    for (int c = 0; c < 7; ++c) {
        score[c] = kRatingNegOne;                 // v44[c+7] = -1.0f
        // clamp total/support to 15
        if (total[c]   >= 15) total[c]   = 15;
        if (support[c] >= 15) support[c] = 15;
        if (total[c] > 0) {
            if (HookAiNeeds(c)) {                 // VIBE_AiNeeds_ComputeWeights gate
                float rating = HookCategoryRating(c); // v43[40*c+37]
                score[c] = (static_cast<float>(total[c]) + 1.0f - static_cast<float>(support[c]))
                           / static_cast<float>(total[c])
                           * rating
                           * (kTrialScale16 - static_cast<float>(support[c]))
                           * kTrialScaleInv15;
                if (v24 == -1) {
                    v24 = c;
                } else if (static_cast<double>(score[v24]) > static_cast<double>(score[c])) {
                    v24 = 1;                      // verbatim: original sets v24 = 1
                }
                if (v28 == -1 ||
                    static_cast<double>(score[v28]) < static_cast<double>(score[c]))
                    v28 = c;
            }
        }
    }

    if (v24 == -1 || v28 == -1 || v24 == v28)
        return 0;

    // Category ratings read as signed int bits vs 1.0f bits (v43[40*x+37]).
    auto ratingBits = [&](int c) { return HookCategoryRating(c); };
    int v24Bits, v28Bits;
    { float r24 = ratingBits(v24); std::memcpy(&v24Bits, &r24, sizeof v24Bits);
      float r28 = ratingBits(v28); std::memcpy(&v28Bits, &r28, sizeof v28Bits); }

    int v34 = 0;
    // if (rating[v24] > 1.0f || !support[v24])
    if (v24Bits > kRatingOneBits || !support[v24]) {
        // if (rating[v28] < 1.0f || 2*support[v28]+1 >= total[v28]) return 0
        if (v28Bits < kRatingOneBits || 2 * support[v28] + 1 >= total[v28])
            return 0;
        v34 = -1;
    }
    // if (rating[v28] < 1.0f || 2*support[v28]+1 >= total[v28])
    if (v28Bits < kRatingOneBits || 2 * support[v28] + 1 >= total[v28]) {
        // if (rating[v24] <= 1.0f && support[v24]) { v34 = 1; goto LABEL_37 }
        if (v24Bits <= kRatingOneBits && support[v24]) {
            v34 = 1;
            goto emit;
        }
        return 0;
    }
    if (!v34)
        v34 = HookRandomMod4() - 1;               // VIBE_Math_RandomModulo(4) - 1
    if (v34 == -1 || v34 == 1)
        goto emit;
    return 0;

emit:
    if (outVerdict) {
        outVerdict->issued = true;
        outVerdict->category = v24 + 1;           // *(DWORD*)(v47+4) = v24 + 1
        outVerdict->direction = v34;              // *(DWORD*)(v47+16) = v34
    }
    return 54;
}

} // namespace guild::world
