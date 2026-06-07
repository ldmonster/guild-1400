#pragma once
// ===========================================================================
// person_personnel2.h — second batch of Person/Personnel query, wealth, and
// staff-book leaves from gilde.exe (namespace guild::sim).
// ===========================================================================
// These are untranslated leaves of the VIBE_Person_* / VIBE_Personnel_* families
// that operate against the 536-byte Person/NPC record (word_12CE910 @0x12CE910,
// stride 536, 768 slots; see sim/types.h). The originals are __usercall with
// register args; we expose normal C++ signatures and document the register
// mapping per function.
//
// Cross-module callees that are NOT reconstructed in src/ (scene-graph queries,
// market-price lookup, coord rounding, dialog open, widget add/destroy, the
// distance table, and the staff/mercenary book table) are routed through an
// installable hooks struct (PersonPersonnel2Hooks) with inert default
// implementations defined in person_personnel2.cpp. Tests install their own
// hooks; the library never depends on a symbol defined only in a test.
//
// Translated functions (address — original symbol):
//   0x5920b0  VIBE_Person_FindActiveByEntity
//   0x5949fc  VIBE_Person_FindNearestByDistance
//   0x58d95c  VIBE_Person_FindEmploymentRelation
//   0x591328  VIBE_Person_ComputeAssetWorth
//   0x591ff0  VIBE_Person_CheckDebtRatioCritical
//   0x594c94  VIBE_Person_SyncMasterShopObjects
//   0x59630c  VIBE_Person_BeginQueryThenSetField270
//   0x59647c  VIBE_Person_BeginQueryThenOpenBuilding
//   0x596514  VIBE_Person_BeginQueryThenOpenBuildingAlt
//   0x53b5d0  VIBE_Personnel_BuildBookRow
//   0x53ba4c  VIBE_Personnel_DestroyBookRowWidgets
#include "guild/common/types.h"
#include "sim/types.h"   // Person (536), kPersonStride, kPersonCapacity

namespace guild::sim {

// ---------------------------------------------------------------------------
// Additional Person-record offsets these leaves reach (byte offsets into the
// 536-byte record, additive to the field maps in types.h / person_record.h).
// Recovered from the decompiled accessors below.
// ---------------------------------------------------------------------------
enum PersonField2 : int {
    // FindActiveByEntity (0x5920b0):
    kPf2EntityAlive   = 0x164,  // byte_12CEA74[v*4]  alive/active flag byte (+356)
    kPf2EntityPtr     = 0x16C,  // dword_12CEA7C[v]   bound scene-entity id/ptr (+364)
    // FindEmploymentRelation (0x58d95c) — 8-entry employer/relation id array:
    kPf2RelArray      = 0x5C,   // dword[23..30]: relation/employer person ids (+92)
    // ComputeAssetWorth / wealth:
    kPf2FamilyWord    = 0x27,   // *(WORD*)(rec+39) family/household slot (0xFFFF none)
    kPf2ContainerId   = 0x5D,   // *(DWORD*)(rec+93) bound container/scene id
};

// Staff/mercenary book record (gilde.exe dword_11BC772 @0x11BC772, stride 45,
// 512 records). FindEmploymentRelation's overflow fallback scans this when the
// active-person array is full. The original reads only two columns:
//   +0x00 (dword_11BC772)  person id of the booked staff/mercenary
//   +0x13 (dword_11BC785)  "active/valid" flag (nonzero == live booking)
// (+0x13 == 19; see decompile.) We model just those two columns.
struct StaffBookRecord {
    i32 personId;   // +0x00
    u8  pad4[15];   // +0x04..+0x12
    i32 active;     // +0x13  (unaligned in the original; nonzero == live)
};

// ---------------------------------------------------------------------------
// Cross-module hooks (inert defaults defined in person_personnel2.cpp).
// ---------------------------------------------------------------------------
struct PersonPersonnel2Hooks {
    virtual ~PersonPersonnel2Hooks() = default;

    // gilde.exe *(char*)(dword_123D6CD + base + 3): per-person distance scalar
    // table read by FindNearestByDistance. `distBase` is the function argument
    // (a1) advanced by 768 each slot; the original reads byte at base+a1+3.
    // We expose the distance for slot `idx` (0..767). Default: large (never wins).
    virtual int NearestDistance(int idx, int distBase) {
        (void)idx; (void)distBase; return 0x7fffffff;
    }

    // Staff/mercenary book overflow scan (FindEmploymentRelation @0x58d9d6..).
    // `liveCount` is dword_647724 (current live person count). The original only
    // takes this slow path when (768 - liveCount) < 32. Returns the book base
    // (nullptr disables the scan -> the fast path's "return 1" is used) and the
    // record count. Default: empty book.
    virtual const StaffBookRecord* StaffBook(int* outCount) {
        if (outCount) *outCount = 0;
        return nullptr;
    }

    // gilde.exe VIBE_Person_SumCurrencyHeld(rec) (0x59152c): sum of the person's
    // held currency stacks. Routed as a hook here because the reconstructed
    // PersonSumCurrencyHeld takes a ContainerView, not a raw record. Default 0.
    virtual int SumCurrencyHeld(const Person* rec) { (void)rec; return 0; }

    // gilde.exe VIBE_Person_ComputeTotalWealth(*rec, rec) (0x591f7c): total
    // wealth (currency + owned-building worth) for `rec`. Default 0.
    virtual int ComputeTotalWealth(const Person* rec) { (void)rec; return 0; }

    // ComputeAssetWorth owned-building worth term: for the person's bound
    // container, sum of VIBE_Building_LookupCachedMarketPrice over each room/good
    // (truncated to int, accumulated as the original does in double then (int)).
    // `rec` is the person record. Default 0 (no buildings).
    virtual int OwnedBuildingWorth(const Person* rec) { (void)rec; return 0; }

    // VIBE_Dialog_OpenBuildingForActiveChar(record, byte_626DA0) (0x4adef4):
    // opens the building dialog for the queried character. Returns the low byte
    // the caller propagates. Default 0.
    virtual u8 OpenBuildingForActiveChar(Person* rec) { (void)rec; return 0; }

    // VIBE_Person_QueryBegin/IterNext family used by SyncMasterShopObjects and
    // the BeginQueryThen* leaves. We expose the resolved first match as a record
    // pointer (nullptr == no match). `filterTag` distinguishes the call sites:
    //   30 == SyncMasterShopObjects, 15 == SetField270, 10 == OpenBuilding.
    // Default: no match.
    virtual Person* QueryFirst(int slotArg, int filterTag) {
        (void)slotArg; (void)filterTag; return nullptr;
    }

    // SyncMasterShopObjects body: for the matched master record, propagate the
    // shop's owner field to its child shop objects. The whole scene-walk is
    // hooked; default does nothing. Returns the master record (echoed as the
    // original returns the loop pointer, here always the matched record or null).
    virtual void SyncShopChildren(Person* master) { (void)master; }

    // Personnel book-row widget primitives (BuildBookRow / DestroyBookRowWidgets).
    // AddWidget: VIBE_Object_AddToWindow / AddTextLabel — returns a widget handle
    //   (>=0) or -1. DestroyWidget: VIBE_Widget_DestroyByType. Defaults: -1 / noop.
    virtual int AddWidget(int kind, int x, int y) { (void)kind; (void)x; (void)y; return -1; }
    virtual void DestroyWidget(int handle) { (void)handle; }
};

void SetPersonPersonnel2Hooks(PersonPersonnel2Hooks* hooks);
PersonPersonnel2Hooks* PersonPersonnel2HooksPtr();

// ---------------------------------------------------------------------------
// Personnel book-row record (gilde.exe stack/heap struct the *BookRow functions
// build; addressed by byte offset a3+N). Only the columns the two translated
// functions touch are modeled; offsets are byte offsets into the row record.
//   +0x00 winX (a1), +0x04 winY (a2)
//   +0x08 personId/state, +0x0C window widget, +0x10 slider widget,
//   +0x14 (label A area), +0x18 label widgets..  We name the fields used.
// ---------------------------------------------------------------------------
struct PersonnelBookRow {
    i32 winX;        // +0x00  (a1)
    i32 winY;        // +0x04  (a2)
    i32 state;       // +0x08  cleared to 0 by BuildBookRow
    i32 window;      // +0x0C  window widget handle (AddToWindow result)
    i32 slider;      // +0x10  slider widget handle (-1 == none)
    i32 labelA;      // +0x14  text label A handle
    i32 form;        // +0x18  unused-by-us form ptr region start
    i32 labelB;      // +0x18 alias kept for clarity (see BuildBookRow: +24)
    i32 extraStart;  // sentinel array start (+32/+56 set to -1)
    i32 extraEnd;
    u8  flag33;      // +0x21  (a3+33) cleared by BuildBookRow
};

// gilde.exe 0x53b5d0 — VIBE_Personnel_BuildBookRow (__usercall, eax=(x@eax),
// edx=(y@edx), ebx=(row@ebx)). Initializes a book row's window + two text-label
// widgets (via AddWidget hook) and resets its handle/sentinel fields. Returns
// the row pointer advanced past the cleared sentinel region (matches the
// original's `result` walk to a3+24).
PersonnelBookRow* PersonnelBuildBookRow(int x, int y, PersonnelBookRow* row);

// gilde.exe 0x53ba4c — VIBE_Personnel_DestroyBookRowWidgets (__usercall,
// eax=(row@eax), ebx=(a2@ebx)). Destroys the row's window/slider/extra widgets
// (via DestroyWidget hook) and resets their handles to -1. Returns the last
// destroy result (0 if none destroyed).
int PersonnelDestroyBookRowWidgets(PersonnelBookRow* row);

// ---------------------------------------------------------------------------
// Person query/scan leaves.
// ---------------------------------------------------------------------------

// gilde.exe 0x5920b0 — VIBE_Person_FindActiveByEntity (__usercall, eax=(entityId
// @eax, treated as the bound-entity column value)). Scans the person array for
// the active slot whose bound-entity column (+364) equals `entityId` and whose
// kind byte (+2) is one of {1,2,6,7}; returns that record (nullptr if none, or
// if entityId is 0). Operates on g_persons.
Person* PersonFindActiveByEntity(i32 entityId);

// gilde.exe 0x5949fc — VIBE_Person_FindNearestByDistance (__usercall,
// eax=(distBase@eax)). Scans the 768 person slots; for each occupied slot
// (marker != -1) reads the distance scalar via the NearestDistance hook and
// returns the slot index with the minimum distance (-1 if none occupied).
int PersonFindNearestByDistance(int distBase);

// gilde.exe 0x58d95c — VIBE_Person_FindEmploymentRelation (__usercall,
// eax=(rec@eax person record ptr)). Finds an employer/relation match for `rec`:
//   * returns 0 if rec's kind (+2) is 6 or 7;
//   * scans the person array for a record whose id matches one of rec's three
//     primary relation ids (+92/+96/+100) OR whose extended relation array
//     (+104..+120) contains rec's id (with <5 valid entries) — returns 1 on the
//     first such structural match found via the relation-array branch;
//   * on overflow (book nearly full) falls back to the staff book scan.
// Returns 0 (no employer / direct relation), 1 (employer/relation found), per
// the original's 0/1 result. (The original also returns XOR-derived nonzero
// sentinels on the relation-array branch; those reduce to "found" == 1 here,
// documented inline.)
int PersonFindEmploymentRelation(Person* rec);

// gilde.exe 0x591328 — VIBE_Person_ComputeAssetWorth (__usercall, eax=(rec@eax),
// edx=(includeBuildings@edx)). Currency held (if family slot valid and the
// family kind byte < 10) plus, when includeBuildings, the owned-building market
// worth. Returns the integer total.
int PersonComputeAssetWorth(Person* rec, int includeBuildings);

// gilde.exe 0x591ff0 — VIBE_Person_CheckDebtRatioCritical (__usercall,
// eax=(rec@eax)). True if the person is insolvent past the kind-dependent debt
// ratio threshold (0.07 for kind 6/7, else 0.20). Net = currency + reserve; if
// net > 0 returns false; else ratio = |net| / (totalWealth - net) compared to
// the threshold.
bool PersonCheckDebtRatioCritical(Person* rec, int reserve);

// gilde.exe 0x594c94 — VIBE_Person_SyncMasterShopObjects (__usercall,
// esi=(slotArg@esi)). For each "master" record matching the query (filterTag 30)
// propagates its shop owner to child shop objects (via SyncShopChildren hook).
void PersonSyncMasterShopObjects(int slotArg);

// gilde.exe 0x59630c — VIBE_Person_BeginQueryThenSetField270 (__usercall,
// eax=(cur@eax), esi=(slotArg@esi)). If `cur` is null (low byte 0), runs the
// query (filterTag 15); on a match sets the record's +41 word to 270 and returns
// it. Returns the (possibly new) record pointer.
Person* PersonBeginQueryThenSetField270(Person* cur, int slotArg);

// gilde.exe 0x59647c / 0x596514 — VIBE_Person_BeginQueryThenOpenBuilding[Alt]
// (__usercall, edx:eax=(state), esi=(slotArg@esi)). If the low byte of `state`
// is 1, runs the query (filterTag 10); on a match opens the building dialog for
// the matched char and returns its low byte. Returns the propagated low byte.
// The two addresses are byte-identical; one implementation, both listed.
u8 PersonBeginQueryThenOpenBuilding(u8 stateLow, int slotArg);

}  // namespace guild::sim
