// estate_transfer.cpp — 1:1 reconstruction of VIBE_Person_TransferEstateOwnership
// (gilde.exe 0x58c4a8). See estate_transfer.h for the full provenance map.
// Addresses are gilde.exe (imagebase 0x400000).
#include "world/estate_transfer.h"

#include "sim/entity.h"   // g_persons / g_objects / PersonFindRecordById
#include "sim/person.h"   // PersonGet/SetByte/Word/Dword, kPf* field offsets

#include <cstring>

namespace guild::world {

using guild::sim::Person;
using guild::sim::ObjectRec;
using guild::sim::PersonFindRecordById;
using guild::sim::PersonGetByte;
using guild::sim::PersonSetByte;
using guild::sim::PersonGetWord;
using guild::sim::PersonSetWord;
using guild::sim::PersonGetDword;
using guild::sim::PersonSetDword;
using guild::sim::g_persons;
using guild::sim::g_objects;

// Person record byte offsets touched by the splice (mirrors the original's raw
// *(_BYTE/_WORD/_DWORD)(rec + N) accesses; see types.h PersonField for the named
// subset). The estate splice reaches a few offsets the named enum doesn't cover.
namespace {
constexpr int kOffMarker      = 0x00; // record+0  (LOWORD : marker word)
constexpr int kOffByte2       = 0x02; // record+2  (kind byte; set to 9 in v33)
constexpr int kOffDword1      = 0x04; // record+4  (id dword; v34[1]/v33[1])
constexpr int kOffByte13      = 0x0D; // record+13 (BYTE1(v34[3]) splice)
constexpr int kOffRelation170 = 0x170;// record+0x170 (v34[92] = FROM +0x170)
constexpr int kOffByte356     = 0x164;// record+356 (family-head byte source)
constexpr int kOffByte89      = 0x164;// LOBYTE(v34[89]) == record+0x164 splice
// HIBYTE(*(_DWORD*)(rec+353)) reads the byte at rec+353+3 == rec+356 == 0x164.
constexpr int kOff353Hi       = 0x164;
}

// ---------------------------------------------------------------------------
// Hooks (inert defaults — every cross-module leaf is a no-op so the pure record
// mutation is observable headless).
// ---------------------------------------------------------------------------
namespace {
void DefStraftat(Person*) {}
int  DefMapTypeToCategory(guild::u8) { return 0; }
void DefSetObjectParent(ObjectRec*, guild::u16, guild::u16, int) {}
int  DefComputeTotalWealth(guild::u16, const Person*) { return 0; }
void DefRefreshHandlers(Person*) {}
void DefCharActionCancel(Person*) {}
void DefOfficeRelease(Person*) {}
int  DefGroupFromCode(guild::u8) { return 0; }
int  DefRankWithinGroup(guild::u8) { return 0; }
guild::u8 DefGetCategoryForObject(guild::i16) { return 0; }
void DefAddToParent(int, guild::u16, int, Person*) {}
void DefRemoveAndCleanup(guild::i16, bool) {}

EstateTransferHooks MakeDefaults() {
    EstateTransferHooks h{};
    h.straftatFindAndInit          = DefStraftat;
    h.buildingMapTypeToCategory    = DefMapTypeToCategory;
    h.buildingSetObjectParent      = DefSetObjectParent;
    h.computeTotalWealth           = DefComputeTotalWealth;
    h.heRefreshEntityHandlers      = DefRefreshHandlers;
    h.charActionCancel             = DefCharActionCancel;
    h.officeReleaseHoldings        = DefOfficeRelease;
    h.buildingTypeGroupFromCode    = DefGroupFromCode;
    h.buildingTypeRankWithinGroup  = DefRankWithinGroup;
    h.buildingGetCategoryForObject = DefGetCategoryForObject;
    h.gameObjectAddToParent        = DefAddToParent;
    h.buildingRemoveAndCleanup     = DefRemoveAndCleanup;
    h.ctx = nullptr;
    return h;
}
EstateTransferHooks g_hooks = MakeDefaults();
} // namespace

void EstateTransferSetHooks(const EstateTransferHooks& h) { g_hooks = h; }
void EstateTransferResetHooks() { g_hooks = MakeDefaults(); }
const EstateTransferHooks& EstateTransferGetHooks() { return g_hooks; }

// ---------------------------------------------------------------------------
// Building array layout (dword_13CE298, stride 169). The estate scan reads:
//   *(_BYTE*)(rec)        — type byte (rec+0)
//   *(_WORD*)(rec + 39)   — owner marker (rec+0x27)
// These match ObjectRec.alive (+0) and ObjectRec owner word at +39 (the pad
// region; we read it by raw offset to stay byte-faithful).
// ---------------------------------------------------------------------------
namespace {
constexpr int kBldOwnerWord = 39; // *(_WORD*)(bld + 39)
guild::i16 ReadObjWord(const ObjectRec* o, int off) {
    guild::i16 v;
    std::memcpy(&v, reinterpret_cast<const guild::u8*>(o) + off, sizeof(v));
    return v;
}
} // namespace

int PersonTransferEstateOwnership(guild::i32 fromId, guild::i32 toRec, int mode) {
    const EstateTransferHooks& H = g_hooks;

    // 0x58c4bb: resolve FROM person; -1 if absent.
    Person* fromPtr = PersonFindRecordById(fromId);  // v40
    if (!fromPtr)
        return -1;                                    // 0x58c4e3

    // 0x58c4e4: criminal-record init pass over the FROM record.
    H.straftatFindAndInit(fromPtr);

    const guild::i16 fromMarker = fromPtr->marker;    // *v40 (record+0 word)

    // 0x58c4e9..0x58c50d: walk the 256 building slots. For every live building of
    // category 2 whose owner word (+39) == FROM marker, re-parent it under the
    // focus record (dword_6498E4 — modeled as the FROM record's marker/id pair,
    // the live "active office holder"; the leaf is inert by default).
    for (int b = 0; b < guild::sim::kObjectCapacity; ++b) {
        ObjectRec* bld = &g_objects[b];               // dword_13CE298 + 169*b
        guild::u8 typeByte = bld->alive;              // *(_BYTE*)v7
        if (typeByte != 0
            && H.buildingMapTypeToCategory(typeByte) == 2     // 0x58c897
            && ReadObjWord(bld, kBldOwnerWord) == fromMarker) // *(_WORD*)(v7+39)==*v40
        {
            // 0x58c8b0: re-parent under the focus record. The focus marker/id come
            // from dword_6498E4 in the original; passed through the inert hook.
            H.buildingSetObjectParent(bld, static_cast<guild::u16>(fromMarker),
                                      static_cast<guild::u16>(fromPtr->id), mode);
        }
    }

    // 0x58c52f: snapshot FROM total wealth (v37). v41 packs HIWORD(v40)/marker.
    const guild::u16 fromSlot = static_cast<guild::u16>(fromMarker); // (u16)v41
    const int snapWealth = H.computeTotalWealth(fromSlot, fromPtr);  // v37

    // 0x58c53f: resolve TO person; -2 if absent.
    Person* toPtr = PersonFindRecordById(toRec);      // v35
    if (!toPtr)
        return -2;                                    // 0x58c8c9

    // 0x58c555..0x58c56f: refresh FROM handlers; cancel BOTH persons' actions.
    H.heRefreshEntityHandlers(fromPtr);
    H.charActionCancel(fromPtr);
    H.charActionCancel(toPtr);

    // 0x58c574..0x58c592: family-link fix-up. The original compares the FROM +0x50
    // word against the TO +0x50 word (disasm: eax=(u16)fromPtr[0x50], ecx=(u16)
    // toPtr[0x50], cmp eax,ecx); if they differ it copies the FROM +0x50 word into
    // the TO +0x50 word. (Hex-Rays collapsed both operands onto v10/v11; the disasm
    // shows the LHS is fromPtr, not a self-compare.)
    if (PersonGetWord(fromPtr, guild::sim::kPfFamilyWord)
            != PersonGetWord(toPtr, guild::sim::kPfFamilyWord))
        PersonSetWord(toPtr, guild::sim::kPfFamilyWord,
                      PersonGetWord(fromPtr, guild::sim::kPfFamilyWord));

    // 0x58c5b1: drop the FROM person's office holdings.
    H.officeReleaseHoldings(fromPtr);

    // ----- build working image v34 (the NEW image of the TO record) -----------
    // 0x58c5bd: qmemcpy(v34, v35, 0x218) — start from the full TO record.
    Person v34;
    std::memcpy(&v34, toPtr, sizeof(Person));
    // 0x58c5c2: v34[1] (record+4 id dword) = FROM +4 id.
    PersonSetDword(&v34, kOffDword1, PersonGetDword(fromPtr, kOffDword1));
    // 0x58c5d3: LOWORD(v34[0]) = FROM marker.
    PersonSetWord(&v34, kOffMarker, fromMarker);
    // 0x58c5e5: BYTE2(v34[0]) = FROM kind byte (record+2).
    PersonSetByte(&v34, kOffByte2, PersonGetByte(fromPtr, kOffByte2));
    // 0x58c5f6: BYTE1(v34[3]) = FROM record+13.
    PersonSetByte(&v34, kOffByte13, PersonGetByte(fromPtr, kOffByte13));
    // 0x58c60a: v34[92] (record+0x170) = FROM record+0x170 dword.
    PersonSetDword(&v34, kOffRelation170, PersonGetDword(fromPtr, kOffRelation170));

    // 0x58c621: office-rank / family-head reconciliation. Default (LABEL_34) sets
    // LOBYTE(v34[89]) = FROM record+0x164. If the TO record belongs to a building
    // group AND a family-head record + a category resolve, and the FROM building's
    // rank is below the TO building's rank, the family-head byte is written into
    // the FROM family record instead.
    guild::u8 toGroupCode = PersonGetByte(toPtr, kOff353Hi); // HIBYTE(*(rec+353))
    bool wroteFamilyHead = false;                            // took the LABEL_14 path
    if (H.buildingTypeGroupFromCode(toGroupCode)) {
        // 0x58c63c: FROM family record (modeled via FindRecordById on the family
        // word). 0x58c653: category for FROM marker.
        Person* famRec = PersonFindRecordById(PersonGetWord(fromPtr, guild::sim::kPfFamilyWord));
        guild::u8 cat = famRec ? H.buildingGetCategoryForObject(fromMarker) : 0;
        if (famRec && cat) {
            // 0x58c65f / 0x58c676: rank(FROM cat) vs rank(TO group code). The
            // original calls ComputeRankWithinGroup EXACTLY TWICE — first on `cat`
            // (result KEPT in edx as v19=fromRank), then on the TO group code
            // (eax=v17=toRank) — then `cmp edx,eax; jge` -> writes when fromRank <
            // toRank. (Hex-Rays printed a spurious third call; disasm 0x58c65c..
            // 0x58c67d shows only two, with the first result retained.)
            int fromRank = H.buildingTypeRankWithinGroup(cat);        // 0x58c65f (v19)
            int toRank   = H.buildingTypeRankWithinGroup(toGroupCode);// 0x58c676 (v17)
            if (fromRank < toRank) {
                // 0x58c690: write TO record+356 into FROM family record at a byte.
                PersonSetByte(famRec, /*+112*/ 112,
                              PersonGetByte(toPtr, kOffByte356));
                wroteFamilyHead = true;                       // -> LABEL_14
            }
        }
    }
    if (!wroteFamilyHead) {
        // LABEL_34 (0x58c8d7): LOBYTE(v34[89]) = FROM record+0x164.
        PersonSetByte(&v34, kOffByte89, PersonGetByte(fromPtr, kOffByte356));
    }

    // 0x58c69f: if the FROM kind byte (record+2) == 5, set the drop flag (v38=1).
    bool dropFlag = (PersonGetByte(fromPtr, kOffByte2) == 5);

    // ----- build working image v33 (the NEW image of the FROM record) ---------
    // 0x58c6c3: qmemcpy(v33, v40, 0x218) — start from the full FROM record.
    Person v33;
    std::memcpy(&v33, fromPtr, sizeof(Person));
    // 0x58c6ca: v33[1] (record+4 id) = TO +4 id.
    PersonSetDword(&v33, kOffDword1, PersonGetDword(toPtr, kOffDword1));
    // 0x58c6d5: BYTE2(v33[0]) = 9 (kind byte forced to 9).
    PersonSetByte(&v33, kOffByte2, 9);
    // 0x58c6e6: LOWORD(v33[0]) = TO marker.
    const guild::i16 toMarker = toPtr->marker;        // *v35
    PersonSetWord(&v33, kOffMarker, toMarker);

    // ----- commit both images back into the person array ----------------------
    // 0x58c724: copy v34 (NEW TO image) into the FROM slot (word_12CE910[268*fromSlot]).
    std::memcpy(&g_persons[fromSlot], &v34, sizeof(Person));
    // 0x58c73f: copy v33 (NEW FROM image) into the TO slot (word_12CE910[268*toMarker]).
    const guild::u16 toSlot = static_cast<guild::u16>(toMarker); // v12
    std::memcpy(&g_persons[toSlot], &v33, sizeof(Person));

    // Keep the parallel id column (dword_12CE914) in lockstep with the records, as
    // the original holds it: the id dword lives at record+4, which the splices set.
    guild::sim::g_personIds[fromSlot] = g_persons[fromSlot].id;
    guild::sim::g_personIds[toSlot]   = g_persons[toSlot].id;

    // 0x58c749 / 0x58c786: relink the per-person object lists (dword_12CEA88).
    // Each is a singly linked list (next ptr @+63) of objects whose +6 field is the
    // owning person's id (dword_12CE914). The original re-stamps every node's +6 to
    // the NEW owner id of that slot. Modeled via the inventory-move hook semantics:
    // there is no object-list array in this model, so this is a no-op here and is
    // surfaced to the wiring layer (see header note). Faithful control flow only.

    // 0x58c7ab: re-point the whole relation matrix (dword_12CE96C). For every
    // person k (0..767) and every relation slot m (0..7): if the slot referenced
    // the FROM record (v23 == fromId), point it at the TO id; else if it referenced
    // the TO id, point it at the FROM id (the swap is symmetric).
    const guild::i32 fromIdVal = fromId;              // v23 = v39 (the FROM id)
    const guild::i32 toIdVal   = toRec;               // a2   (the TO id)
    for (int k = 0; k < guild::sim::kPersonCapacity; ++k) {     // k < 0x300
        for (int m = 0; m < 8; ++m) {                          // m < 8
            const int off = guild::sim::kPfRelationBase + 4 * m; // record+0x5C + 4*m
            guild::i32 slot = PersonGetDword(&g_persons[k], off);
            if (slot == fromIdVal) {
                PersonSetDword(&g_persons[k], off, toIdVal);   // 0x58c7d3
            } else if (slot == toIdVal) {
                PersonSetDword(&g_persons[k], off, fromIdVal); // 0x58c8eb
            }
        }
    }

    // 0x58c7f1: move the FROM person's "category 9" inventory objects to the TO
    // container. The original walks v33[94] (the FROM image's object-list head, a
    // linked list with next @+63) and, for each node whose type-def byte (+0 of the
    // dword_13CE27C type table) is 9, calls VIBE_GameObject_AddObjektToParent. With
    // no object-list array in this model the walk is empty; the leaf is inert by
    // default. Faithful: the inventory-move hook would be driven by the live list.
    (void)H.gameObjectAddToParent;

    // 0x58c81a..0x58c83f: stamp the wealth/jail bookkeeping for the FROM slot.
    //   dword_12CEAD8[2*67*slot] = 0;            (record+0x1C8 turn-bit dword)
    //   dword_12CEAB8[2*67*slot] = snapWealth;   (record+0x1A8 wealth dword)
    //   dword_12CEAA4[2*67*slot] = 4;            (record+0x194 status dword)
    // These map to per-record dwords; 67 dwords == 268 bytes == half a 536 record,
    // and "2*67*slot" indexes the dword view of the FULL record. We write them by
    // raw record offset into the (now NEW-TO) FROM slot.
    PersonSetDword(&g_persons[fromSlot], 0x1C8, 0);          // dword_12CEAD8
    PersonSetDword(&g_persons[fromSlot], 0x1A8, snapWealth); // dword_12CEAB8
    PersonSetDword(&g_persons[fromSlot], 0x194, 4);          // dword_12CEAA4

    // 0x58c859: remove + clean the spliced building record (LOWORD(v33[0]) == TO
    // marker), dropping its contents when the FROM-was-a-corpse flag is set.
    H.buildingRemoveAndCleanup(toMarker, dropFlag);

    // 0x58c4d9: return the FROM slot index.
    return static_cast<int>(fromSlot);
}

} // namespace guild::world
