#pragma once
// world_economy2 — the office-action *dialog* family (VIBE_Amt_Build* / Run*),
// the leaves left over after amt.cpp / amt_economy2.cpp / office_assign.cpp /
// guild_election.cpp took the economic + candidacy + slot cores. These are the
// player-triggered "appoint / dismiss / promote / demote an office holder"
// commands and their office-slot grid window.
//
// They are nominally UI builders, but each carries a self-contained DETERMINISTIC
// kernel that the original computes inline before it touches the form/text/network
// plumbing:
//   * the appointment "production price" formula (two branches),
//   * the shared office-payment formula ((rand+1)*(rating*15)+5)*2.55,
//   * the dismissal severance split (768-person scan / division),
//   * the action-direction sign roll,
//   * the office-slot grid label x-offset (record stride 740 layout math).
// We keep that control flow + arithmetic + recovered constant tables byte-for-byte
// and route the RNG / coord-truncation / table lookups / command-queue / render
// plumbing through an installable hooks struct with inert defaults (defined in the
// .cpp) — the sanctioned cross-module pattern (cf. amt_economy2.h's
// AmtEconomy2Hooks). A test installs its own hooks; src never references a test
// symbol.
//
// Translated (rules core extracted from the UI builders):
//   VIBE_Amt_BuildGuildOfficeMenu      0x481db8  (appointment: rank scan + price)
//   VIBE_Amt_BuildElectionDialog       0x482218  (dismissal: severance split)
//   VIBE_Amt_BuildOfficeActionDialog   0x4825b0  (appoint/promote/demote dispatch)
//   VIBE_Amt_RunOfficeGridWindow       0x558200  (office-slot grid label layout)
//   VIBE_Amt_RunCandidateWindowVariantA 0x5581e8 (variant dispatch wrapper)
//   VIBE_Amt_RunCandidateWindowVariantB 0x5584b0 (variant dispatch wrapper)
//   VIBE_Amt_ShowOfficeInfoDialog      0x558130  (info-dialog frame loop)
#include <cstddef>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered tuning constants (get_bytes; little-endian float/double bit-exact).
// ===========================================================================
// The shared office-payment formula constants (BuildElectionDialog 0x482218 uses
// flt_61AF38/dbl_61AF40/dbl_61AF48; BuildOfficeActionDialog 0x4825b0 uses the
// bit-identical flt_61AF54/dbl_61AF58/dbl_61AF60):
//   payment = ((RandomFloatScaled()+1) * (rating*15.0) + 5.0) * 2.55
constexpr float  kPayRatingScale = 15.0f; // flt_61AF38 / flt_61AF54 (0x41700000)
constexpr double kPayBase        = 5.0;   // dbl_61AF40 / dbl_61AF58
constexpr double kPayFinalScale  = 2.55;  // dbl_61AF48 / dbl_61AF60

// BuildOfficeActionDialog direction roll: dir = (roll <= 0.3) ? -1 : 1.
constexpr float kActionRollGate = 0.30000001192092896f; // flt_61AF50 (0x3E99999A)

// BuildGuildOfficeMenu appointment price (0x481db8):
//   branch (objectKind==1): price = (rand+1) * (rating*0.4) + 1.2
//   else                  : price = 1.0 - ((rand+1) * (rating*0.3) + 0.2)
constexpr float  kMenuPriceScaleHi = 0.4000000059604645f;  // flt_61AF30
constexpr float  kMenuPriceBaseHi  = 1.2000000476837158f;  // flt_61AF34
constexpr float  kMenuPriceScaleLo = 0.30000001192092896f; // flt_61AF20
constexpr double kMenuPriceBaseLo  = 0.2;                  // dbl_61AF28

// Office-slot grid window (0x558200): the office-holder/person record table is
// 740-byte stride; the grid lays out 6 cells. Slot field offsets used by the
// label-offset math: screen-x is the high word of the slot's @+14 dword; the
// camera origin (dword_62D298) supplies the @+2 / @+4 high-word offsets, and the
// label lands at (slotX>>16)+64-(camX>>16). We surface only the deterministic
// label-offset math; the form widgets are routed through hooks.
constexpr int kOfficeRecordStride = 740;
constexpr int kOfficeGridCells    = 6;
constexpr int kOfficeLabelBaseX   = 64; // the +64 literal

// The person record table is 768 records of 536 bytes (the well-known layout);
// the office-id (the entity this person currently serves) is the u16 @ +39.
constexpr int kPersonCount       = 768;
constexpr int kPersonOfficeIdOff = 39;

// ===========================================================================
// Installable hooks (RNG / coord / table access). Defaults inert/deterministic.
// ===========================================================================
struct WorldEconomy2Hooks {
    // VIBE_Math_RandomFloatScaled() == (double)RandNext()/32767. The payment /
    // price / direction rolls. Default deterministic 0.
    float (*randFloatScaled)() = nullptr;

    // VIBE_Coord_ConvertX() == round-toward-zero of the FPU st0. The payment and
    // label-offset results are truncated through it. Default = (i32) truncation.
    i32 (*truncate)(double v) = nullptr;

    // VIBE_Building_EvalProductionRating(building, stat). The dialogs query the
    // acting building's rating for stat 4 (or 2 for the menu). Default 0.
    float (*productionRating)(const void* building, int stat) = nullptr;

    // VIBE_Command_QueueRequest16(payer, recipient, amount, currency) — the money
    // command queued per affected holder. Default no-op.
    void (*queueRequest16)(i32 payer, i32 recipient, i32 amount, int currency) = nullptr;

    // VIBE_Command_QueueRequestCoord27(a, b, value) — the per-pair relation/coord
    // command (election + action dialogs). Default no-op.
    void (*queueCoord27)(i32 a, i32 b, i32 value) = nullptr;
};

void WorldEconomy2SetHooks(const WorldEconomy2Hooks& hooks);
const WorldEconomy2Hooks& WorldEconomy2GetHooks();
void WorldEconomy2ResetHooks(); // restore inert defaults

// ===========================================================================
// Shared office-payment formula (BuildElectionDialog / BuildOfficeActionDialog):
//   trunc( ((RandomFloatScaled()+1) * (rating*15.0) + 5.0) * 2.55 )
// `building` is forwarded to the productionRating hook with stat 4.
// ===========================================================================
i32 ComputeOfficePaymentAmount(const void* building);

// VIBE_Amt_BuildGuildOfficeMenu (0x481db8) appointment price kernel. When
// `objectKind == 1` the price ramps up from the production rating; otherwise it
// is an inverted (1.0 - ...) discount. Uses the menu price constants. Returns the
// float price (the original stores it as v35, later multiplied into slot fields).
float ComputeAppointmentPrice(float rating, int objectKind);

// VIBE_Amt_BuildOfficeActionDialog (0x4825b0) direction roll: returns -1 when the
// drawn roll is <= 0.3, else +1 (the v63 return value of the original).
int RollActionDirection();

// ===========================================================================
// VIBE_Amt_BuildGuildOfficeMenu (0x481db8) — guild-candidate rank scan. The
// original walks the person query iterator for category `cat` and keeps the max
// of each candidate's rank byte (record +583, 589-byte stride). We model the
// candidate ranks as a caller-provided array (the iterator result). Returns the
// maximum rank, or 0 when no candidate qualifies.
int ScanMaxCandidateRank(const u8* candidateRanks, int count);

// ===========================================================================
// VIBE_Amt_BuildElectionDialog (0x482218) — dismissal severance split. The
// original scans all 768 person records and, for each whose office-id (@+39)
// equals the dismissed holder's entity AND whose record index differs from the
// holder's own office-id value, counts a dependent and (in the second pass) queues
// each one a severance command. The severance per head is totalAmount / count.
// `officeIds[i]` is person i's office-id field; `present[i]` mirrors the original's
// "word_12CE910[i] != -1" liveness gate; `holderOfficeId` is the dismissed
// holder's office-id (the `v11 != v4` self-exclusion). Returns the dependent
// count; *outPerHead is the integer severance per head (0 when count==0).
int CountOfficeDependents(i32 targetEntity, const i32* personEntity,
                          const u16* officeIds, const bool* present,
                          u16 holderOfficeId, int count, i32 totalAmount,
                          i32* outPerHead);

// ===========================================================================
// VIBE_Amt_RunOfficeGridWindow (0x558200) — the grid-cell label x-offset. For a
// slot whose screen-x dword (@+14) high word is `slotScreenX` and a camera whose
// origin high word is `cameraX`, the centered label lands at
//   (slotScreenX) + 64 - (cameraX).
// (The original reads the high words via >>16 of the @+14 / @+2 dwords; the caller
// passes the already-shifted high words.) Pure integer math.
i32 ComputeGridLabelOffset(i32 slotScreenX, i32 cameraX);

} // namespace guild::world
