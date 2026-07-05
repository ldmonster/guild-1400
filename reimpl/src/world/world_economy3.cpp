#include "world/world_economy3.h"

// Faithful 1:1 ports of the deterministic decision kernels inside the guild/office
// "amt" dialog shells (see world_economy3.h for the address map). The surrounding
// GUI frame pumps are deferred; only the value-deciding math/dispatch is recovered.

namespace guild::world {

// ---------------------------------------------------------------------------
// Shared office-name text base (all four Level3/Level2 dialogs).
//   gilde.exe 0x520f98/0x5210e4/0x521234/0x520838:
//     if (LOBYTE(dword_12CE919[...])) v = defKind+560; else v = defKind+525;
// ---------------------------------------------------------------------------
int GuildOfficeNameTextId(unsigned promoted, unsigned defKind) {
    // defKind is read as an unsigned byte in the original ((unsigned __int8)...).
    unsigned dk = defKind & 0xFFu;
    if (promoted)
        return static_cast<int>(dk) + kOfficeNameBasePromoted;
    return static_cast<int>(dk) + kOfficeNameBasePlain;
}

// ---------------------------------------------------------------------------
// VIBE_Amt_RunElectionCandidateWindow 0x557a40 kernels.
// ---------------------------------------------------------------------------
int GuildCandidateOfficeNameId(unsigned heldFlag) {
    // gilde.exe 0x557d27: (*(_BYTE *)(rec+12) == 0) + 1596.
    //   heldFlag == 0 -> 1597, else 1596.
    return ((heldFlag & 0xFFu) == 0 ? 1 : 0) + kOfficeNameHeld;
}

int GuildCandidateOutputRatingId(float output) {
    // gilde.exe 0x557e4e..: thresholds against the IEEE bit patterns of
    // 100.0 / 350.0 / 650.0, with the exact nesting:
    //   if (out >= 100) { if (out >= 350) { if (out >= 650) 1602 else 1603 }
    //                     else 1604 }
    //   else 1605
    if (output >= kCandidateOutputTier1) {
        if (output >= kCandidateOutputTier2) {
            if (output >= kCandidateOutputTier3)
                return 1602;
            return 1603;
        }
        return 1604;
    }
    return 1605;
}

int GuildElectionTitleTextId(bool hasSelection) {
    // gilde.exe 0x557aa9: if (v45) render id 0x9D; else the "$Z$[%s$]" string.
    return hasSelection ? kElectionTitleSelectedId : 0;
}

ElectionCollectMode GuildElectionCollectMode(int category) {
    // gilde.exe 0x557b37: if (a1 > 0 && a1 < 7) collect-by-category;
    //   else if (a1 != 7) return (abort); else collect elective offices.
    // a1 is a signed byte (v46[4]).
    if (category > 0 && category < 7)
        return ElectionCollectMode::kByCategory;
    if (category != 7)
        return ElectionCollectMode::kAbort;
    return ElectionCollectMode::kElectiveOffices;
}

// ---------------------------------------------------------------------------
// VIBE_Guild_CheckLevel3AndShowDialog 0x521384.
// ---------------------------------------------------------------------------
Level3Action GuildLevel3RankAction(int rankCheckCode) {
    // gilde.exe 0x5213ad: if (v4 == 1) ...office dialog...;
    //   else if (v4 == -1) message box; else skill check.
    if (rankCheckCode == 1)
        return Level3Action::kShowOfficeDialog;
    if (rankCheckCode == -1)
        return Level3Action::kShowMessageBox;
    return Level3Action::kCheckSkill;
}

Level3Dialog GuildLevel3DialogForDefKind(unsigned defKind) {
    // gilde.exe 0x521412 switch on byte_12CEA79[...] (the def-kind byte):
    //   30 -> A (RunOfficeOverviewWindow + ShowLevel3OfficeDialogA)
    //   31,33 -> B (RunOfficeGridWindow + ShowLevel3OfficeDialogB)
    //   32 -> C (RunOfficeGridWindow + ShowLevel3OfficeDialogC)
    switch (defKind & 0xFFu) {
        case 30: return Level3Dialog::kDialogA;
        case 31:
        case 33: return Level3Dialog::kDialogB;
        case 32: return Level3Dialog::kDialogC;
        default: return Level3Dialog::kNone;
    }
}

// ---------------------------------------------------------------------------
// VIBE_Guild_RunLevel3ContactLoop 0x521508.
// ---------------------------------------------------------------------------
int GuildLevel3ContactStatusId(unsigned defKind) {
    // gilde.exe 0x521537 switch:
    //   30 -> 4751, 31/33 -> 4733, 32 -> 4743, default -> 0.
    switch (defKind & 0xFFu) {
        case 30: return kLevel3ContactId30;
        case 31:
        case 33: return kLevel3ContactId3133;
        case 32: return kLevel3ContactId32;
        default: return 0;
    }
}

// ---------------------------------------------------------------------------
// VIBE_Guild_ShowLevel2JoinDialog 0x520838 — guild-join fee.
// ---------------------------------------------------------------------------
int GuildLevel2JoinFee(int totalWealth) {
    // gilde.exe 0x52086e..0x520880: fild wealth; fmul flt_62235C(0.01f);
    //   fcomp flt_622360(160.0f); ja 0x520A5B.
    // 0x520A5B (> branch): recomputes wealth*0.01f and *fstp*s it into the 4-byte
    //   float var_10 — the fee is rounded to SINGLE precision before truncation.
    // fall-through: mov var_10, 43200000h (160.0f).
    // 0x520898..0x5208aa: fld dword var_10; Coord_ConvertX; fistp -> (int) trunc.
    // e.g. wealth 1'000'000: 9999.99978 (double) -> 10000.0f -> fee 10000, NOT 9999.
    double scaled = static_cast<double>(totalWealth) * kGuildJoinFeeRate;
    float fee = (scaled > kGuildJoinFeeFloor) ? static_cast<float>(scaled)
                                              : 160.0f;
    return static_cast<int>(fee);
}

// ---------------------------------------------------------------------------
// VIBE_Amt_RunAgendaWindow 0x5584c8 — agenda bucket from the +16 state byte.
// ---------------------------------------------------------------------------
AgendaBucket GuildAgendaBucket(u8 holderState) {
    // gilde.exe 0x5585d5 / 0x558666: state 2 -> member, state 3 -> successor.
    if (holderState == kAgendaStateMember)
        return AgendaBucket::kMember;
    if (holderState == kAgendaStateSuccessor)
        return AgendaBucket::kSuccessor;
    return AgendaBucket::kSkip;
}

// ---------------------------------------------------------------------------
// Hooks (inert defaults).
// ---------------------------------------------------------------------------
namespace {
WorldEconomy3Hooks g_hooks{};
}  // namespace

const WorldEconomy3Hooks& WorldEconomy3GetHooks() { return g_hooks; }

WorldEconomy3Hooks WorldEconomy3SetHooks(const WorldEconomy3Hooks& h) {
    WorldEconomy3Hooks prev = g_hooks;
    g_hooks = h;
    return prev;
}

int GuildCandidateRatingViaHook(const void* officeRecord) {
    float out = g_hooks.buildingCurrentOutput
                    ? g_hooks.buildingCurrentOutput(officeRecord)
                    : 0.0f;  // inert default -> 0 -> tier 1605
    return GuildCandidateOutputRatingId(out);
}

int GuildLevel2JoinFeeViaHook(int personId, const void* extra) {
    int wealth = g_hooks.personTotalWealth
                     ? g_hooks.personTotalWealth(personId, extra)
                     : 0;  // inert default -> 0 -> fee floor 160
    return GuildLevel2JoinFee(wealth);
}

} // namespace guild::world
