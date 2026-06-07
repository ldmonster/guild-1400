#include "world/location_tavern.h"

namespace guild::world {

// gilde.exe 0x516b78 — VIBE_Location_TavernCardGame offer gate + willingness.
// The original's branch:
//   if ( RandomFloatScaled() >= willingness || gameHour <= 0xB || wealth < 10000 )
//   {
//       // DECLINE path
//       if ( gameHour <= 0xB ) goto LABEL_6;            // no willingness update
//       newWillingness = willingness*flt_621C5C + flt_621C60;   // *0.66 - 0.02
//   }
//   else
//   {
//       // ACCEPT path
//       newWillingness = willingness*flt_621C5C + flt_621C64;   // *0.66 - 0.10
//   }
//   familyWillingness = newWillingness;                 // written back
//
// So: offered == (rand < willingness && hour > 11 && wealth >= 10000). On the
// decline path the willingness write-back is SKIPPED when hour <= 11 (LABEL_6).
CardGameDecision TavernComputeCardGame(float rand01, float willingness,
                                       int gameHour, int playerWealth) {
    CardGameDecision d{};
    d.npcInclined = rand01 < willingness;
    d.hourOk      = gameHour > tavern::kMinHourExclusive;
    d.wealthOk    = playerWealth >= tavern::kMinWealth;

    // The decline condition mirrors the original's `||` chain exactly.
    bool decline = (rand01 >= willingness) || (gameHour <= tavern::kMinHourExclusive) ||
                   (playerWealth < tavern::kMinWealth);
    d.offered = !decline;

    if (d.offered) {
        d.newWillingness = willingness * tavern::kWillingnessDecay +
                           tavern::kWillingnessBiasYes;
        d.willingnessUpdated = true;
    } else {
        if (gameHour <= tavern::kMinHourExclusive) {
            // LABEL_6: willingness left untouched.
            d.newWillingness = willingness;
            d.willingnessUpdated = false;
        } else {
            d.newWillingness = willingness * tavern::kWillingnessDecay +
                               tavern::kWillingnessBiasNo;
            d.willingnessUpdated = true;
        }
    }
    return d;
}

} // namespace guild::world
