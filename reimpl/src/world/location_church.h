#pragma once
// Church location rule-cores — the DECISION / COST / OUTCOME math behind each
// church menu action, lifted out of the GUI-coupled dialog bodies in gilde.exe.
//
// Each original VIBE_Location_Church*Dialog is a per-frame GUI loop
// (VIBE_GameLogic_RunFrameLoop) that (a) computes a cost/eligibility from
// person+city state, (b) renders it, and (c) on the player's click emits a
// network command (VIBE_Command_*). The GUI/render/frame-loop scaffold is
// DEFERRED; what is recovered here byte-for-byte are the (a)/(c) rule cores:
//
//   VIBE_Location_ChurchDonationDialog    0x521674  (donation cost + reputation)
//   VIBE_Location_ChurchIndulgenceDialog  0x521bac  (indulgence cost + clamp)
//   VIBE_Location_ChurchConfessionDialog  0x522ae8  (confession eligibility)
//
// All floating-point tuning constants are recovered exactly from the binary
// (see get_bytes provenance on each constant).
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP tuning constants (byte-for-byte from gilde.exe .rdata).
// ===========================================================================
namespace church {
// Donation (0x521674).
constexpr float  kDonationCostFrac    = 0.009999999776482582f; // flt_62240C  0x0a d7 23 3c
constexpr float  kDonationReputFactor = 0.20000000298023224f;  // flt_622410  0xcd cc 4c 3e
// Indulgence (0x521bac).
constexpr float  kIndulgenceCostFrac  = 0.004999999888241291f; // flt_622430  0x0a d7 a3 3b
constexpr float  kIndulgenceFavBase   = 200.0f;                // flt_622434  0x00 00 48 43
constexpr float  kIndulgenceFavScale  = 0.009999999776482582f; // flt_622438  0x0a d7 23 3c
constexpr float  kIndulgenceDefFactor = 1.5f;                  // immediate in fn
constexpr double kIndulgenceTenthDiv  = 0.1;                   // dbl_622440  ...b9 3f
constexpr double kIndulgenceMinClamp  = 3200.0;                // dbl_622448  ...a9 40
constexpr double kIndulgenceMaxClamp  = 320000.0;              // dbl_622450  ...13 41
} // namespace church

// ===========================================================================
// Donation  (gilde.exe 0x521674 — VIBE_Location_ChurchDonationDialog).
// ===========================================================================
// The dialog computes a suggested donation from the COMBINED wealth of the
// active player (word_63CC5C) and the visited NPC's employer (person+39):
//
//   wA = max(1, ComputeTotalWealth(player));
//   wB = max(1, ComputeTotalWealth(employer));
//   suggested = (int)((double)(wA + wB) * flt_62240C);   // 1% of combined wealth
//
// On confirm the player pays `paid` (the slider value, clamped to held money),
// and the church's standing with the player is bumped by a reputation delta
// distributed across other characters:
//
//   reputDelta = (int)((double)(255 * paid) / (double)(wA + wB));   // base bump
//   spread     = reputDelta * flt_622410;                           // 0.2x to peers
//
// DonationResult exposes those computed values; the command emit is mocked.
struct DonationResult {
    int wealthPlayer;     // max(1, player wealth)
    int wealthEmployer;   // max(1, employer wealth)
    int wealthCombined;   // wA + wB
    int suggestedCost;    // (int)(combined * kDonationCostFrac)
    int reputDelta;       // (int)(255*paid / combined)  (0 if combined==0)
    float reputSpread;    // reputDelta * kDonationReputFactor
};

// gilde.exe 0x521674 — the donation cost+reputation rule core.
// `playerWealth`/`employerWealth` are the raw VIBE_Person_ComputeTotalWealth
// returns BEFORE the max(1,..) guard; `paid` is the confirmed payment.
DonationResult ChurchComputeDonation(int playerWealth, int employerWealth, int paid);

// ===========================================================================
// Indulgence  (gilde.exe 0x521bac — VIBE_Location_ChurchIndulgenceDialog).
// ===========================================================================
// Offered only when the NPC has at least one outstanding crime against the
// player (VIBE_He_SumPlayerHandlerValues != 0). The indulgence price is:
//
//   base   = playerWealth * flt_622430;                 // 0.5% of player wealth
//   factor = (npc == player) ? 1.5
//          : (flt_622434 - Favorability(player,npc)) * flt_622438;  // (200-fav)*0.01
//   cost0  = (int)(base * factor);
//   // clamp the cost to a sane band, measured in 1/10ths:
//   t = cost0 * 0.1;
//   if (t > 3200 && t >= 320000)  cost = 320000;        // over the hard ceiling
//   else                          cost = (t <= 3200) ? 3200 : t;   // floor at 3200
//
// (The original's clamp reads oddly because the > and >= share the 3200/320000
//  thresholds; reproduced faithfully.)
struct IndulgenceResult {
    bool   offered;     // true when the NPC has an active crime vs the player
    float  factor;      // favorability-derived multiplier
    int    rawCost;     // (int)(base*factor) before clamp
    int    cost;        // final clamped indulgence price (<=0 => not offered)
};

// gilde.exe 0x521bac — the indulgence cost rule core.
//   hasCrime     : VIBE_He_SumPlayerHandlerValues(...) != 0
//   playerWealth : VIBE_Person_ComputeTotalWealth(player)
//   npcIsPlayer  : (person+39)==player  (self-indulgence path -> fixed 1.5 factor)
//   favorability : VIBE_Ai_ComputePersonFavorability(player, npc, 1)
IndulgenceResult ChurchComputeIndulgence(bool hasCrime, int playerWealth,
                                         bool npcIsPlayer, int favorability);

// ===========================================================================
// Confession  (gilde.exe 0x522ae8 — VIBE_Location_ChurchConfessionDialog).
// ===========================================================================
// Eligibility: the NPC must be employed (person+39 != 0xFFFF). The dialog shows
// the count of the player's active crimes (VIBE_Straftat_CountActiveByTarget);
// a "confess" button is only offered when that count > 0. On confess the player
// pays a skill-gated penance and the active crime count is reduced; the priest's
// (NPC employer's) standing with the player rises by half the paid amount
// (VIBE_He_RequestRivalEntityPairs(..., paid/2, ...)).
struct ConfessionResult {
    bool eligible;     // NPC employed
    bool canConfess;   // eligible && activeCrimes > 0
    int  activeCrimes; // VIBE_Straftat_CountActiveByTarget
    int  reputGain;    // paid/2 distributed to priest's owner on confess
};

// gilde.exe 0x522ae8 — confession eligibility + reputation rule core.
//   npcEmploymentWord : person+39 (0xFFFF == unemployed -> ineligible)
//   activeCrimes      : VIBE_Straftat_CountActiveByTarget(player)
//   paid              : skill-gated penance amount (0 if not confessing)
ConfessionResult ChurchComputeConfession(u16 npcEmploymentWord, int activeCrimes,
                                         int paid);

} // namespace guild::world
