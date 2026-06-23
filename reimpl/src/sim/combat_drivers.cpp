#include "sim/combat_drivers.h"

namespace guild::sim {

// ===========================================================================
// Hooks plumbing (inert defaults)
// ===========================================================================
namespace {
CombatDriversHooks g_defaultHooks;     // all-null -> inert
const CombatDriversHooks* g_hooks = &g_defaultHooks;
}

void SetCombatDriversHooks(const CombatDriversHooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}

const CombatDriversHooks& GetCombatDriversHooks() {
    return *g_hooks;
}

u16 DriverRoll(u16 n) {
    // Mirrors `(unsigned __int16)VIBE_Math_RandomModulo(n)`. The original returns 0
    // when n == 0; the inert hook also returns 0.
    if (g_hooks->randomModulo)
        return static_cast<u16>(g_hooks->randomModulo(n));
    return 0;
}

// ===========================================================================
// RunResultScreen winner determination (0x48e4e4)
// ===========================================================================
int CountSurvivors(const std::vector<ResultUnit>& team) {
    // gilde.exe 0x48e5b9..0x48e5fb / 0x48e5fd..0x48e63d: a unit counts when its id
    // is set, its alive byte is non-zero AND not 4 (escaped), and its resolved
    // building class byte (+533) is not 1 (captured).
    int count = 0;
    for (const ResultUnit& u : team) {
        if (u.id == -1)
            continue;
        if (u.alive && u.alive != 4 && u.bldClass != 1)
            ++count;
    }
    return count;
}

DriverBattleWinner PickWinner(int attackerSurvivors, int defenderSurvivors) {
    // 0x48e641: `if (v15 <= v11) defender; else attacker;` with
    //   v11 == attackerSurvivors, v15 == defenderSurvivors.
    if (defenderSurvivors <= attackerSurvivors)
        return DriverBattleWinner::kDefender;
    return DriverBattleWinner::kAttacker;
}

DriverBattleWinner PickWinnerForced(bool forcedIsAttacker) {
    // 0x48e796: `if (forced == attacker) v19 = defender; else v19 = attacker;`
    return forcedIsAttacker ? DriverBattleWinner::kDefender : DriverBattleWinner::kAttacker;
}

int ClassifyRankIndex(u8 personClass, int previous) {
    // 0x48e945 chain: 19 -> 0, 16 -> 1, 4 -> 2, else keep previous.
    if (personClass == 19)
        return 0;
    if (personClass == 16)
        return 1;
    if (personClass == 4)
        return 2;
    return previous;
}

// ===========================================================================
// RunBattleSetup auto-resolve (0x490014)
// ===========================================================================
int ValidateRoster(std::vector<SetupSlot>& defenders, bool* outDirty) {
    // 0x49005b: set id but unresolved -> clear to -1 + dirty; resolved -> ++count.
    bool dirty = false;
    int liveCount = 0;
    for (SetupSlot& s : defenders) {
        if (s.id != -1) {
            if (s.resolves) {
                ++liveCount;
            } else {
                dirty = true;
                s.id = -1;
            }
        }
    }
    if (outDirty)
        *outDirty = dirty;
    return liveCount;
}

int SumSideScore(const std::vector<AutoResolveUnit>& side) {
    // 0x490146..0x49016f: for each unit alive (and != 4) accumulate
    //   acc = (int)(GetSoundRangeScale(unit) + (double)acc)   [x87 spill -> float
    // store then back to int]. We accumulate the int directly because the original
    // re-reads the int slot each iteration (v60 = v59; v59 = (int)(scale + v59)).
    int acc = 0;
    for (const AutoResolveUnit& u : side) {
        if (u.alive && u.alive != 4) {
            double sum = u.strength + static_cast<double>(acc);
            // VIBE_Coord_ConvertX is the x87 spill; the value lands in an int slot.
            acc = static_cast<int>(sum);
        }
    }
    return acc;
}

DriverBattleWinner AutoResolveWinner(int attackerScore, int defenderScore,
                               int attackerBonus, int defenderBonus) {
    // 0x490232: defenderTotal (v31) = defenderScore + defenderBonus;
    //           attackerTotal (Begin) = attackerScore + attackerBonus;
    //           if (defenderTotal <= attackerTotal) defender; else attacker.
    int defenderTotal = defenderScore + defenderBonus;
    int attackerTotal = attackerScore + attackerBonus;
    if (defenderTotal <= attackerTotal)
        return DriverBattleWinner::kDefender;
    return DriverBattleWinner::kAttacker;
}

// ===========================================================================
// BuildDeploymentScreen AI decision (0x48dab0)
// ===========================================================================
bool DeploymentOfferAccepted(int threshold) {
    // 0x48e23a / 0x48e3d8: (RandomModulo(90) + 10) <= threshold -> accept.
    int draw = static_cast<int>(DriverRoll(kDeployRollMod)) + kDeployRollBias;
    return draw <= threshold;
}

DeployDecision DeploymentAiDecision(bool hasUnits, int playerForce) {
    // 0x48e371 branch: only engage the roll when there are no units OR
    //   RandomModulo(5) > playerForce; otherwise hold (fight normally).
    if (hasUnits && static_cast<int>(DriverRoll(kDeployForceMod)) <= playerForce)
        return DeployDecision::kHold;

    // 0x48e3a7: threshold = RandomModulo(4) ? (==1 ? 90 : 50) : 10.
    int mode = static_cast<int>(DriverRoll(kDeployModeMod));
    int threshold;
    if (mode == 0)
        threshold = kDeployThreshLow;
    else if (mode == 1)
        threshold = kDeployThreshHigh;
    else
        threshold = kDeployThreshMid;

    return DeploymentOfferAccepted(threshold) ? DeployDecision::kAccept
                                              : DeployDecision::kRefuse;
}

// ===========================================================================
// IssueOrdersForTeam (0x48c15c)
// ===========================================================================
int ResolveTeamRow(const std::vector<i32>& teamTable, int teamCount, i32 teamId) {
    // 0x48c166: scan v3 in [0, *(u8*)(dword_631208+48)); match when
    //   dword_631208[v4+52] == team+4.  v4 steps by 4 (== one i32 per row).
    int rows = teamCount;
    if (rows > static_cast<int>(teamTable.size()))
        rows = static_cast<int>(teamTable.size());
    for (int i = 0; i < rows; ++i) {
        if (teamTable[i] == teamId)
            return i;
    }
    return -1;
}

// ===========================================================================
// UpdateOrderSlots per-slot refresh (0x4882c0)
// ===========================================================================
OrderSlotResult ComputeOrderSlot(const OrderSlotInput& in, double ratioScale) {
    OrderSlotResult r;
    if (in.slotId == -1) {
        r.skip = true;
        return r;
    }
    // 0x488353: if the resolved record is "captured" (+533 == 1) the slot is hidden
    // (both objects set invisible) and nothing is rendered.
    if (in.recCaptured) {
        r.skip = true;
        return r;
    }
    // 0x4883b2: active -> add to drag table + ++dword_63120C ; else clear.
    if (in.active) {
        r.count = 1;
    }
    // 0x4883f8: value = (int)(ComputeOutputRatio(unit) * dbl_61B264).
    // dbl_61B264 == 100.0 (double). The original multiplies in the x87 stack
    // (ComputeOutputRatio(double) * 100.0(double)) and truncates via ConvertX
    // (frndint with RC=truncate-toward-zero) — there is NO float store in between,
    // so we must truncate the DOUBLE product directly (a float intermediate would
    // wrongly re-round e.g. 6.9999999->7.0 and change the truncated int).
    double value = in.outputRatio * ratioScale;
    r.outputValue = static_cast<int>(value);             // ConvertX truncate
    return r;
}

// ===========================================================================
// AssignSelectedTarget (0x488874)
// ===========================================================================
int FindSelectedTarget(const std::vector<SelectableUnit>& units, bool globalOverride) {
    // 0x488891..0x4888a8 (bound 32): the first unit that is present, selectable
    // (+392), owned-by-active-team OR globalOverride (dword_63C7B8), has a field
    // (+388) and a set alive byte (+8).
    int bound = static_cast<int>(units.size());
    if (bound > 32)
        bound = 32;
    for (int i = 0; i < bound; ++i) {
        const SelectableUnit& u = units[i];
        if (!u.present || !u.selectable)
            continue;
        bool gate = u.ownerMatch || globalOverride;   // v10 |= 8 on either
        if (gate && u.hasField && u.alive)
            return i;
    }
    return -1;
}

// ===========================================================================
// SetUnitFormationMode (0x48980c)
// ===========================================================================
int FindFirstFreeFormationSlot(const std::vector<i32>& slots) {
    // 0x489830: if slot 0 is already free (-1) the result is 0. Otherwise scan
    //   ++v3; if v3 >= 16 -> v3 = -1 (all full); if slots[v3] == -1 stop.
    if (slots.empty())
        return -1;
    if (slots[0] == -1)
        return 0;
    int v3 = 0;
    while (true) {
        ++v3;
        if (v3 >= kTeamSlotCount)
            return -1;
        if (v3 >= static_cast<int>(slots.size()))
            return -1;
        if (slots[v3] == -1)
            return v3;
    }
}

int FormationModeOpcode(u8 mode) {
    // 0x489820 / 0x489b69: mode 1 -> 342; mode 2 or 3 -> 344 (first of the 344/352
    // pair); anything else -> none (-1).
    if (mode == 1)
        return kFormOpLine;
    if (mode == 2 || mode == 3)
        return kFormOpA;
    return -1;
}

// ===========================================================================
// UpdatePursuitTargets (0x48c400)
// ===========================================================================
std::vector<PursuitAction> DrivePursuitTargets(const std::vector<PursuitUnit>& units,
                                               double ratioScale) {
    std::vector<PursuitAction> out;
    out.reserve(units.size());
    for (const PursuitUnit& u : units) {
        if (u.id == -1) {
            out.push_back(PursuitAction::kSkip);
            continue;
        }
        // 0x48c4f7: scaled = (int)(ComputeOutputRatio(unit) * dbl_61B8EC).
        // dbl_61B8EC == 100.0 (double); the original truncates the DOUBLE product
        // via ConvertX (no float store) — truncate the double directly.
        double scaledD = u.outputRatio * ratioScale;
        int scaled = static_cast<int>(scaledD);          // ConvertX truncate
        // 0x48c51f: attack if scaled >= 20 OR RandomModulo(100) <= 30; else flee.
        bool attack;
        if (scaled >= 20) {
            attack = true;
        } else {
            int roll = static_cast<int>(DriverRoll(100));
            attack = (roll <= 30);
        }
        out.push_back(attack ? PursuitAction::kAttack : PursuitAction::kFlee);
    }
    return out;
}

} // namespace guild::sim
