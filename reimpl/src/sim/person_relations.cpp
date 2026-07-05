// person_relations — person social-relation collectors + status flag resolution.
// Faithful 1:1 port of the gilde.exe control flow. Cross-cluster leaves (He pool
// scan, candidate eligibility, AI favorability, building output) are routed
// through PersonRelCtx; the record-walk / scoring / sort logic is verbatim.
#include "sim/person_relations.h"
#include "sim/entity.h"        // g_persons, g_personIds, PersonFindRecordById

#include <cstring>

namespace guild::sim {

// ===========================================================================
// Recovered scalar constants (exact bytes from gilde.exe .rdata).
//   flt_6246EC = 100.0   flt_6246F0 = 350.0   flt_6246F4 = 650.0
//   SLODWORD(75.0) == 1117126656 (favorability cutoff in CollectTopByScore).
// ===========================================================================
constexpr float kOutputLow  = 100.0f;  // flt_6246EC (0x42C80000)
constexpr float kOutputMid  = 350.0f;  // flt_6246F0 (0x43AF0000)
constexpr float kOutputHigh = 650.0f;  // flt_6246F4 (0x44228000)
constexpr float kFavorCutoff = 75.0f;  // SLODWORD 1117126656

// Person record byte-offset reads used here that have no named PersonField yet.
// The originals fold these into raw `*(T*)(rec + off)` accesses.
static inline u8  PByte(const Person* p, int off) {
    return reinterpret_cast<const u8*>(p)[off];
}
static inline i32 PDword(const Person* p, int off) {
    i32 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + off, sizeof v); return v;
}
static inline i16 PWord(const Person* p, int off) {
    i16 v; std::memcpy(&v, reinterpret_cast<const u8*>(p) + off, sizeof v); return v;
}

// ===========================================================================
// Context plumbing.
// ===========================================================================
static const PersonRelCtx kInertCtx{};
static const PersonRelCtx* g_ctx = &kInertCtx;
void SetPersonRelCtx(const PersonRelCtx* ctx) { g_ctx = ctx ? ctx : &kInertCtx; }
const PersonRelCtx& GetPersonRelCtx() { return *g_ctx; }

static HeRecord* HeFirst(int kind) {
    return g_ctx->heFindFirst ? g_ctx->heFindFirst(kind) : nullptr;
}
static HeRecord* HeNext() {
    return g_ctx->heFindNext ? g_ctx->heFindNext() : nullptr;
}
static int HeCount(i32 id) {
    return g_ctx->heCountMatchingEntities ? g_ctx->heCountMatchingEntities(id) : 0;
}
static float BuildOutput(Person* p) {
    return g_ctx->buildingCurrentOutput ? g_ctx->buildingCurrentOutput(p) : 0.0f;
}
static float Favor(int a, int b, int mode) {
    return g_ctx->personFavorability ? g_ctx->personFavorability(a, b, mode) : 0.0f;
}
static int Eligible(u16 ref, u16 cand, int filter) {
    return g_ctx->evaluateEligibility ? g_ctx->evaluateEligibility(ref, cand, filter) : 0;
}

// Clear `count` entries (the original's VIBE_Light_SetGrayColorThunk(0,56*count,
// out) zeroes the whole 56-byte-stride region; we clear sizeof(PersonRelEntry)
// per entry — see PersonRelEntry note on the 32-vs-64-bit stride difference).
static void ClearEntries(PersonRelEntry* out, u32 count) {
    std::memset(out, 0, sizeof(PersonRelEntry) * count);
}

// ===========================================================================
// gilde.exe 0x553ce8 — VIBE_Person_ResolveStatusFlags.
// ---------------------------------------------------------------------------
//   a1 = entry (_DWORD*). *a1 == person record ptr. The id column the originals
//   key against is dword_12CE914[134*word_63CC5C] == g_personIds[playerIdx].
// ===========================================================================
int PersonResolveStatusFlags(PersonRelEntry* entry) {
    Person* p = entry->person;
    if (!p)                                   // if (!*a1) return 0
        return 0;

    const u16 playerIdx = g_ctx->currentPlayerIndex;       // word_63CC5C
    const i32 playerId  = g_personIds[playerIdx];          // dword_12CE914[134*idx]
    const i32 personId  = PDword(p, 4);                    // *(DWORD*)(*a1+4)

    // a1[7] = (*(BYTE*)(*a1+12) == 0) + 1596   (flagA)
    entry->flagA = (PByte(p, 12) == 0) + 1596;
    entry->flagB = 0;                                      // a1[8] = 0   (flagB)

    // The id column dword the original reads is &dword_12CE914[134*idx] + 2 (a
    // misaligned dword inside the id column); its high byte is compared to the
    // person id's high byte (*(int*)(*a1+6)>>24). Both come from g_personIds[idx]
    // (the id column) — the +2 offset reads the high half of that id column dword.
    i32 idColPlus2;
    std::memcpy(&idColPlus2,
                reinterpret_cast<const u8*>(&g_personIds[playerIdx]) + 2,
                sizeof idColPlus2);
    const i32 personPlus6 = PDword(p, 6);

    if ((idColPlus2 >> 24) != (personPlus6 >> 24)
        && static_cast<i8>(PByte(p, 458)) < 0) {
        // filter 111: a handler whose +172 == playerId and +176 == person id sets
        // flagB (a1[8]) to 1599.
        for (HeRecord* h = HeFirst(111); h; h = HeNext()) {
            if (HeMatchKey172(h) == playerId && HeMatchRel176(h) == personId) {
                entry->flagB = 1599;
                break;
            }
        }
    }

    // --- flagC (a1[9]) ---
    entry->flagC = 0;                                      // v1[9] = 0
    if (HeCount(PDword(p, 4)) <= 0) {
        // filter 24: a handler whose +8 (word) == playerIdx and +196 == person id
        // sets flagC to 1601.
        for (HeRecord* h = HeFirst(24); h; h = HeNext()) {
            if (He_CityIndex(h) == playerIdx && HeMatchKey196(h) == personId) {
                entry->flagC = 1601;
                break;
            }
        }
    } else {
        entry->flagC = 1600;
    }

    // --- flagC bucket / flagD (a1[10]) ---
    entry->flagD = 0;                                      // v5[10] = 0
    // filter 69: a handler whose +172 == playerId and +176 == person id sets
    // flagD to 1598 and returns 1.
    for (HeRecord* h = HeFirst(69); h; h = HeNext()) {
        if (HeMatchKey172(h) == playerId && HeMatchRel176(h) == personId) {
            entry->flagD = 1598;
            return 1;
        }
    }
    if (entry->flagD)                                      // if (v7[10]) return 1
        return 1;

    // Bucket the building output into flagD.
    float out = BuildOutput(p);
    if (out < kOutputLow) {
        entry->flagD = 1605;
    } else if (out >= kOutputMid) {
        entry->flagD = (out >= kOutputHigh) ? 1602 : 1603;
    } else {
        entry->flagD = 1604;
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x5555c8 — VIBE_Office_CollectFamilyHeirCandidates.
// ---------------------------------------------------------------------------
//   a1=refRec, a2=capacity, a3=filter, a4=out. Scan filter-111 handlers for up
//   to 4 whose +172 == refRec id; resolve +176 -> person rec; collect.
// ===========================================================================
u32 OfficeCollectFamilyHeirCandidates(Person* refRec, u32 capacity, int filter,
                                      PersonRelEntry* out) {
    u32 found = 0;
    Person* heirs[4] = {nullptr, nullptr, nullptr, nullptr};  // v15[1..4]

    const i32 refId = PDword(refRec, 4);                  // *((DWORD*)v6 + 1)
    int v7 = 0;                                            // count into heirs[]
    for (HeRecord* h = HeFirst(111); h && v7 < 4; h = HeNext()) {
        if (HeMatchKey172(h) == refId) {
            Person* rec = PersonFindRecordById(HeMatchRel176(h));
            if (rec) {
                heirs[v7] = rec;                          // v15[++v7] = rec
                ++v7;
                ++found;
            }
        }
    }

    if (!capacity)                                        // if (!v19) return v4
        return found;

    ClearEntries(out, capacity);                          // memset 56*capacity

    const u32 cap = (capacity >= found) ? found : capacity;  // min(capacity,found)
    u32 i = 0;                                            // index into heirs[]
    u32 n = 0;                                            // entries written
    PersonRelEntry* e = out;
    for (; n < cap; ++i) {
        e->person = heirs[i];                             // *v8 = v15[i+1]
        PersonResolveStatusFlags(e);
        i32 candMarker = static_cast<u16>(e->person->marker);  // v12 = **v8
        e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                  static_cast<u16>(candMarker), filter);
        ++n;
        ++e;
    }
    return found;                                         // return v18 (== v4)
}

// ===========================================================================
// gilde.exe 0x554e34 — VIBE_Person_CollectRelatedNpcs.
// ---------------------------------------------------------------------------
//   a1=refRec, a2=capacity, a3=filter, a4=out. Walk the 768-slot Person array.
//   refRec relation id array: *((DWORD*)refRec + 23) == refRec+92 (relation[0]),
//   8 dwords (index += 2 words each iter => +4 bytes => relation[0..7]).
//   Candidate person id == *((DWORD*)cand + 1) == cand+4.
//   Household match: cand[40] (word @+80) == refRec[40] && cand+88 == refRec+88.
// ===========================================================================
//
// Relation class (entry +44), derived from the relation-array index `slot` (the
// original's v16) and the candidate's married/gender byte (cand+9):
//   slot 0 -> (cand+9 != 0) + 4    (5 if married, 4 otherwise)
//   slot 1 -> 0
//   slot 2 -> 1
//   slot >=3 -> (cand+9 != 0) + 6  (7 if married, 6 otherwise)
//   household-only match (no relation slot) -> (cand+9 != 0) + 2
//   extra filter-65 relative -> (cand+9 != 0) + 8
int PersonCollectRelatedNpcs(Person* refRec, u32 capacity, int filter,
                             PersonRelEntry* out) {
    int v5 = 0;  // entries collected

    if (capacity) {
        ClearEntries(out, capacity);                      // memset 56*capacity

        const i16 refHouse = PWord(refRec, 80);           // a1[40]
        const u8  refClass = PByte(refRec, 88);           // *((BYTE*)a1 + 88)

        int slotIdx = 0;
        for (Person* cand = &g_persons[0];
             slotIdx < 768 && static_cast<u32>(v5) < capacity;
             ++slotIdx, ++cand) {
            // 0x55504a: `*((char*)v19 + 2) < 10` — SIGNED char compare of the
            // kind byte (kind >= 0x80 passes in the binary).
            if (cand == refRec || cand->marker == -1
                || static_cast<i8>(PByte(cand, 2)) >= 10)
                continue;

            const i32 candId = PDword(cand, 4);           // *((DWORD*)v19 + 1)
            const u8  candMarried = PByte(cand, 9);       // *((BYTE*)v19 + 9)

            // Scan refRec's 8-entry relation id array for candId.
            int rel = 0;
            bool matched = false;
            for (; rel < 8; ++rel) {
                if (PDword(refRec, 92 + 4 * rel) == candId) {
                    matched = true;
                    break;
                }
            }

            PersonRelEntry* e = &out[v5];
            if (matched) {
                e->person = cand;                         // *v20 = v19
                int cls;
                if (rel == 0)       cls = (candMarried != 0) + 4;
                else if (rel == 1)  cls = 0;
                else if (rel == 2)  cls = 1;
                else                cls = (candMarried != 0) + 6;
                e->relClass = cls;
                ++v5;
                continue;
            }
            // Household fallback: same household word & same class byte.
            if (PWord(cand, 80) == refHouse && PByte(cand, 88) == refClass) {
                e->person = cand;
                ++v5;
                e->relClass = (candMarried != 0) + 2;
            }
        }

        // One extra related person via a filter-65 handler whose +172 == refRec id.
        if (static_cast<u32>(v5) < capacity) {
            const i32 refId = PDword(refRec, 4);          // *((DWORD*)a1 + 1)
            HeRecord* h = HeFirst(65);
            for (; h; h = HeNext()) {
                if (HeMatchKey172(h) == refId)
                    break;
            }
            Person* rec = nullptr;
            if (h)
                rec = PersonFindRecordById(HeMatchRel176(h));
            // The handler's related person must not already be refRec's relation[0]
            // (*((DWORD*)rec + 1) != *((DWORD*)a1 + 23) == refRec+92).
            if (rec && PDword(rec, 4) != PDword(refRec, 92)) {
                PersonRelEntry* e = &out[v5];
                e->person = rec;
                ++v5;
                e->relClass = (PByte(rec, 9) != 0) + 8;
            }
        }

        // Resolve status flags + eligibility per filled entry.
        for (int i = 0; i < v5; ++i) {
            PersonRelEntry* e = &out[i];
            PersonResolveStatusFlags(e);
            i32 candMarker = static_cast<u16>(e->person->marker);
            e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                      static_cast<u16>(candMarker), filter);
        }
        return v5;
    }

    // capacity == 0: count-only pass (no household-fallback double count: the
    // original `goto LABEL_4` skips the household test once a relation matched).
    const i16 refHouse = PWord(refRec, 80);
    const u8  refClass = PByte(refRec, 88);
    for (int slotIdx = 0; slotIdx < 768; ++slotIdx) {
        Person* cand = &g_persons[slotIdx];
        // 0x554ebf: signed char compare (see filled path).
        if (cand == refRec || cand->marker == -1
            || static_cast<i8>(PByte(cand, 2)) >= 10)
            continue;
        const i32 candId = PDword(cand, 4);
        bool matched = false;
        for (int rel = 0; rel < 8; ++rel) {
            if (PDword(refRec, 92 + 4 * rel) == candId) {
                ++v5;
                matched = true;
                break;
            }
        }
        if (matched)
            continue;
        if (PWord(cand, 80) == refHouse && PByte(cand, 88) == refClass)
            ++v5;
    }
    // One extra related person via filter-65 (counts once if present).
    const i32 refId = PDword(refRec, 4);
    for (HeRecord* h = HeFirst(65); h; h = HeNext()) {
        if (HeMatchKey172(h) == refId) {
            ++v5;
            break;
        }
    }
    return v5;
}

// ===========================================================================
// gilde.exe 0x555150 — VIBE_Person_CollectTopByScore.
// ---------------------------------------------------------------------------
//   a1=refRec, a2=capacity, a3=filter, a4=out. Walk the Person array scoring
//   candidates of kind 2..7 by favorability toward refRec; keep score >= 75.0,
//   exclude refRec's own relation[0] id (cand+524 != refRec+4), insertion-sort
//   the top `capacity` by descending score (entry +44 holds the int score).
// ===========================================================================
int PersonCollectTopByScore(Person* refRec, u32 capacity, int filter,
                            PersonRelEntry* out) {
    int v5 = 0;  // entries collected

    if (capacity) {
        ClearEntries(out, capacity);                      // memset 56*capacity

        const i32 refId = PDword(refRec, 4);              // *((DWORD*)a1 + 1)

        for (int i = 0; i < 768; ++i) {
            Person* cand = &g_persons[i];                 // word_12CE910[268*i]
            if (cand == refRec || cand->marker == -1)
                continue;
            const u8 k = PByte(cand, 2);
            if (k < 2 || k > 7)
                continue;

            float score = Favor(static_cast<u16>(refRec->marker),
                                static_cast<u16>(cand->marker), 1);
            // SLODWORD(score) >= 1117126656  (i.e. score >= 75.0 as a float bit cmp)
            if (score < kFavorCutoff)
                continue;
            // cand+524 (relation/employer id) must differ from refRec id, and the
            // new score must beat the current weakest kept entry (out[cap-1]+44).
            if (PDword(cand, 524) == refId)
                continue;
            // (double)(int)a4[14*a2-3] < v29 : the weakest kept score (the tail
            // slot out[capacity-1] +44) must be strictly below the new score. The
            // memset leaves the tail at 0 until `capacity` entries are kept, so a
            // qualifying score always enters until the array is full.
            if (!(static_cast<double>(out[capacity - 1].relClass) < score))
                continue;

            // Insert the candidate at the tail slot, then bubble it up while its
            // score exceeds the predecessor's (descending order). The original
            // (v19 walk) swaps out[j] with out[j-1] while out[j-1]+44 < score, so
            // the kept array stays sorted by descending score (entry +44).
            ++v5;
            int tail = static_cast<int>(capacity) - 1;
            out[tail].person   = cand;
            out[tail].relClass = static_cast<int>(score);

            for (int j = tail; j > 0; --j) {
                if (out[j].relClass <= out[j - 1].relClass)
                    break;
                Person* tp = out[j].person;
                int     ts = out[j].relClass;
                out[j].person   = out[j - 1].person;
                out[j].relClass = out[j - 1].relClass;
                out[j - 1].person   = tp;
                out[j - 1].relClass = ts;
            }
        }

        if (v5 >= static_cast<int>(capacity))
            v5 = static_cast<int>(capacity);

        for (int i = 0; i < v5; ++i) {
            PersonRelEntry* e = &out[i];
            PersonResolveStatusFlags(e);
            i32 candMarker = static_cast<u16>(e->person->marker);
            e->eligibility = Eligible(static_cast<u16>(refRec->marker),
                                      static_cast<u16>(candMarker), filter);
        }
        return v5;
    }

    // capacity == 0: count-only pass.
    const i32 refId = PDword(refRec, 4);
    for (Person* cand = &g_persons[0];
         cand != &g_persons[768]; ++cand) {
        if (cand == refRec || cand->marker == -1)
            continue;
        const u8 k = PByte(cand, 2);
        if (k < 2 || k > 7)
            continue;
        float score = Favor(static_cast<u16>(refRec->marker),
                            static_cast<u16>(cand->marker), 1);
        // v28 >= 1117126656 && cand+524 != refId
        if (score >= kFavorCutoff && PDword(cand, 524) != refId)
            ++v5;
    }
    return v5;
}

}  // namespace guild::sim
