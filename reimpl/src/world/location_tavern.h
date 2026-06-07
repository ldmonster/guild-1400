#pragma once
// Tavern location rule-cores — the STAKE / ELIGIBILITY / OUTCOME math behind the
// tavern card game and the Stammtisch ("regulars' table"), lifted out of the
// GUI dialog bodies in gilde.exe.
//
//   VIBE_Location_TavernCardGame  0x516b78  (card-game offer gate + willingness)
//
// The full card-round play (VIBE_Tavern_RunCardRoundPhase / VIBE_AiCardGame_*)
// is its own AI module and is DEFERRED; recovered here is the decision the
// dialog makes BEFORE play: whether a game is even offered, and how the NPC's
// willingness ("Spiellaune") decays after accepting/declining.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Recovered FP tuning constants (byte-for-byte) — card game willingness decay.
// The NPC's family record holds a willingness float at +0x5C (FamilyRecord+23
// dwords). Each visit it decays:  new = old * 0.66 + bias.
// ===========================================================================
namespace tavern {
constexpr float kWillingnessDecay   = 0.6600000262260437f;  // flt_621C5C  0xc3 f5 28 3f
constexpr float kWillingnessBiasNo  = -0.019999999552965164f;// flt_621C60  0x0a d7 a3 bc (declined)
constexpr float kWillingnessBiasYes = -0.10000000149011612f; // flt_621C64  0xcd cc cc bd (accepted)
// Hard gates for offering a game.
constexpr int   kMinHourExclusive   = 0xB;     // WORD2(qword_13CE852) must be > 11
constexpr int   kMinWealth          = 10000;   // player wealth >= 10000
} // namespace tavern

// ===========================================================================
// Card game offer  (gilde.exe 0x516b78 — VIBE_Location_TavernCardGame).
// ===========================================================================
// The dialog computes whether the NPC will play this visit:
//
//   playerWealth = ComputeTotalWealth(player);
//   willing = (Math_RandomFloatScaled() < familyWillingness)   // NPC inclined
//          && (gameHour > 11)                                  // tavern open late
//          && (playerWealth >= 10000);                         // stake floor
//   // willingness then decays regardless of branch:
//   if (offered)  newWillingness = willingness*0.66 + (-0.10);  // accepted
//   else          newWillingness = willingness*0.66 + (-0.02);  // declined
//   // (the decline branch ONLY updates willingness when gameHour > 11.)
//
// The eventual stake/payout is settled by the deferred card round; the rule core
// here is the offer gate + willingness mutation.
struct CardGameDecision {
    bool  offered;             // a game is offered this visit
    bool  npcInclined;         // rand < willingness
    bool  hourOk;              // gameHour > 11
    bool  wealthOk;            // playerWealth >= 10000
    float newWillingness;      // updated familyWillingness (unchanged if not stored)
    bool  willingnessUpdated;  // whether newWillingness was written back
};

// gilde.exe 0x516b78 — card-game offer + willingness core.
//   rand01       : Math_RandomFloatScaled() result in [0,1)
//   willingness  : familyRecord +0x5C float (NPC's current Spiellaune)
//   gameHour     : WORD2(qword_13CE852) — current game-time hour
//   playerWealth : ComputeTotalWealth(player)
CardGameDecision TavernComputeCardGame(float rand01, float willingness,
                                       int gameHour, int playerWealth);

} // namespace guild::world
