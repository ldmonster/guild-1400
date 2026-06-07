#pragma once
// AI person-favorability scoring for the Guild simulation (gilde.exe).
//
//   VIBE_Ai_ComputePersonFavorability   0x594330  (48 call sites across the game)
//   VIBE_Ai_AverageObjectFavorability   0x594928  (5 call sites)
//
// ComputePersonFavorability(self, other, applyLaw) returns a 0..100 "how much
// person `self` favors person `other`" scalar that drives recruitment cost,
// trials, bank loans, church/office relations and many social interactions.
//
// The binary reads a dense set of per-person record fields and several office /
// inventory / law leaf functions, all spread across strided globals
// (word_12CE910 stride 536, dword_12CE919 stride 134, ...). To keep the SCORING
// RULES — the load-bearing part — faithful AND testable, the per-person data and
// the leaf-function results are supplied through a small environment interface
// (FavorabilityEnv). A production backend binds the interface to the real globals
// (sim::*); tests bind a deterministic mock. The arithmetic, gating, law modifier
// and clamps below are reproduced 1:1 from the pseudocode.
//
// Recovered constants (gilde.exe .rdata):
//   flt_626AA4 = 0.003921568859 (= 1/255)   relation-byte normaliser
//   flt_626AA8 = 100.0                       clamp ceiling
//   flt_626AAC = 0.75                        gesetz==2 office-weight scale
//   flt_626AB0 = 7.0   flt_626AB4 = 15.0     inventory bonuses
//   flt_626AB8 = 10.0  flt_626ABC = 8.0      inventory bonuses
//   dbl_626AC4 = 0.01                        average -> 0..1 scale
#include "guild/common/types.h"

namespace guild::ai {

// Office definition triple (VIBE_Office_GetDefinition fills 3 dwords). The
// favorability fn only reads three sub-fields of it:
//   BYTE1(def[0])  office "kind" code (==4 gates a worker-count bonus)
//   BYTE2(def[0])  office "tier" code (1..3 / 4..7 / 8..9 select item-id sets)
//   def[2] as float  office relation weight (v27, law-scaled, +/- by faction)
struct OfficeDefinition {
    u8    kind = 0;    // BYTE1(def[0])
    u8    tier = 0;    // BYTE2(def[0])
    float weight = 0.0f;  // *(float*)&def[2]
};

// Per-person fields the scorer reads for a given person id. Field provenance
// (all strided global reads in the binary) noted per member.
struct FavPersonFields {
    int  workstationBuildingPtr = 0;  // dword_12CEA80[134*id]  (0 => no building)
    u8   officeId = 0;                // byte_12CEA76[536*id]
    u8   titleId = 0;                 // byte_12CEA79[536*id]  (30..33 special)
    int  relationByteSelf = 0;        // (dword_123D6CD[192*self]+other)>>24 — signed
    int  inventoryBase = 0;           // &word_12CE910[268*id] base for slot probes
    int  factionHigh = 0;             // dword_12CE919[134*id]>>24
    int  guildBitsLow = 0;            // dword_12CE93C[134*id]  (0x1C000 / 0x1800000 bits)
    int  rankHigh = 0;                // (dword_12CE914[134*id]+2)>>24 (BYTE-shift)
    int  spouseRecordPtr = 0;         // dword_12CEA7C[134*id]  (0 => unmarried)
    int  spousePartnerId = 0;         // *(u16*)(spouseRecordPtr+39)
};

// Environment the favorability scorer runs against. A null hook for any optional
// query returns the documented neutral default. The production backend wires
// these to the sim globals; tests use a deterministic mock.
struct FavorabilityEnv {
    virtual ~FavorabilityEnv() = default;

    // Per-person record fields (above).
    virtual FavPersonFields Person(int personId) = 0;

    // VIBE_Office_GetDefinition(officeId).
    virtual OfficeDefinition Office(u8 officeId) = 0;

    // VIBE_Building_SumWorkstationByCategory(buildingPtr, 4, 1): worker count.
    virtual int WorkstationWorkers(int buildingPtr) = 0;

    // VIBE_Person_QueryByGoodType(1, otherId): a building ptr (0 => none) used for
    // the office.kind==4 bonus.
    virtual int QueryByGoodType(int otherId) = 0;

    // VIBE_Person_QueryBegin(otherId, 1, 5, mappedTitle): a record ptr; returns
    // {ptr, gateOk} where gateOk == ((rec[90] & 1) == 0). ptr==0 => skip.
    virtual int QueryBeginWorkers(int otherId, int mappedTitle, bool& gateOk) = 0;

    // VIBE_Gesetz_GetRecord(2): the law state byte (==2 scales the office weight).
    virtual int GesetzState() = 0;

    // VIBE_Inventory_IsObjectSlotActive(inventoryBase, itemId): 0/1 per probe.
    virtual int InventorySlot(int inventoryBase, int itemId) = 0;
};

// gilde.exe 0x594330 — VIBE_Ai_ComputePersonFavorability
//   (__usercall: st0=ret, eax=self, edx=other, ebx=applyLaw)
// Returns a favorability score, clamped to [0, 100]; 100 when self==other.
double ComputePersonFavorability(int self, int other, bool applyLaw,
                                 FavorabilityEnv& env);

// gilde.exe 0x594928 — VIBE_Ai_AverageObjectFavorability
//   (__usercall: st0=ret, eax=self, edx=count, ebx=ids)
// Averages ComputePersonFavorability(self, id, applyLaw=false) over the `count`
// ids in `ids` (skipping -1 and ids with no resolvable record), scaled by 0.01;
// returns 0.5 when no id resolved. `resolve` maps a raw id to a person id (the
// VIBE_Person_FindRecordById step) or returns -1 if unresolved.
double AverageObjectFavorability(int self, const int* ids, int count,
                                 FavorabilityEnv& env,
                                 int (*resolve)(int rawId));

} // namespace guild::ai
