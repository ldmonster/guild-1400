// ---------------------------------------------------------------------------
// VIBE_MissionReq_Evaluate (gilde.exe 0x5398c4) — objective requirement dispatcher.
// See mission_recon3_evaluate.h for the cluster rationale and provenance.
//
// 1:1 translation of the disassembly at 0x5398c4..0x539c44.  The requirement-row
// scan, the master/special gates, the 49-case switch (selector = type-1) and the
// exact comparison polarity per case are reproduced verbatim.  Coupled leaves are
// invoked through the inert-default hook table (MissionReq3GetHooks()).
//
// VERIFIED-1:1 (byte/disasm audit against gilde.exe):
//   * Special-flag gate 0x539928-0x539935: `mov dh,byte_63C8F4; test dh,dh;
//     jl ...; cmp dh,5; jle ...` is a SIGNED [0,5] test (jl/jle).  Hex-Rays'
//     collapse to `(u8)byte_63C8F4 < 6u` is wrong; signed model below is correct.
//   * Table scan 0x5398e2: `lea ebx,[edx*4]; sub ebx,edx; shl ebx,3` = edx*24;
//     stride 24, unsigned byte type compare.  ReqTableRow stride==24 (static_assert).
//   * Offsets: threshold=[ebx+10h], timerMin=[ebx+14h], value=[esi+1Ch],
//     state=[ecx+166h], clanSize=[ecx+0Dh], bldcode=[ecx+161h] sar 18h (signed).
//     family dwords [11]=+2Ch [12]=+30h [13]=+34h [14]=+38h [15]=+3Ch.
//   * Polarity: setnle(>) fam-wealth cases; setnl(>=) rank/value/wealth/currency;
//     setz(==) state cases; case47 setbe; case48 fldz/fcompp setz.
//   * Case 47 0x539bf4: `fild [ebx+10h]; fmul flt_623D68(0x3C23D70A=0.01f);
//     fcomp [esp+var_1C](float average)` — product kept in 80-bit x87, never
//     stored to a float slot; promoting 0.01f and the float average to double in
//     C++ is bit-equivalent (double->80-bit exact).  flt_623D68 bytes 0a d7 23 3c.
//   * Case 31 0x539a2e: `mov cl,byte_6477A1` feeds BOTH GetCurrencyAmount and
//     ConvertToDisplayCoord (Hex-Rays' uninitialised `v9` is noise).
//   * Cases 33/44 0x539b99/0x539bb9: `mov edx,esi`(objective), eax=person;
//     38/39 0x539b89/0x539b79: `mov edx,ebx`(row), eax=person — hook contracts ok.
// ---------------------------------------------------------------------------
#include "world/mission_recon3_evaluate.h"

namespace guild::world {

using f32 = float;

// --- recovered dispatcher globals ------------------------------------------
u8  g_missionReqEnabled     = 0;   // byte_63CC40
i32 g_missionReqSpecialFlag = 0;   // dword_63CD44
u8  g_missionReqDisplayCcy  = 0;   // byte_6477A1

// byte_63C8F4 is g_missionSlotMode (world/mission.h); reused, not redefined.
extern u8 g_missionSlotMode;

// flt_623D68 = 0x3C23D70A = 0.01f (case 47 threshold scale).
static const f32 kFlt623D68 = 0.01f;

// --- requirement table (byte_63CD4C / dword_5383F0) ------------------------
namespace {
const ReqTableRow* s_reqRows  = nullptr;
int                s_reqCount = 0;

MissionReq3Hooks s_hooks;  // all-null inert defaults
}  // namespace

void MissionReq3SetTable(const ReqTableRow* rows, int count) {
    s_reqRows  = rows;
    s_reqCount = count;
}

MissionReq3Hooks& MissionReq3GetHooks() { return s_hooks; }

// Scan the stride-24 requirement table for the row whose type byte (+0) matches.
// gilde.exe 0x5398d0..0x53997c: while (byte_63CD4C[v] != type) v += 24; off the
// end (v >= 24*count) -> null.
static const ReqTableRow* FindReqRow(u8 type) {
    if (s_reqCount <= 0 || s_reqRows == nullptr)   // dword_5383F0 <= 0
        return nullptr;
    for (int i = 0; i < s_reqCount; ++i) {
        if (s_reqRows[i].type == type)             // byte_63CD4C[v] == type
            return &s_reqRows[i];
    }
    return nullptr;
}

// gilde.exe 0x5398c4 — VIBE_MissionReq_Evaluate.
bool MissionReqEvaluate(ObjectiveRecord* objective) {
    const MissionReq3Hooks& h = s_hooks;

    const u8 type = objective->type;               // v11 = *a1 (0x5398ce)
    const ReqTableRow* row = FindReqRow(type);     // table scan (0x5398d0..)

    if (!g_missionReqEnabled)                       // !byte_63CC40 (0x53990b) -> 0
        return false;
    if (row == nullptr)                             // jz loc_539966 (0x53991d): eax==0
        return false;

    // One-shot "special objective met" short-circuit (0x53991f..0x53998f):
    //   if dword_63CD44 == 1 && (signed char)byte_63C8F4 in [0,5] -> clear, true.
    if (g_missionReqSpecialFlag == 1) {
        i8 mode = static_cast<i8>(g_missionSlotMode);   // mov dh, byte_63C8F4
        if (mode >= 0 && mode <= 5) {                   // test/jl + cmp 5/jle
            g_missionReqSpecialFlag = 0;                // dword_63CD44 = 0
            return true;                                // mov eax, 1
        }
    }

    // person = FindRecordById(objective->personId) (0x539937); null -> 0.
    const u8* person = h.personFindRecordById
                           ? h.personFindRecordById(objective->personId)
                           : nullptr;
    if (person == nullptr)
        return false;

    const i32 threshold = row->threshold;            // [ebx+10h]
    const i32 timerMin  = row->timerMin;             // [ebx+14h]
    const u8  ccy       = g_missionReqDisplayCcy;     // byte_6477A1

    switch (type) {                                   // dec dl; switch 49 (0x539954)

    // cases 1,6,14,22,32 — family[+0x38] (dword[14]); convert; > threshold.
    case 1: case 6: case 14: case 22: case 32: {
        const i32* fam = h.personGetFamilyRecord ? h.personGetFamilyRecord(person)
                                                 : nullptr;
        if (!fam) return false;                       // jz loc_539966
        i32 disp = h.moneyConvertToDisplayCoord
                       ? h.moneyConvertToDisplayCoord(fam[14], ccy) : 0;
        return disp > threshold;                       // setnle (0x5399a4)
    }

    // cases 2,7 — building-type code (person[+0x161] top byte); rank >= threshold.
    case 2: case 7: {
        i32 code = h.personBuildingTypeCode ? h.personBuildingTypeCode(person) : 0;
        i32 rank = h.buildingTypeComputeRank
                       ? h.buildingTypeComputeRank(static_cast<u8>(code)) : 0;
        return rank >= threshold;                      // setnl (0x5399c6)
    }

    // cases 3,10,24 — person[+0x0D] >= threshold, gated through hold timer.
    case 3: case 10: case 24: {
        bool met = (static_cast<i32>(person[0x0D]) >= threshold);  // setnl (0x5399e5)
        return h.accumulateTimer
                   ? h.accumulateTimer(objective, met, timerMin) : false;
    }

    // cases 4,9,29 — CheckStatThreshold(person, row).
    case 4: case 9: case 29:
        return h.checkStatThreshold ? h.checkStatThreshold(person, row) : false;

    // cases 5,13,21,30 — ComputeTotalWealth(marker, objective); convert; >= threshold.
    case 5: case 13: case 21: case 30: {
        u16 marker = h.personMarkerWord ? h.personMarkerWord(person) : 0;  // mov ax,[ecx]
        i32 wealth = h.personComputeTotalWealth
                         ? h.personComputeTotalWealth(marker,
                               reinterpret_cast<const u8*>(objective)) : 0;
        i32 disp = h.moneyConvertToDisplayCoord
                       ? h.moneyConvertToDisplayCoord(wealth, ccy) : 0;
        return disp >= threshold;                       // setnl (0x539a1b)
    }

    // case 8 — CheckObjectCount(person, row, objective).
    case 8:
        return h.checkObjectCount ? h.checkObjectCount(person, row, objective)
                                  : false;

    // cases 11,19,23,28,40 — objective[+0x1C] (value) >= threshold.
    case 11: case 19: case 23: case 28: case 40:
        return objective->value >= threshold;           // setnl (0x539a99)

    // case 12 — CheckBuildingEquip(person).
    case 12:
        return h.checkBuildingEquip ? h.checkBuildingEquip(person) : false;

    // cases 15,16,17,18,34,35 — person[+0x166] == threshold, gated through timer.
    case 15: case 16: case 17: case 18: case 34: case 35: {
        u8 state = h.personStateByte ? h.personStateByte(person) : 0;  // [ecx+166h]
        bool met = (static_cast<i32>(state) == threshold);             // setz (0x539a78)
        return h.accumulateTimer
                   ? h.accumulateTimer(objective, met, timerMin) : false;
    }

    // cases 20,25,36 — family[+0x2C](dword[11]) + family[+0x34](dword[13]); convert; > threshold.
    case 20: case 25: case 36: {
        const i32* fam = h.personGetFamilyRecord ? h.personGetFamilyRecord(person)
                                                 : nullptr;
        if (!fam) return false;                          // jz loc_539966
        i32 disp = h.moneyConvertToDisplayCoord
                       ? h.moneyConvertToDisplayCoord(fam[11] + fam[13], ccy) : 0;
        return disp > threshold;                          // setnle (0x539af4)
    }

    // cases 26,37 — family[+0x30] (dword[12]); convert; > threshold.
    case 26: case 37: {
        const i32* fam = h.personGetFamilyRecord ? h.personGetFamilyRecord(person)
                                                 : nullptr;
        if (!fam) return false;                           // jz loc_539966
        i32 disp = h.moneyConvertToDisplayCoord
                       ? h.moneyConvertToDisplayCoord(fam[12], ccy) : 0;
        return disp > threshold;                           // setnle (0x539b27)
    }

    // case 27 — family[+0x3C] (dword[15]); convert; > threshold.
    case 27: {
        const i32* fam = h.personGetFamilyRecord ? h.personGetFamilyRecord(person)
                                                 : nullptr;
        if (!fam) return false;                            // jz loc_539966
        i32 disp = h.moneyConvertToDisplayCoord
                       ? h.moneyConvertToDisplayCoord(fam[15], ccy) : 0;
        return disp > threshold;                            // setnle (0x539b58)
    }

    // case 31 — GetCurrencyAmount(person, ccy); convert; >= threshold.
    case 31: {
        i32 amt = h.personGetCurrencyAmount
                      ? h.personGetCurrencyAmount(person, ccy) : 0;
        i32 disp = h.moneyConvertToDisplayCoord
                       ? h.moneyConvertToDisplayCoord(amt, ccy) : 0;
        return disp >= threshold;                            // setnl (0x539a45)
    }

    // case 33 — CheckGuildMemberCount(person, objective, row).
    case 33:
        return h.checkGuildMemberCount
                   ? h.checkGuildMemberCount(person, objective, row) : false;

    // case 38 — CheckStatCombo(person). The binary passes the PERSON record as
    // the selector source (eax=ecx=person at 0x539B89); the leaf reads only its
    // +0 word (owner key) via CountMembersByState, so the person record stands in
    // for the ObjectiveRecord* the hook is typed against.
    case 38:
        return h.checkStatCombo
                   ? h.checkStatCombo(
                         reinterpret_cast<const ObjectiveRecord*>(person))
                   : false;

    // case 39 — CheckMultiStat(person). Same as case 38: eax=ecx=person at
    // 0x539B79; the leaf's selector source is the person record, not the objective.
    case 39:
        return h.checkMultiStat
                   ? h.checkMultiStat(
                         reinterpret_cast<const ObjectiveRecord*>(person))
                   : false;

    // case 41 — CheckOwnPersonRatio(person, row).
    case 41:
        return h.checkOwnPersonRatio ? h.checkOwnPersonRatio(person, row) : false;

    // case 43 — CheckCumulativeStats(person, row).
    case 43:
        return h.checkCumulativeStats ? h.checkCumulativeStats(person, row) : false;

    // case 44 — CheckMemberStats(person, objective).
    case 44:
        return h.checkMemberStats ? h.checkMemberStats(person, objective) : false;

    // case 45 — CheckMinThresholds(person).
    case 45:
        return h.checkMinThresholds ? h.checkMinThresholds(person) : false;

    // case 46 — CheckTimeElapsed(person, row).
    case 46:
        return h.checkTimeElapsed ? h.checkTimeElapsed(person, row) : false;

    // case 47 — CountGuildMembers(0,out); (double)threshold * 0.01 <= out.average.
    case 47: {
        MemberCount out{};                              // [esp+var_24]
        if (h.countGuildMembers) h.countGuildMembers(0, &out);
        double lhs = static_cast<double>(threshold) * static_cast<double>(kFlt623D68);
        return lhs <= static_cast<double>(out.average);  // setbe (0x539c04)
    }

    // case 48 — ComputeWeightedLawScore(person, type-1) == 0.0.
    case 48: {
        double score = h.economyComputeWeightedLawScore
                           ? h.economyComputeWeightedLawScore(
                                 person, static_cast<u8>(type - 1))
                           : 0.0;
        return score == 0.0;                             // fldz/fcompp/setz (0x539c23)
    }

    // case 49 — CheckNoActiveCombat(objective).
    case 49:
        return h.checkNoActiveCombat ? h.checkNoActiveCombat(objective) : false;

    // default (incl. case 42) — false (def_539954).
    default:
        return false;
    }
}

}  // namespace guild::world
