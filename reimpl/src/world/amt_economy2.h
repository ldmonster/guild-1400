#pragma once
// Amt (public-office) administration leaves — the office-slot / guild-rivalry /
// office-info family that VIBE_GameTick and the office panels drive. These are
// the *untranslated* leaves left over after amt.cpp / amt_enforcement.cpp /
// office_assign.cpp / guild_election.cpp took the economic + candidacy cores.
//
// Every one of these reaches into live game tables (the 24-byte office-holder
// table byte_B59848 @0xB59848, and the 536-byte person/building record table
// word_12CE910 @0x12CE910), and commits through the command/network queue. We
// keep the *control flow + arithmetic + constant tables* byte-for-byte and route
// the table lookups / mutations / RNG / coord-truncation / render plumbing
// through an installable hooks struct with inert defaults (defined in the .cpp),
// so the deterministic rules stay standalone-testable. This is the sanctioned
// cross-module pattern (cf. amt.h's AmtTransferHook, amt_enforcement.h's command
// hooks): a test installs its own hooks; src never references a test symbol.
//
// Translated (rules core):
//   VIBE_Amt_TriggerOfficeNotice           0x483570  (office-type slot scan)
//   VIBE_Amt_ResetGuildSlots               0x480b50  (two-phase slot reset)
//   VIBE_Amt_HighlightGuildMembers         0x48311c  (category dedup highlight)
//   VIBE_Amt_BuildOfficeInfoText           0x483414  (promotion-list message)
//   VIBE_Amt_ComputeOfficeRenderOffset     0x4834e4  (office-marker x-offset)
//   VIBE_Amt_FindNextActiveBuilding        0x57bb50  (cached richest-building)
//   VIBE_Amt_ComputeBuildingRivalryScore   0x57bc60  (rivalry roll + payout)
//   VIBE_City_LookupSelectionInfoText      0x507b18  (district info-text widen)
//   VIBE_Amt_OpenOfficeWindow              0x5546a0  (office-window entry wrap)
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"   // OfficeHolder (24-byte slot record), LawRecord

namespace guild::world {

// ===========================================================================
// Recovered tuning constants (get_bytes; little-endian float/double bit-exact).
// ===========================================================================
// VIBE_Amt_ComputeOfficeRenderOffset (0x4834e4):
//   flt_47DE10[4] — per-law-level horizontal-stretch factors (law #14's field
//   selects the index). Bits: 0x3F8CCCCD, 0x3F866666, 0x3F800000, 0x3F733333.
constexpr float kRenderStretch[4] = {1.10000002f, 1.04999995f, 1.0f, 0.949999988f};
//   flt_61AFC8 == 100.0f, flt_61AFCC == 32.0f — the marker pitch product.
constexpr float kRenderPitchA = 100.0f; // flt_61AFC8 (0x42C80000)
constexpr float kRenderPitchB = 32.0f;  // flt_61AFCC (0x42000000)
//   byte_62EC93[12*type] — per-office-type tile count (stride-12 struct, +0).
//   Recovered first 9 records: {0,5,7,10,5,7,10,5,7}. Index 0 is the "none" row.
constexpr u8 kOfficeTileCount[9] = {0, 5, 7, 10, 5, 7, 10, 5, 7};

// VIBE_Amt_ComputeBuildingRivalryScore (0x57bc60):
constexpr float  kRivalrySupplyScale   = 0.0125000002f; // flt_6258C4 (0x3C4CCCCD)
constexpr float  kRivalryDistanceScale = 0.25f;         // flt_6258C8 (0x3E800000)
constexpr double kRivalrySameCity      = 0.6;           // dbl_6258CC
constexpr double kRivalryAdjacentCity  = 0.7;           // dbl_6258D4
constexpr double kRivalryFarCity       = 0.8;           // dbl_6258DC
constexpr float  kRivalryRollScale     = 2.0f;          // flt_6258E4
constexpr float  kRivalryWealthScale   = 160.0f;        // flt_6258E8
constexpr float  kRivalryPayoutScale   = 10.0f;         // flt_6258EC
// The +0.05 / -0.05 reputation delta queued as field 460 (float bit literals
// 1028443341 / -1119040307), gated by the rep band (0.05, 0.95).
constexpr float kRivalryRepUp    = 0.0500000007f;  // 1028443341
constexpr float kRivalryRepDown  = -0.0500000007f; // -1119040307
constexpr float kRivalryRepLow   = 0.0500000007f;  // 1028443341 (> gate)
constexpr float kRivalryRepHigh  = 0.949999988f;   // 1064514355 (< gate)

// Slot-walk bounds (the originals walk 30 records, stride 24, byte index < 720).
constexpr int kSlotByteSpan = 720; // 30 * 24

// ===========================================================================
// Installable hooks (table lookups / mutations / RNG / coord). Defaults inert.
// ===========================================================================
struct AmtEconomy2Hooks {
    // VIBE_Office_AddTableEntry(holderKey, primaryRec, state, succ, flag) -> id.
    // ResetGuildSlots / TriggerOfficeNotice queue a slot (re)assignment. Default
    // returns -1 (nothing queued).
    int (*officeAddTableEntry)(u8 holderKey, i32 primaryId, int state,
                               int succ, int flag) = nullptr;

    // VIBE_Person_FindRecordById(id) -> opaque handle (non-null == found). The
    // reset pass consults two record bytes: present(+8) and dirty(+358). The
    // default reports "not found".
    bool (*personFind)(i32 id, bool* outPresent, bool* outDirty) = nullptr;

    // The delta-packet commit (BeginDeltaPacket + AppendDeltaField + QueueState22)
    // ResetGuildSlots fires for dirty records (field 0x166) and dirty buildings
    // (field 0x168). Args: (recordId, fieldId).
    void (*queueDeltaFlag)(i32 recordId, int fieldId) = nullptr;

    // VIBE_Command_QueueRequestCoord27(holderId, otherId, color) — highlight /
    // marker command (HighlightGuildMembers, rivalry payout coord).
    void (*queueCoord27)(i32 holderId, i32 otherId, int arg) = nullptr;

    // VIBE_Command_QueueRequest16(payer, recipient, amount, currency) — the money
    // payout in ComputeBuildingRivalryScore.
    void (*queueRequest16)(i32 payer, i32 recipient, i32 amount, int currency) = nullptr;

    // VIBE_Command_QueueRequestArgs26(personId, field, floatBits) — the rivalry
    // reputation delta (field 460).
    void (*queueArgs26)(i32 personId, int field, float value) = nullptr;

    // VIBE_He_SendEntityMessage(holderId, ...) — the office-info text message.
    void (*sendEntityMessage)(i32 holderId, const char* text) = nullptr;

    // VIBE_Math_RandomFloatScaled() — the rivalry roll. Default deterministic 0.
    float (*randFloatScaled)() = nullptr;

    // VIBE_Coord_ConvertX() == round-toward-zero of the FPU st0 the caller just
    // pushed. We surface it as an explicit truncation so the marker math matches.
    // Default = C++ (int) truncation.
    i32 (*truncate)(double v) = nullptr;
};

// Install / fetch the active hook table (process-global, like the other modules).
void AmtEconomy2SetHooks(const AmtEconomy2Hooks& hooks);
const AmtEconomy2Hooks& AmtEconomy2GetHooks();
void AmtEconomy2ResetHooks(); // restore inert defaults

// ===========================================================================
// VIBE_Amt_TriggerOfficeNotice (0x483570) — find the first office slot whose
// type byte (+8) matches `officeType` and (re)post it as vacant/electable
// (state 3) via officeAddTableEntry. Returns the AddTableEntry result, or the
// terminal byte-index (>= 720) when no slot matches.
i32 TriggerOfficeNotice(u8 officeType, const OfficeHolder* slots, int slotCount);

// ===========================================================================
// VIBE_Amt_ResetGuildSlots (0x480b50) — the per-game-reset office-slot rebuild.
// For each of the 30 office slots: if the holder record is present+active and the
// slot state isn't already 1, queue an assignment (state 1); otherwise, if the
// slot state isn't already 3, queue it vacant (state 3); a dirty holder record
// also gets a delta-flag commit (field 0x166). A separate pass commits a delta
// flag (field 0x168) for every dirty building. Returns the number of slot
// assignments queued (a tally; the original returns the last command handle).
struct ResetGuildSlotsResult {
    int slotAssignsQueued = 0; // state-1 AddTableEntry calls
    int slotVacanciesQueued = 0; // state-3 AddTableEntry calls
    int holderFlagsCommitted = 0; // field 0x166 delta packets
    int buildingFlagsCommitted = 0; // field 0x168 delta packets
};
ResetGuildSlotsResult ResetGuildSlots(const OfficeHolder* slots, int slotCount,
                                      const bool* buildingDirty, int buildingCount);

// ===========================================================================
// VIBE_Amt_HighlightGuildMembers (0x48311c) — for office category `category`
// (1..7), collect up to `collected` holders (provided by the caller as the
// resolved id list) and queue a highlight (Coord27) toward each holder id that
// differs from `selfId` and isn't -1. Returns the number of highlights queued.
int HighlightGuildMembers(i32 selfId, u8 category, int arg,
                          const i32* holderIds, int collected);

// ===========================================================================
// VIBE_Amt_BuildOfficeInfoText (0x483414) — when the holder's rank byte (+13) is
// >= 2 and a promotion list exists, build the "$A"-joined list message and send
// it. `promotionCount` is the BuildPromotionList result; `entryText` resolves a
// promotion entry index to its display string. Returns false when the rank gate
// fails or the list is empty; true when a message is sent.
bool BuildOfficeInfoText(i32 holderId, u8 rankByte, int promotionCount,
                         const u8* entryIndices,
                         const char* (*entryText)(u8 index, void* ctx), void* ctx);

// ===========================================================================
// VIBE_Amt_ComputeOfficeRenderOffset (0x4834e4) — compute the accumulated marker
// x-offset for the office row. Law #14's stretch factor (kRenderStretch) scales a
// per-occupied-slot pitch (tileCount * kRenderPitchA * kRenderPitchB). `lawLevel`
// is law #14's selector field; each occupied slot (city != -1) of the given type
// contributes one pitch step. Returns the final x (matching Coord_ConvertX
// truncation via the truncate hook).
i32 ComputeOfficeRenderOffset(int baseX, u8 lawLevel,
                              const OfficeHolder* slots, int slotCount);

// ===========================================================================
// VIBE_Amt_FindNextActiveBuilding (0x57bb50) — cached scan for the richest active
// production building. A per-call cache (dword_641FE4/E0, generation dword_641FDC)
// is revalidated against `generation`; if stale (or the cached building is gone)
// the 768-record table is rescanned for the max-wealth building among active
// (type < 10) production rooms. Writes the chosen building's id (or -1) and its
// wealth. Returns 1 when a building was found, 0 otherwise.
struct BuildingScanEntry {
    i32 id = -1;      // *(word @+0): -1 == empty slot
    u8  type = 0;     // byte @+2 (< 10 == active production)
    bool active = false; // byte @+12 (the "occupied/owned" flag)
    i32 wealth = 0;   // VIBE_Person_ComputeTotalWealth result for this building
};
struct ActiveBuildingCache {
    i32 generation = -0x7FFFFFFF; // dword_641FDC
    i32 cachedId = 0;             // *(word @ dword_641FE4) — 0 means "no cache ptr"
    i32 cachedWealth = 3200;      // dword_641FE0
    bool cachedValid = false;     // dword_641FE4 != 0
    u8  cachedTypeWord = 0;       // *(word @ cached) for the staleness check
    bool cachedActive = false;    // *(byte @ cached + 8)
};
int FindNextActiveBuilding(ActiveBuildingCache* cache, i32 generation,
                           const BuildingScanEntry* buildings, int count,
                           i32* outId, i32* outWealth);

// ===========================================================================
// VIBE_Amt_ComputeBuildingRivalryScore (0x57bc60) — the rivalry "raid" pass: the
// acting building rolls against every foreign active building, and (on a won
// roll) queues a money payout + a reputation delta. See the .cpp for the exact
// branch arithmetic; constants above.
// ===========================================================================
struct RivalryRival {
    i32   id = -1;          // *(word @+0)
    u8    type = 0;         // byte @+2
    i32   cityId = 0;       // dword @+1 (+4): the "different city" gate
    i32   cityRegion = 0;   // (int @+9) >> 24 : the same/adjacent region test
    i16   workForce = 0;    // *(byte @+432) (count, used as i16)
    i32   wealth = 0;       // ComputeTotalWealth(rival)
    float reputation = 0;   // float @+460
};
struct RivalrySelf {
    i32   cityId = 0;
    i32   cityRegion = 0;     // (int @+9) >> 24 of self
    i32   cityRegion2 = 0;    // v19 — the active-building region for the 0.7 test
    float workstationSum = 0; // SumWorkstationByCategory(9, 1)
    i32   wealth = 1;         // self wealth (v21[0]; divisor, never 0 in practice)
    float ratingCurveA = 0;   // RatingCurveA(4) for the optional coord payout
    bool  payoutCoordFlag = false; // v24[2]
};
struct RivalryResult {
    int rivalsPaid = 0;
    i32 totalPayout = 0;
};
RivalryResult ComputeBuildingRivalryScore(const RivalrySelf& self,
                                          const RivalryRival* rivals, int count,
                                          int currency);

// ===========================================================================
// VIBE_City_LookupSelectionInfoText (0x507b18) — resolve "_STADTAUSWAHL_<key>_INFO"
// to its localized text and copy it (UTF-16-ish 2-byte widen loop) into `out`.
// `lookup` returns the resolved narrow source string for the upcased key, or the
// key itself when not found (matching the original's fallthrough). Always
// returns 1. `out` must hold 2*len+2 bytes.
int LookupSelectionInfoText(const char* key, char* out,
                            const char* (*lookup)(const char* upperKey, void* ctx),
                            void* ctx);

// ===========================================================================
// VIBE_Amt_OpenOfficeWindow (0x5546a0) — thin entry wrapper: it primes a 4-dword
// stack arg block {?, 516, type=6} and forwards to the candidate-selection
// window. The window itself is deferred (pure UI); we expose the prepared arg
// block so callers/tests can verify the wiring.
struct OfficeWindowArgs {
    i32 slot0 = 0;
    i32 windowId = 516;
    u8  officeType = 6;
};
OfficeWindowArgs OpenOfficeWindow(int a1, int a2);

} // namespace guild::world
