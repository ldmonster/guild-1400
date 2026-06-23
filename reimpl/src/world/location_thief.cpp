#include "world/location_thief.h"

namespace guild::world {

// gilde.exe 0x524074 — VIBE_Location_ThiefBurglaryDialog eligibility + emit.
//   if (!IsAnimalTargetBusy(target)) { message; return; }      // unreachable
//   if (!CheckSecurityThreshold(target, skill)) { message; return; }  // too secure
//   ...open panel, count thieves with byte_12CEA98 marker set...
//   if (count) QueueRequestSlotReset28(list, count); else message.
BurglaryDecision ThiefComputeBurglary(bool targetReachable, bool securityPassed,
                                      const std::vector<u8>& thiefMarkers) {
    BurglaryDecision d{};
    d.targetReachable = targetReachable;
    d.securityPassed  = securityPassed;
    d.offered = targetReachable && securityPassed;

    d.eligibleThieves = 0;
    if (d.offered) {
        for (u8 m : thiefMarkers)
            if (m) ++d.eligibleThieves;
    }
    if (d.offered && d.eligibleThieves > 0) {
        d.emit = {ThiefCommand::QueueBurglary, d.eligibleThieves};
    }
    return d;
}

// gilde.exe 0x5242d4 — VIBE_Location_ThiefBurglaryStart guard.
//   if (!VIBE_Dialog_CheckActiveCharFlag()) { ...open... }
bool ThiefBurglaryStartAllowed(bool activeCharFlag) {
    return !activeCharFlag;
}

// gilde.exe 0x5259f8 ransom cut by captivity-state byte (person+433).
//   if (state < 4) switch(state){case 3:.06; case 2:.04; case 1:.02;} else .1
// (state 0 falls through the switch leaving the default; the original's switch
//  has no case 0 and v40 is uninitialised there, but the offered path only runs
//  with a valid kidnap commission whose state is >=1, so 0 maps to the default.)
float ThiefRansomCut(int captivityState) {
    if ((unsigned)captivityState < 4u) {
        switch (captivityState) {
            case 3: return thief::kRansomCut3;
            case 2: return thief::kRansomCut2;
            case 1: return thief::kRansomCut1;
            default: return thief::kRansomCut4; // state 0 -> default band
        }
    }
    return thief::kRansomCut4;
}

// gilde.exe 0x5259f8 — VIBE_Location_ThiefRansomDialog price + outcome core.
//   v40 = cut(state);
//   v6  = ComputeTotalWealth(hostage);                       // wealth int
//   v7  = RandomFloatScaled();                               // FIRST draw
//   if ((double)v6 <= (v7*dbl_622848 + dbl_622850)*dbl_622858)
//        v8 = (double)v6;                                    // base = wealth
//   else v8 = (RandomFloatScaled()*dbl_622848 + dbl_622850)*dbl_622858; // SECOND draw
//   VIBE_Coord_ConvertX(); v43 = (int)v8;                    // trunc toward zero
//   v42 = (int)((double)(int)v8 * v40);
//   accept -> RequestBuildOp91(hostage, -state) + QueueRequestSlotReset28
//
// The binary draws the RNG TWICE: the comparison uses the first roll, and the
// value-when-exceeded is an INDEPENDENT second roll. Both pre-evaluated rolls are
// passed in (firstRoll for the compare, secondRoll for the else branch) so the
// helper reproduces the exact draw order/select without conflating them.
RansomDecision ThiefComputeRansom(bool hostageExists, bool hasKidnapCmd,
                                  bool ransomPending, int captivityState,
                                  int wealth, double firstRoll, double secondRoll,
                                  bool accept) {
    RansomDecision d{};
    d.offered = hostageExists && hasKidnapCmd && !ransomPending;
    if (!d.offered) {
        d.cut = 0.0f;
        d.baseValue = 0;
        d.ransom = 0;
        return d;
    }
    d.cut = ThiefRansomCut(captivityState);
    // if (wealth <= firstRoll) base = wealth; else base = secondRoll (fresh draw).
    double v8 = ((double)wealth <= firstRoll) ? (double)wealth : secondRoll;
    d.baseValue = (int)v8;                                 // (int) trunc toward zero
    d.ransom = (int)((double)d.baseValue * (double)d.cut); // (int) trunc toward zero
    if (accept) {
        d.emit = {ThiefCommand::PayRansom, d.ransom};
    }
    return d;
}

// gilde.exe 0x5258bc — VIBE_Location_ThiefKidnapStart eligibility chain.
//   if (CheckActiveCharFlag()) abort.
//   if (type-60 handler present) -> already captured -> message (LABEL_11).
//   if (type-61 handler whose hostage record +433 flagged) -> in progress -> msg.
//   else open kidnap target window (Entity29).
KidnapDecision ThiefComputeKidnap(bool activeCharFlag, bool capturedHandler,
                                  bool kidnapHandlerCaptive) {
    KidnapDecision d{};
    d.blockedByActiveChar = activeCharFlag;
    d.alreadyCaptured     = capturedHandler;
    d.kidnapInProgress    = kidnapHandlerCaptive;
    d.offered = !activeCharFlag && !capturedHandler && !kidnapHandlerCaptive;
    if (d.offered) {
        d.emit = {ThiefCommand::StartKidnap, 0};
    }
    return d;
}

// gilde.exe 0x524740 — VIBE_Location_ThiefSpyBuildingStart selector.
//   if (CheckActiveCharFlag()) not offered.
//   v6 = (*(byte*)(589 * typeIndex + dword_13CE294) == 19) ? 589840 : 134038452;
SpyBuildingDecision ThiefComputeSpyBuilding(bool activeCharFlag, int buildingTypeByte) {
    SpyBuildingDecision d{};
    d.offered = !activeCharFlag;
    d.isProduction = (buildingTypeByte == 19);
    d.promptValue = d.isProduction ? 589840 : 134038452;
    return d;
}

} // namespace guild::world
