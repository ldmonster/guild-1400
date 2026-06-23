#pragma once
// Thieves' guild location rule-cores — the COST / ELIGIBILITY / OUTCOME math
// behind each thief-guild commission, lifted out of the GUI dialog bodies in
// gilde.exe. The contact-loop menu (which item opens which dialog) lives in
// world/location.{h,cpp}; this file recovers what the dialogs *compute*.
//
//   VIBE_Location_ThiefBurglaryDialog   0x524074  (burglary eligibility gates)
//   VIBE_Location_ThiefBurglaryStart    0x5242d4  (busy/active-flag guard)
//   VIBE_Location_ThiefKidnapStart       0x5258bc (kidnap eligibility chain)
//   VIBE_Location_ThiefRansomDialog      0x5259f8 (ransom price + cut formula)
//   VIBE_Location_ThiefSpyBuildingStart  0x524740 (spy building-type select)
//   VIBE_Location_ThiefAttackStart       0x524e28 (attack start, fixed params)
//
// Mutations (queue a heist/ransom command, free a hostage) are routed through a
// mockable command hook so the rule cores are unit-testable without the net VM.
#include <vector>

#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP tuning constants (byte-for-byte from gilde.exe .rdata) — ransom.
// ===========================================================================
namespace thief {
constexpr double kRansomRngScaleA = 0.5;       // dbl_622848  ...e0 3f
constexpr double kRansomRngBias   = 0.75;      // dbl_622850  ...e8 3f
constexpr double kRansomRngScaleB = 1600000.0; // dbl_622858  ...6a 38 41
// Captivity-state -> ransom cut fraction (person+433).  case 1/2/3 / else.
constexpr float  kRansomCut1 = 0.02f;          // immediate
constexpr float  kRansomCut2 = 0.039999999f;   // immediate
constexpr float  kRansomCut3 = 0.059999999f;   // immediate
constexpr float  kRansomCut4 = 0.1f;           // default (>=4)
} // namespace thief

// ---------------------------------------------------------------------------
// Command hook (mock). Every thief mutation funnels through this so a test can
// observe what the dialog would have queued without the network VM.
// ---------------------------------------------------------------------------
enum class ThiefCommand {
    None = 0,
    QueueBurglary,     // VIBE_Command_QueueRequestSlotReset28 (heist target list)
    QueuePickpocket,   // VIBE_Command_QueueRequestSlotReset28 (pickpocket targets)
    PayRansom,         // VIBE_Command_RequestBuildOp91 + QueueRequestSlotReset28
    StartKidnap,       // VIBE_Command_QueueRequestEntity29
};
struct ThiefEmit { ThiefCommand cmd = ThiefCommand::None; int arg = 0; };

// ===========================================================================
// Burglary  (gilde.exe 0x524074 / 0x5242d4).
// ===========================================================================
// Two gates precede the heist dialog:
//   * VIBE_CharAction_IsAnimalTargetBusy(target) must be TRUE (target reachable)
//   * VIBE_Building_CheckSecurityThreshold(target, skill) must pass (the thief
//     out-skills the building's guard rating)
// When both pass, the dialog enumerates the player's eligible thieves
// (byte_12CEA98 marker set) and, if any, queues a burglary command on them.
struct BurglaryDecision {
    bool targetReachable;  // IsAnimalTargetBusy
    bool securityPassed;   // CheckSecurityThreshold
    bool offered;          // both gates pass
    int  eligibleThieves;  // count with the active marker
    ThiefEmit emit;        // QueueBurglary when eligibleThieves > 0, else None
};

// gilde.exe 0x524074 — burglary eligibility + command core.
// `thiefMarkers[i] != 0` means thief slot i is available for the heist (the
// original tests byte_12CEA98 stride-134 per candidate).
BurglaryDecision ThiefComputeBurglary(bool targetReachable, bool securityPassed,
                                      const std::vector<u8>& thiefMarkers);

// gilde.exe 0x5242d4 — VIBE_Location_ThiefBurglaryStart guard. The heist UI is
// only entered when no active-character action is already in flight.
//   if (!VIBE_Dialog_CheckActiveCharFlag()) { ...open burglary panel... }
// Returns true when the panel would open.
bool ThiefBurglaryStartAllowed(bool activeCharFlag);

// ===========================================================================
// Ransom  (gilde.exe 0x5259f8 — VIBE_Location_ThiefRansomDialog).
// ===========================================================================
// Offered only when the hostage exists (Person_FindRecordById) and the player's
// guild holds an active kidnap commission for them (handler type 61), and there
// is NO active ransom-pending handler (type 62). The price:
//
//   cut = by captivity-state byte (person+433): 1->0.02, 2->0.04, 3->0.06, >=4->0.1
//   wealth   = ComputeTotalWealth(hostage);
//   firstRoll  = (rand01_a * dbl_622848 + dbl_622850) * dbl_622858;  // FIRST draw
//   secondRoll = (rand01_b * dbl_622848 + dbl_622850) * dbl_622858;  // SECOND draw
//   base   = (wealth <= firstRoll) ? wealth : secondRoll; // else branch re-rolls
//   ransom = (int)((double)(int)base * cut);
//
// On "pay": the hostage is freed (BuildOp91 with -captivityState) and the ransom
// is queued to the guild; on "decline" nothing is emitted.
struct RansomDecision {
    bool   offered;     // hostage exists, has kidnap commission, no pending ransom
    float  cut;         // captivity-state cut fraction
    int    baseValue;   // min(wealth, rngRoll)
    int    ransom;      // (int)(baseValue * cut)
    ThiefEmit emit;     // PayRansom on accept, None otherwise
};

// gilde.exe 0x5259f8 — ransom price + outcome core.
//   hostageExists  : Person_FindRecordById(target) != 0
//   hasKidnapCmd   : handler type 61 present for hostage
//   ransomPending  : handler type 62 present (already in ransom flow -> not offered)
//   captivityState : person+433
//   wealth         : ComputeTotalWealth(hostage)
//   firstRoll      : (rand01_a*0.5 + 0.75) * 1600000 — the comparison roll
//   secondRoll     : (rand01_b*0.5 + 0.75) * 1600000 — used iff wealth > firstRoll
//   accept         : player clicked "pay ransom"
RansomDecision ThiefComputeRansom(bool hostageExists, bool hasKidnapCmd,
                                  bool ransomPending, int captivityState,
                                  int wealth, double firstRoll, double secondRoll,
                                  bool accept);

// gilde.exe 0x5259f8 ransom-cut helper: maps the captivity-state byte to its
// cut fraction (case 1/2/3, default for 0 and >=4).
float ThiefRansomCut(int captivityState);

// ===========================================================================
// Kidnap  (gilde.exe 0x5258bc — VIBE_Location_ThiefKidnapStart).
// ===========================================================================
// A chain of eligibility gates decides whether a kidnap can be commissioned:
//   1. no active-character action in flight (CheckActiveCharFlag == 0), else abort
//   2. NOT already a "captured" handler (type 60) for the target  -> else message
//   3. NOT already a kidnap-in-progress handler (type 61) whose hostage record is
//      flagged captive (+433), else message
// Only when all gates pass does the kidnap target window open (Entity29 emit).
struct KidnapDecision {
    bool blockedByActiveChar;  // CheckActiveCharFlag != 0
    bool alreadyCaptured;      // type-60 handler present
    bool kidnapInProgress;     // type-61 handler with captive hostage
    bool offered;              // none of the above
    ThiefEmit emit;            // StartKidnap when offered
};

// gilde.exe 0x5258bc — kidnap eligibility core.
KidnapDecision ThiefComputeKidnap(bool activeCharFlag, bool capturedHandler,
                                  bool kidnapHandlerCaptive);

// ===========================================================================
// Spy a building  (gilde.exe 0x524740 — VIBE_Location_ThiefSpyBuildingStart).
// ===========================================================================
// The spy prompt's text/range depends on the target building's TYPE byte
// (dword_13CE294 + 589*typeIndex, +0): production buildings (type 19) get a
// different value than the rest.
//   v6 = (typeByte == 19) ? 589840 : 134038452;
// Recovered as the building-type selector the start function computes.
struct SpyBuildingDecision {
    bool offered;        // !CheckActiveCharFlag
    bool isProduction;   // target building type byte == 19
    int  promptValue;    // 589840 (production) else 134038452
};
SpyBuildingDecision ThiefComputeSpyBuilding(bool activeCharFlag, int buildingTypeByte);

} // namespace guild::world
