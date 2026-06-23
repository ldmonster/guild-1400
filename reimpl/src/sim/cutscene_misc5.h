#pragma once
// ===========================================================================
// cutscene_misc5.{h,cpp} — the VIBE_Scene_* "scene activation / world-sync"
// slice (gilde.exe, namespace guild::sim).
// ===========================================================================
//
// Wave 14 slice. The VIBE_Cutscene_* prefix is FULLY EXHAUSTED — all 75
// functions are already reconstructed across cutscene.cpp / cutscene_misc.cpp
// .. cutscene_misc4.cpp / cutscene_process.cpp / cutscene_{duel,wedding,
// auction}.cpp / gui/cutscene_build.cpp (VIBE_Cutscene_LoadScene 0x4aa234).
// Confirmed by DEFINITION lines, not just provenance comments.
//
// The cutscene scene-loaders (Cutscene_LoadScene, the salon LoadScene hook,
// etc.) all forward into the adjacent VIBE_Scene_* family — the city/building/
// object scene activation + world-sync layer at 0x500218..0x506df4 (+ the
// 0x5e7e38 stream loader, whose header-parse portion is already in
// render/scene_load.cpp). That ENTIRE family was untranslated. This slice
// ports the deterministic scene-sync bodies 1:1.
//
// As with cutscene_misc4, the deterministic decision kernels are exposed as
// standalone testable free functions and the full control flow is driven over
// an installable hook surface (SceneSyncHooks, inert defaults in the .cpp).
//
// RECOVERED TABLES / CONSTANTS / RECORD OFFSETS
//   * Building object record (the raw __int16*/char* the originals address):
//       +0   type byte (589*type indexes the type table dword_13CE294)
//       +1   id dword (object id used in command requests)
//       +2   objectKind byte (==23/37 select the output mood-noise table)
//       +39  season word (HIWORD selects ambience scene)
//       +44  staff/equipment bits (HIBYTE -> guard-target hi byte)
//       +93  universe handle (QueryFind root)
//       +97  building-data ptr (effects: chimney smoke, +530 visual-state byte)
//       +529 gate-object lo flag byte; +530 building visual-state byte
//       +533/+536 suspend / contact flags
//   * Building type byte 9 == "Bauplatz/site" (entrance: queue fee 310 + sell
//     500*rate cmd 15); types {4,16,19} == storage/no-fee. byte 11/12 ==
//     master-shop classes (SyncMeisterBuildings record scan).
//   * Category from Building_MapTypeToCategory: 1 (production w/ output) and 4
//     (special) drive ComputeProductionTickRate's price loop.
//   * dword_13CE27C — the 65-byte OBJECT type table (entrance/product records);
//     each entry's +46 word slots hold up-to-4 product type ids (0xFFFF == end).
//   * flt_620D3C == 16000.0f production base; 16000/totalCost or 16000.0/cost.
//   * Mood-noise bias dbl_620F1C / dbl_620F24 (object height jitter scaled by
//     flt_13C3B80[obj]). RetZero (0x5e9020) is a verbatim `return 0` thunk.
// ---------------------------------------------------------------------------
#include "guild/common/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Recovered constants.
// ---------------------------------------------------------------------------
// Building type byte values special-cased by the entrance sync.
constexpr u8 kBuildingTypeBauplatz = 9;   // site: queue build-fee 310 + sell cmd
// Storage/no-entrance-fee building types (skip the 308 fee + guard target).
constexpr u8 kBuildingTypeStore0 = 4;
constexpr u8 kBuildingTypeStore1 = 16;
constexpr u8 kBuildingTypeStore2 = 19;

// ComputeProductionTickRate categories that have a production loop.
constexpr u8 kCategoryProduction = 1;
constexpr u8 kCategorySpecial    = 4;

// Object record +2 "kind" values that select the high-noise mood table.
constexpr u8 kObjKindHighMood0 = 23;
constexpr u8 kObjKindHighMood1 = 37;

// Object type-table sentinel for "no product in slot".
constexpr u16 kProductSlotEnd = 0xFFFF;

// Production base numerator (flt_620D3C).
constexpr float kProductionBase = 16000.0f;

// SyncMeisterBuildings object record-byte classes.
constexpr u8 kMeisterClassA = 12;  // master-shop class (the v3/v4 anchor scan)
constexpr u8 kMeisterClassB = 11;  // secondary list class

// ---------------------------------------------------------------------------
// Hook surface — every cross-module side effect the scene-sync bodies make.
// Tests install a recording mock; the inert default (all null) makes every
// leaf a no-op, name-matches fail (so collectors are pass-through), and the
// frame pump returns 0 ("stop"), so every loop terminates deterministically.
// ---------------------------------------------------------------------------
struct SceneSyncHooks {
    // --- name / type classification --------------------------------------------
    // The prefix name-match used by the scene collectors (VIBE_Util_StrCmpNoCase,
    // entered mid-body at loc_5CB930): returns true (nonzero) on a match AND
    // yields the matched object pointer via *outObj. Inert default: no match.
    bool (*nameMatch)(int candidate, const char* prefix, void** outObj) = nullptr;
    // Building_MapTypeToCategory(typeByte) -> display category 0..8.
    int  (*mapTypeToCategory)(u8 typeByte) = nullptr;
    // Building_IsProductionType(rec) -> production-kind building?
    bool (*isProductionType)(const void* rec) = nullptr;

    // --- the scene stream load + traversal -------------------------------------
    // Scene_LoadFromStream(file): nonzero == loaded. Inert default 0 (no scene).
    int  (*loadFromStream)(const char* file) = nullptr;
    // SceneGraph_TraverseTree(root, mask): run a per-node pass. Records the mask.
    void (*traverseTree)(int mask) = nullptr;

    // --- command staging (world sync) ------------------------------------------
    // Command_QueueRequest17(objId, peer, count, productType, rate, flag).
    void (*queueRequest17)(i32 objId, i32 peer, int count, int productType) = nullptr;
    // Command_EnqueueCmd15(objId, peer, amount, rate) — the sell-on-site cmd.
    void (*enqueueCmd15)(i32 objId, i32 amount) = nullptr;
    // Money_MultiplyByRate(amount, rate) -> scaled amount.
    int  (*multiplyByRate)(int amount, u8 rate) = nullptr;
    // Inventory_GetSlotCapacity(obj) -> remaining capacity for the product slot.
    int  (*getSlotCapacity)(int obj) = nullptr;

    // --- effects / building visual state ---------------------------------------
    void (*spawnChimneySmoke)(int person, int a) = nullptr;   // Object_SpawnChimneySmoke
    void (*refreshAllLights)() = nullptr;                      // Light_RefreshAllObjects
    void (*reserveBauplatz)(int a2) = nullptr;                 // Building_ReserveBauplatzForActiveChar
    void (*buildTerrainMesh)() = nullptr;                      // Heightmap_BuildTerrainMesh
    void (*updateVisualState)(int obj, int mode) = nullptr;    // Object_UpdateBuildingVisualState
};

void SetSceneSyncHooks(const SceneSyncHooks* hooks);
const SceneSyncHooks& GetSceneSyncHooks();

// ===========================================================================
// DETERMINISTIC KERNELS (the testable cores).
// ===========================================================================

// gilde.exe 0x5e9020 — VIBE_Scene_RetZero. Verbatim `return 0` thunk passed as a
// node-callback in several traversals (a "collect nothing" predicate).
char SceneRetZero();

// gilde.exe 0x503678 — VIBE_Scene_CollectMatchingObject(obj, outList). The list
// header is a struct: count at +0, then up-to-512 ids. When the candidate's name
// matches (nameMatch with prefix at outList+513 in the original) the id is
// appended. Returns true while the list still has room (count < 512). We model
// the recovered append + capacity test directly; `matched` is the name-match
// result. `list[0]` is the count, `list[1..]` the ids.
//   returns: count < 512 (still has room).
bool SceneCollectMatchingObject(i32* list, int candidateId, bool matched);

// gilde.exe 0x504774 — VIBE_Scene_CollectTorchObject(obj, ctx). The torch list is
// over a record whose count lives at +128 (in DWORDs == +32 ids) and the ids at
// +0. On a name match ("ub_FACKEL_") the id is appended at [count] and count++.
// Returns true while count < 32. `count` is *(ctx+128) (in/out via the list).
//   list layout: list[32] == count, list[0..31] == ids.
//   returns: count < 32 (still has room).
bool SceneCollectTorchObject(i32* list, int candidateId, bool matched);

// gilde.exe 0x5048c4 — VIBE_Scene_FlagBuildingGate(rec, ctx). If the object's
// name does not start with "gb_" OR the name-match fails, leaves the record
// untouched. Otherwise clears the two visual bits (byte +530 & 0xF3) then sets
// bit 2 (|4) — i.e. "gated building, present gate mesh". Always returns 1.
// `gateByte` is the in/out +530 byte; `matched` is the "gb_" name-match.
char SceneFlagBuildingGate(u8* gateByte, bool nameStartsGb, bool matched);

// gilde.exe 0x505b3c — VIBE_Scene_FlagGateObject(obj, ...). On a "gate" name
// match: sets the +536 dword to 1 and clears bit 0 of the +529 byte (& 0xFE).
// On no match: sets +536 to 0. Returns the match result (nonzero == matched).
// `loByte` is the in/out +529 byte; `flag536` receives the +536 value.
int SceneFlagGateObject(bool matched, u8* loByte, i32* flag536);

// gilde.exe 0x504ce0 — VIBE_Scene_SyncMeisterBuildings record scan core. Walks
// the object record array `kinds[0..count)` (stride is conceptual here) and
// classifies the two anchors the original picks with OVERWRITE-until-both-found
// semantics: every class-A (12) record with sub-state 0 overwrites anchorA
// (v4 = rec; v5 |= 1) and every class-A record with sub-state 1 overwrites
// anchorB (v3 = rec; v5 |= 2) — unconditionally; the scan stops only once BOTH
// anchors have been seen (the `while (.. && v5 != 3)` guard), so the LAST
// qualifying match before that point wins and records after it never update an
// anchor. Then collects the indices of all class-B (11) records into `outB`.
// `subState[i]` is HIBYTE(dword_12CE919[i]) in the original.
// Returns the number of class-B records collected (>=0); writes the two anchor
// indices (or -1) into *anchorA / *anchorB.
int SceneClassifyMeisterRecords(const u8* kinds, const u8* subState, int count,
                                int* anchorA, int* anchorB,
                                int* outB, int outBCap);

// gilde.exe 0x502198 — VIBE_Scene_ComputeProductionTickRate cost core. For an
// object with up-to-4 product slots, the per-product cost sum is
//   sum += (int)(MarketPrice(productType, 100) * count)  for each non-0xFFFF slot
// (at most 4 slots, stop early on the first 0xFFFF after the first). When the sum
// is nonzero the per-tick rate is:
//   typeByte == 21 -> (int)(16000.0 / sum)   (the "special" salon rate)
//   else           -> 16000 / sum
// clamped to a floor of 1. Returns the clamped tick rate, or 0 when sum == 0.
// `prices[i]` is the resolved MarketPrice(productType,100); `counts[i]` the slot
// count word (+38); `valid[i]` whether the slot is present (slot+46 != 0xFFFF).
int SceneComputeProductionTickRate(const int* prices, const u16* counts,
                                   const bool* valid, int slotCount,
                                   bool isSalonType);

// ===========================================================================
// FULL FLOW DRIVERS (1:1 control flow over the hook surface).
// ===========================================================================

// gilde.exe 0x500218 — VIBE_Scene_LoadStadtScene(name@<eax>). Formats
// "scenes/*stadt_<name>.ed3", loads it via loadFromStream; on success runs the
// particle-emitter init traversal (mask 6) and returns 1. On failure returns 0.
char SceneLoadStadtScene(const char* name);

// gilde.exe 0x5023b8 — VIBE_Scene_SyncBuildingEntrance(rec). Dispatches on the
// building type byte: type 9 (Bauplatz) queues a build-fee (productType 310) and
// a sell-on-site cmd15 of 500*rate; types {4,16,19} are fee-exempt; everything
// else queues the standard entrance fee (productType 308). Then computes the
// production tick rate for the entrance. `typeByte` is the +0 building type;
// `objId` the +1 id; `rate` byte_6477A1; `gameMoney` the cmd15 base (500).
// Returns the productType queued for the entrance fee (310 / 308 / -1 exempt).
int SceneSyncBuildingEntrance(u8 typeByte, i32 objId, u8 rate);

// gilde.exe 0x504910 — VIBE_Scene_RefreshBuildingEffects(a1, a2). For each person
// with a building-data ptr (+97): clears +530 visual bits then sets bit 2, and
// spawns chimney smoke. Then runs the gate/torch traversals, refreshes lights,
// reserves the active char's Bauplatz, and rebuilds the terrain mesh. Driven over
// hooks; `personData[0..n)` is the list of building-data pointers (0 == none).
// Returns the count of chimney-smoke spawns issued.
int SceneRefreshBuildingEffects(const int* personData, int personCount, int a1);

}  // namespace guild::sim
