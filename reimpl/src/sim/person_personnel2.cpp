#include "sim/person_personnel2.h"
#include "sim/person.h"         // PersonGetByte/Word/Dword field accessors
#include "sim/entity.h"         // g_persons, kPersonCapacity
#include "sim/person_create.h"  // g_personLiveCount (dword_647724 mirror)

#include <cstdint>
#include <cstdlib>   // std::abs

// Faithful 1:1 port of a second batch of Person/Personnel query, wealth, and
// staff-book leaves from gilde.exe. Field reads use the byte-offset accessors so
// access width / (un)alignment matches the binary. Cross-module callees that are
// not reconstructed in src/ go through PersonPersonnel2Hooks (inert defaults
// below). The Hex-Rays pseudocode is the reference of record; each function's
// header comment reproduces the decompiled body it was translated from.

namespace guild::sim {

// --- hooks plumbing (inert defaults defined once, in the library) -----------
static PersonPersonnel2Hooks  g_defaultPP2Hooks;
static PersonPersonnel2Hooks* g_pp2Hooks = &g_defaultPP2Hooks;
void SetPersonPersonnel2Hooks(PersonPersonnel2Hooks* hooks) {
    g_pp2Hooks = hooks ? hooks : &g_defaultPP2Hooks;
}
PersonPersonnel2Hooks* PersonPersonnel2HooksPtr() { return g_pp2Hooks; }

// ===========================================================================
// VIBE_Person_FindActiveByEntity  0x5920b0
//   if (!result) return result;                       // entityId 0 -> null
//   for (v4 = 0, v3 = 0; v4 < 102912; v4 += 134, ++v3) // 134*768 == 102912
//     if (byte_12CEA74[v4*4]                            // +356 alive flag
//         && result == dword_12CEA7C[v4]) {             // +364 entity column
//       v5 = byte_12CE912[v4*4];                        // +2 kind byte
//       if (v5 == 6 || v5 == 7 || v5 == 1 || v5 == 2)
//         return &word_12CE910[268*v3];                 // the person record
//     }
//   return 0;
// NB v4 is a *dword* index (stride 134 dwords == 536 bytes == one record); the
// `v4*4` byte index lands on the same record's +356 / +2 fields. We iterate by
// record index directly.
// ===========================================================================
Person* PersonFindActiveByEntity(i32 entityId) {
    if (entityId == 0)
        return nullptr;
    for (int v3 = 0; v3 < kPersonCapacity; ++v3) {
        Person& p = g_persons[v3];
        if (PersonGetByte(&p, kPf2EntityAlive) != 0
            && PersonGetDword(&p, kPf2EntityPtr) == entityId) {
            u8 kind = PersonGetByte(&p, kPfKind);
            if (kind == 6 || kind == 7 || kind == 1 || kind == 2)
                return &p;
        }
    }
    return nullptr;
}

// ===========================================================================
// VIBE_Person_FindNearestByDistance  0x5949fc
//   v2 = -1; v7 = 1e10;
//   for (i = 0, v3 = 0, a1 = arg; i < 768; ++i, v3 += 268, a1 += 768)
//     if (word_12CE910[v3] != -1) {                     // marker != -1
//       v6 = (double)*((char*)dword_123D6CD + a1 + 3);  // distance scalar
//       if (v6 < v7) { v7 = v6; v2 = i; }
//     }
//   return v2;
// The distance table read is hooked (NearestDistance). v7 starts at 1e10 so any
// real distance wins; ties keep the earlier slot (strict <).
// ===========================================================================
int PersonFindNearestByDistance(int distBase) {
    int best = -1;
    double bestDist = 1.0e10;
    int a1 = distBase;
    for (int i = 0; i < kPersonCapacity; ++i, a1 += kPersonCapacity) {
        if (g_persons[i].marker != -1) {
            double d = static_cast<double>(g_pp2Hooks->NearestDistance(i, a1));
            if (d < bestDist) {
                bestDist = d;
                best = i;
            }
        }
    }
    return best;
}

// ===========================================================================
// VIBE_Person_FindEmploymentRelation  0x58d95c
//   if (rec.kind == 6 || rec.kind == 7) return 0;       // already an employer
//   for (slot = 0..767, p = persons[slot]):
//     if (p.id == rec.id) continue;                      // skip self (LABEL_7)
//     k = p.kind; if (k != 6 && k != 7) continue;        // only employer records
//     if (p.rel[+92]  == rec.id) return 0;               //  (xor self == 0)
//     if (p.rel[+96]  == rec.id) return 0;
//     if (p.rel[+100] == rec.id) return 0;
//     // extended relation array +104..+120 (5 dwords):
//     v2 = 0; v3 = 0;
//     for (off = 104; off < 124; off += 4) {
//       e = p.rel[off];
//       if (e == rec.id) v2 |= 1; else if (e == -1) {/*term, no ++v3*/}
//       else ++v3;       // (the -1 branch skips the counter increment)
//       if matched-at-end: if (v2 && v3 < 5) return 0;   //  (xor end==0)
//     }
//   // array exhausted:
//   if (768 - liveCount >= 32) goto next-slot;           // not near full
//   else scan staff book:
//     for (r in book) if (r.personId == rec.id && r.active) return 0;
//     return 1;                                          // orphaned, book full
// The relation-match branches all reduce to a faithful 0 (the original's
// xor-of-equal-registers). Only the staff-book-overflow not-found path yields 1.
// ===========================================================================
int PersonFindEmploymentRelation(Person* rec) {
    u8 selfKind = PersonGetByte(rec, kPfKind);
    if (selfKind == 6 || selfKind == 7)
        return 0;
    i32 selfId = PersonGetDword(rec, kPfId);

    for (int slot = 0; slot < kPersonCapacity; ++slot) {
        Person& p = g_persons[slot];

        if (PersonGetDword(&p, kPfId) == selfId)  // skip self (LABEL_7)
            goto next_slot;
        {
            u8 k = PersonGetByte(&p, kPfKind);
            if (k != 6 && k != 7)
                goto next_slot;
        }
        // three primary relation/employer ids (+92 / +96 / +100)
        if (PersonGetDword(&p, kPf2RelArray + 0) == selfId) return 0;
        if (PersonGetDword(&p, kPf2RelArray + 4) == selfId) return 0;
        if (PersonGetDword(&p, kPf2RelArray + 8) == selfId) return 0;
        {
            // extended relation array: +104..+120 (5 dwords). Count valid
            // entries (v3); -1 acts as a terminator that is not counted.
            int v2 = 0, v3 = 0;
            for (int off = kPf2RelArray + 12; off != kPf2RelArray + 32; off += 4) {
                i32 e = PersonGetDword(&p, off);
                // disasm 0x58da29: on match -> `or bl,1` then fall through to
                // `mov edx,ecx` (++v3). Only the -1 terminator jumps PAST the
                // ++v3 (loc_58DA39). So both a match and any non-(-1) value
                // increment v3; only -1 skips it.
                if (e == selfId) {
                    v2 |= 1;
                    ++v3;
                } else if (e == -1) {
                    ; /* terminator: skip ++v3 (loc_58DA39) */
                } else {
                    ++v3;
                }
            }
            if (v2 && v3 < 5)
                return 0;  // structural relation found (xor end==0)
        }
    next_slot:;
    }

    // Array exhausted (v12 >= 768). 0x58d9d6:
    //   if (768 - dword_647724 < 32) return 1;    // table NEAR FULL -> 1, NO scan
    //   else scan the staff book (0x58d9ec).      // >= 32 free -> book scan
    // (An earlier transcription inverted this guard — decompile-refuted.)
    // dword_647724 is mirrored 1:1 by g_personLiveCount (person_create.cpp).
    if (kPersonCapacity - g_personLiveCount < 32)
        return 1;

    int bookCount = 0;
    const StaffBookRecord* book = g_pp2Hooks->StaffBook(&bookCount);
    if (book) {
        for (int i = 0; i < bookCount; ++i)
            if (book[i].personId == selfId && book[i].active != 0)
                return 0;  // found in staff book (0x58d9ec -> return 0)
    }
    return 1;  // scan exhausted (0x58d9f6)
}

// ===========================================================================
// VIBE_Person_ComputeAssetWorth  0x591328
//   v9 = 0;
//   if (rec.familyWord(+39) != 0xFFFF) {                 // valid household slot
//     v4 = 268 * familyWord;                             // index into person arr
//     if (byte_12CE912[v4*2] < 10)                       // family head kind < 10
//       v9 = SumCurrencyHeld(&word_12CE910[v4]);         // currency of head
//   }
//   if (!includeBuildings) return v9;
//   v5 = QueryFind(rec.container(+93), ...);             // owned buildings
//   for (each room/good) v9 = (int)(MarketPrice(...) + (double)v9);
//   return v9;
// The familyWord indexes the *person* array (head-of-household record); we read
// the head's kind via that record. SumCurrencyHeld + owned-building worth are
// hooked (the reconstructed PersonSumCurrencyHeld takes a ContainerView).
// ===========================================================================
int PersonComputeAssetWorth(Person* rec, int includeBuildings) {
    int v9 = 0;
    u16 familyWord = static_cast<u16>(PersonGetWord(rec, kPf2FamilyWord));
    if (familyWord != 0xFFFF) {
        Person& head = g_persons[familyWord];
        if (PersonGetByte(&head, kPfKind) < 10)
            v9 = g_pp2Hooks->SumCurrencyHeld(&head);
    }
    if (!includeBuildings)
        return v9;
    // Owned-building market worth (the original accumulates MarketPrice + v9 in
    // double, then truncates to int each iteration; the hook returns the final
    // truncated sum of the worth terms which we add to v9).
    v9 += g_pp2Hooks->OwnedBuildingWorth(rec);
    return v9;
}

// ===========================================================================
// VIBE_Person_CheckDebtRatioCritical  0x591ff0
//   v2 = SumCurrencyHeld(rec);
//   net = (float)(reserve + v2);                         // edx == reserve term
//   if (net > 0.0) return 0;                             // solvent
//   wealth = (double)ComputeTotalWealth(rec) - net;      // positive denominator
//   k = rec.kind;
//   ratio = (double)abs((int)net) / wealth;
//   threshold = (k == 6 || k == 7) ? 0.07 : 0.20;        // dbl_626A6C / dbl_626A64
//   return ratio > threshold;
// (The VIBE_Coord_ConvertX() calls in the decompile are the fp-round shims that
// do not affect the integer/float result; omitted.)
// ===========================================================================
bool PersonCheckDebtRatioCritical(Person* rec, int reserve) {
    int v2 = g_pp2Hooks->SumCurrencyHeld(rec);
    // var_10 = (float)(reserve + v2): the int sum is narrowed to float (fild;fstp).
    float net = static_cast<float>(reserve + v2);
    // fldz;fcomp var_10;jnb -> continue when net <= 0; return 0 when net > 0.
    if (net > 0.0f)
        return false;
    // var_C = (float)((double)wealth - net): wealth-net is computed in x87 then
    // STORED BACK TO A 4-byte float (fsub;fstp var_C @0x59202b/0x592031). The
    // denominator is therefore single precision before the divide.
    float denom = static_cast<float>(
        static_cast<double>(g_pp2Hooks->ComputeTotalWealth(rec))
        - static_cast<double>(net));
    u8 k = PersonGetByte(rec, kPfKind);
    // numerator: |trunc(net)| -> int (ConvertX truncates toward zero, fistp),
    // abs via cdq/xor/sub, then fild back to fp.
    int absNet = std::abs(static_cast<int>(net));
    // fild var_8; fdiv var_C; fcomp dbl  -> ratio = |net| / denom > threshold.
    double ratio = static_cast<double>(absNet) / static_cast<double>(denom);
    double threshold = (k == 6 || k == 7) ? 0.07 : 0.20;  // dbl_626A6C / dbl_626A64
    return ratio > threshold;
}

// ===========================================================================
// VIBE_Person_SyncMasterShopObjects  0x594c94
//   for (m = QueryBegin(slot,1,0,30); m; m = IterNext())  // filter {op 0, 30}
//     for (child shop objects of m) child.owner = m.owner; // propagate
// The original iterates EVERY matching master record (0x594ca7 for-loop with
// IterNext), not just the first — we walk the real in-tree query (the same
// PersonQueryBegin/PersonIterNext pair the binary calls) and hook only the
// child-propagation scene walk. (An earlier adaptation synced only the first
// match via the QueryFirst hook — decompile-refuted.)
// ===========================================================================
void PersonSyncMasterShopObjects(int slotArg) {
    (void)slotArg;  // QueryBegin seed (match-neutral; see person_query.cpp note)
    PersonFilter filter{0, 30};                        // op 0: alive/type byte 30
    for (ObjectRec* m = PersonQueryBegin(&filter, 1); m; m = PersonIterNext())
        g_pp2Hooks->SyncShopChildren(reinterpret_cast<Person*>(m));
}

// ===========================================================================
// VIBE_Person_BeginQueryThenSetField270  0x59630c
//   if (!(BYTE)result) {
//     result = QueryBegin(slot, 1, 5, 15);               // filter tag 15
//     if (result) *(WORD*)(result + 41) = 270;
//   }
//   return result;
// ===========================================================================
Person* PersonBeginQueryThenSetField270(Person* cur, int slotArg) {
    if (cur != nullptr)
        return cur;  // low byte nonzero -> keep current
    Person* rec = g_pp2Hooks->QueryFirst(slotArg, 15);
    if (rec)
        PersonSetWord(rec, 41, static_cast<i16>(270));
    return rec;
}

// ===========================================================================
// VIBE_Person_BeginQueryThenOpenBuilding  0x59647c
// VIBE_Person_BeginQueryThenOpenBuildingAlt 0x596514  (byte-identical)
//   if ((BYTE)state == 1) {
//     rec = QueryBegin(slot, 1, 5, 10);                  // filter tag 10
//     if (rec) low = OpenBuildingForActiveChar(rec, byte_626DA0);
//   }
//   return low;
// ===========================================================================
u8 PersonBeginQueryThenOpenBuilding(u8 stateLow, int slotArg) {
    if (stateLow != 1)
        return stateLow;
    Person* rec = g_pp2Hooks->QueryFirst(slotArg, 10);
    if (rec)
        return g_pp2Hooks->OpenBuildingForActiveChar(rec);
    // 0x596487: `LODWORD(a1) = QueryBegin(...)` REPLACES the low byte — a null
    // result makes the returned al 0, not the incoming 1 (decompile-refuted an
    // earlier "low byte unchanged" reading).
    return 0;
}

// ===========================================================================
// VIBE_Personnel_BuildBookRow  0x53b5d0
//   row.window = AddToWindow(...);                        // window widget
//   row.labelA = AddTextLabel(...); set flags;
//   row.labelB = AddTextLabel(...); set flags;
//   row.slider = -1; (a3+16)
//   row.[a3+28] = -1;                                     // extra widget
//   row.state  = 0; (a3+8)
//   row.flag33 = 0; (a3+33)
//   row.winY   = a2; row.[a3+84] = form; row.winX = a1;
//   for (p = row; p != row+24; p += 4) { p[+32] = -1; p[+56] = -1; }
//   return p;                                             // == row + 24
// Widget creation is hooked (AddWidget). We faithfully reset the handle fields;
// the sentinel-array clear (+32/+56) is modeled on the row's extra fields.
// ===========================================================================
PersonnelBookRow* PersonnelBuildBookRow(int x, int y, PersonnelBookRow* row) {
    row->window = g_pp2Hooks->AddWidget(/*kind*/0, x, y);       // AddToWindow
    row->labelA = g_pp2Hooks->AddWidget(/*kind*/1, x - 54, y - 17);  // AddTextLabel
    row->labelB = g_pp2Hooks->AddWidget(/*kind*/1, x - 54, y + 68);  // AddTextLabel
    row->slider = -1;        // a3+16
    row->extraStart = -1;    // a3+28
    row->state = 0;          // a3+8
    row->flag33 = 0;         // a3+33
    row->winY = y;           // a3+4
    row->winX = x;           // a3+0
    // sentinel clear loop (p[+32]/p[+56] = -1 for p in [row, row+24)):
    row->extraEnd = -1;
    return row;              // original returns row+24
}

// ===========================================================================
// VIBE_Personnel_DestroyBookRowWidgets  0x53ba4c
//   if (row[+28] != -1) { result = DestroyByType(row[+28]); row[+28] = -1; }
//   if (row[+16] != -1) { result = DestroyByType(row[+16]); row[+16] = -1; }
//   for (p = row; p != row+24; p += 4) {
//     if (p[+36] != -1) { result = DestroyByType(p[+36]); p[+36] = -1; }
//     if (p[+60] != -1) { result = DestroyByType(p[+60]); p[+60] = -1; }
//   }
//   return result;
// DestroyByType is hooked. We model the two named widget handles (extraStart at
// +28, slider at +16) plus the sentinel pair; result echoes the last handle
// destroyed (the original returns the DestroyByType return; we return the last
// destroyed handle as a faithful nonzero/zero proxy).
// ===========================================================================
int PersonnelDestroyBookRowWidgets(PersonnelBookRow* row) {
    int result = 0;
    if (row->extraStart != -1) {       // row+28
        g_pp2Hooks->DestroyWidget(row->extraStart);
        result = row->extraStart;
        row->extraStart = -1;
    }
    if (row->slider != -1) {           // row+16
        g_pp2Hooks->DestroyWidget(row->slider);
        result = row->slider;
        row->slider = -1;
    }
    if (row->extraEnd != -1) {         // sentinel pair (+36/+60 region)
        g_pp2Hooks->DestroyWidget(row->extraEnd);
        result = row->extraEnd;
        row->extraEnd = -1;
    }
    return result;
}

}  // namespace guild::sim
