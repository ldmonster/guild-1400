#include "sim/person.h"
#include "sim/entity.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

// Faithful 1:1 port of the Person attribute / office / family accessors from
// gilde.exe. The originals reach record fields through per-field global symbols
// (byte_12CE912, dword_12CEABC, ...) that are really the same 536-byte record at
// a fixed byte offset; we read/write by byte offset (PersonField) so the access
// width and (un)alignment match the binary exactly.

namespace guild::sim {

// --- raw byte-offset field access (matches the binary's unaligned loads) -----
u8 PersonGetByte(const Person* rec, int off) {
    return reinterpret_cast<const u8*>(rec)[off];
}
void PersonSetByte(Person* rec, int off, u8 v) {
    reinterpret_cast<u8*>(rec)[off] = v;
}
i16 PersonGetWord(const Person* rec, int off) {
    i16 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(rec) + off, sizeof(v));
    return v;
}
void PersonSetWord(Person* rec, int off, i16 v) {
    std::memcpy(reinterpret_cast<u8*>(rec) + off, &v, sizeof(v));
}
i32 PersonGetDword(const Person* rec, int off) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(rec) + off, sizeof(v));
    return v;
}
void PersonSetDword(Person* rec, int off, i32 v) {
    std::memcpy(reinterpret_cast<u8*>(rec) + off, &v, sizeof(v));
}

// ===========================================================================
// VIBE_Person_IsValidActiveRecord  0x4f8e60
//   return word_12CE910[v1/2] != -1 && byte_12CE918[v1] && byte_12CE912[v1] < 10;
// (v1 = 536*a1). The three symbols are record+0 (marker), +8 (isPlayer), +2 (kind).
// ===========================================================================
bool PersonIsValidActiveRecord(u16 idx) {
    const Person& p = g_persons[idx];
    return p.marker != -1
        && PersonGetByte(&p, kPfIsPlayer) != 0
        && PersonGetByte(&p, kPfKind) < 10;
}

// ===========================================================================
// VIBE_Person_GetCashAmount  0x58bc9c
//   return (double)*(u16*)((char*)&dword_12CE919[134*a1] + 1);
// dword_12CE919 == record+9, +1 byte == record+10 (cash word).
// ===========================================================================
double PersonGetCashAmount(u16 idx) {
    return static_cast<double>(
        static_cast<u16>(PersonGetWord(&g_persons[idx], kPfCash)));
}

// ===========================================================================
// VIBE_Person_ComputePriceMultiplier  0x58f71c
//   rep = *((u8*)rec + 128);
//   if (rep <= 42.0f) return 1.0;
//   return (float)(rep * dbl_626984 * dbl_62698C + 1.0);
// Constants (exact bytes from the IDB): flt_62697C = 42.0, dbl_626984 = 0.25,
// dbl_62698C = 0.003968253968253968 (== 1/252).
// ===========================================================================
constexpr double kRepThreshold = 42.0;                 // flt_62697C
constexpr double kRepScaleA    = 0.25;                 // dbl_626984
constexpr double kRepScaleB    = 0.003968253968253968; // dbl_62698C (1/252)

double PersonComputePriceMultiplier(int idx) {
    const Person& p = g_persons[idx];
    double rep = static_cast<double>(PersonGetByte(&p, kPfReputation));
    if (rep <= static_cast<float>(kRepThreshold))
        return 1.0;
    // The original folds the two doubles, then narrows the sum to float.
    return static_cast<float>(rep * kRepScaleA * kRepScaleB + 1.0);
}

// ===========================================================================
// VIBE_Person_ComputeWealthRank  0x592ae8
//   if (a1 >= 0x300) return 0;
//   v4 = dword_12CEABC[134*a1] + a2;   // this person's score + bonus
//   rank = 1;
//   for (i = 0; i < 768; ++i)
//     if (v4 <= score(i) && marker(i) != -1 && kind(i) < 10 && i != a1) ++rank;
//   return rank;
// ===========================================================================
int PersonComputeWealthRank(u32 idx, int bonus) {
    if (idx >= 0x300)
        return 0;
    int v4 = PersonGetDword(&g_persons[idx], kPfWealthScore) + bonus;
    int rank = 1;
    for (u32 i = 0; i < 768; ++i) {
        const Person& q = g_persons[i];
        if (v4 <= PersonGetDword(&q, kPfWealthScore)
            && q.marker != -1
            && PersonGetByte(&q, kPfKind) < 10
            && i != idx)
            ++rank;
    }
    return rank;
}

// ===========================================================================
// VIBE_Person_ComputeOfficeRank  0x58bccc
// ===========================================================================
static OfficeDefinitionFn g_officeDefFn = nullptr;
void PersonSetOfficeDefinitionHook(OfficeDefinitionFn fn) { g_officeDefFn = fn; }

int PersonComputeOfficeRank(u16 idx, int stopAtSelf) {
    Person& p = g_persons[idx];
    // dword_12CE914[134*a1] == record+4 (id); byte_12CE918[536*a1] == +8.
    if (PersonGetDword(&p, kPfId) == -1 || PersonGetByte(&p, kPfIsPlayer) == 0)
        return 0;

    u8 def[24] = {};
    // VIBE_Office_GetDefinition(byte_12CEA76[536*a1], &def): office id at +0x166.
    int ok = g_officeDefFn
                 ? g_officeDefFn(PersonGetByte(&p, kPfOffice), def)
                 : 0;
    if (!ok)
        return 1;

    int level = def[2]; // BYTE2(v9): the office's rank level.
    if (level == 10 || stopAtSelf)
        return level + 1;

    // Recurse up the office-superior chain (dword_12CE970 == record+0x60).
    // The original computes the parent's rank and returns max(level+1, parentRank).
    int self = level + 1;
    Person* superior = PersonFindRecordById(PersonGetDword(&p, kPfSuperiorId));
    if (superior) {
        u16 superIdx = static_cast<u16>(superior->marker); // *RecordById == marker
        int parent = PersonComputeOfficeRank(superIdx, 1);
        if (self <= parent)
            return PersonComputeOfficeRank(superIdx, 1);
    }
    return self;
}

// ===========================================================================
// VIBE_Person_IsFamilyMemberEligible  0x58c2f8
//   Operates on two record pointers: a1 = candidate, a2 = reference.
// ===========================================================================
static TargetUnderfullFn g_targetUnderfullFn = nullptr;
void PersonSetTargetUnderfullHook(TargetUnderfullFn fn) { g_targetUnderfullFn = fn; }

static bool TargetUnderfull(const Person* rec) {
    // The original passes *(WORD*)rec (the marker word, which equals the slot
    // index for live persons) to VIBE_Combat_IsTargetUnderfull.
    if (!g_targetUnderfullFn)
        return false;
    return g_targetUnderfullFn(static_cast<u16>(PersonGetWord(rec, 0)));
}

bool PersonIsFamilyMemberEligible(const Person* candidate, const Person* reference) {
    const u8* a1 = reinterpret_cast<const u8*>(candidate);
    const u8* a2 = reinterpret_cast<const u8*>(reference);

    // v4 = ref.age(+88) - cand.age(+88)
    int v4 = static_cast<int>(a2[kPfAge]) - static_cast<int>(a1[kPfAge]);
    // if (!ref.isPlayer(+8)) return 0;
    if (a2[kPfIsPlayer] == 0)
        return false;

    // Find the reference's id (+4) in the candidate's relation array (+92, 8 dw).
    i32 refId = PersonGetDword(reference, kPfId);
    int v7 = 0;
    if (PersonGetDword(candidate, kPfRelationBase) != refId) {
        do {
            ++v7;
        } while (v7 < 8
                 && PersonGetDword(candidate, kPfRelationBase + 4 * v7) != refId);
    }

    int v8; // relation class
    if (v7 >= 8) {
        // Not in the relation array: only eligible if same age and same household.
        if (v4 == 0
            && PersonGetWord(candidate, kPfFamilyWord)
                   == PersonGetWord(reference, kPfFamilyWord)) {
            v8 = 3;
            goto label10;
        }
        return false;
    }
    if (v7 == 1 || v7 == 2)
        return false;
    if (v7 != 0) {
        v8 = 1;
        if (TargetUnderfull(reference))
            return false;
        goto label16;
    }
    v8 = 2;

label10: {
    // Scan candidate relation slots 3..7 for a live, not-underfull relative.
    int v9 = 3;
    do {
        i32 relId = PersonGetDword(candidate, kPfRelationBase + 4 * v9);
        if (relId != -1) {
            Person* rel = PersonFindRecordById(relId);
            if (rel) {
                if (!TargetUnderfull(rel) && PersonGetByte(rel, kPfIsPlayer))
                    break;
            }
        }
        ++v9;
    } while (v9 < 8);
    if (v9 < 8)
        return false;
}

label16:
    if (v8 <= 2)
        return true;
    // v8 == 3: eligible only if the candidate's primary relation is not a live actor.
    Person* prim = PersonFindRecordById(PersonGetDword(candidate, kPfRelationBase));
    return !prim || PersonGetByte(prim, kPfIsPlayer) == 0;
}

} // namespace guild::sim
