// =====================================================================================
// office_recon2_rules.cpp — implementations for office_recon2_rules.h.
// 1:1 translation of the portable decision logic of the listed gilde.exe functions.
// Coupled engine leaves are left to caller hooks (see header). No existing global,
// record, or table is redefined here.
// =====================================================================================
#include "world/office_recon2_rules.h"

namespace guild::world {

// -------------------------------------------------------------------------------------
// 0x57c1e8 — VIBE_Office_ResolveStaffModel
// -------------------------------------------------------------------------------------
StaffModelResult OfficeResolveStaffModel(const StaffModelInputs& in,
                                         int* roundRobinPrev,
                                         StaffRandFn rng,
                                         void* rngUser) {
    StaffModelResult out;

    // Outer gate: staffFlagA set, OR curStaffCount >= staffThreshold (double compare,
    // matching: (double)*(u16*)(...) >= flt_12CE930). /*0x57c2f3 / 0x57c2f9*/
    const bool mainPath =
        in.staffFlagA || ((double)in.curStaffCount >= (double)in.staffThreshold);

    if (!mainPath) {
        // Under-threshold default model. /*0x57c2f9..0x57c311*/
        if (in.genderByte /* LOBYTE != 0 */) {
            out.kind = StaffModelResult::kUnderThreshMale;   // unk_63DAA0 /*0x57c311*/
        } else {
            out.kind = StaffModelResult::kUnderThreshFemale; // unk_63DA78 /*0x57c302*/
        }
        return out;
    }

    // Main path. /*0x57c222*/
    // Round-robin name-copy branch fires when staffFlagC set AND roleIdField != -1.
    // (Original: if (!byte_12CEB00 || dword_12CEA9C == -1) -> table path else RR.)
    if (in.staffFlagC && in.roleIdField != -1) {
        // /*0x57c25e*/ dword_641FE8 = (dword_641FE8 + 1) % 8; copies the entity name and
        // role tag into scratch unk_1234610 + 40*slot. The scratch contents are engine
        // data; we report the slot so the caller can populate it.
        int slot = 0;
        if (roundRobinPrev) {
            *roundRobinPrev = (*roundRobinPrev + 1) % 8;
            slot = *roundRobinPrev;
        }
        out.kind = StaffModelResult::kRoundRobinName;
        out.roundRobinSlot = slot;
        return out;
    }

    // Table-scan path. /*0x57c336*/
    if (in.buildingType == 17) {
        // return unk_6405E8 + 40 * (rand % 3) /*0x57c3bd*/
        out.kind = StaffModelResult::kType17;
        out.randIndex = rng ? (int)(unsigned)rng(3u, rngUser) : 0;
        return out;
    }

    if (in.staffFlagA) {                       // /*0x57c338*/
        // gender selects table base; scan cap 76, compare selectorA.
        const StaffModelTable& tab =
            (in.genderByte == 0) ? in.fullFemale   // unk_63E338 /*0x57c34f*/
          : (in.genderByte == 1) ? in.fullMale     // unk_63EF18 /*0x57c3c3*/
                                 : StaffModelTable{}; // v2 stays 0
        const StaffModelRecord* base = tab.records;
        int v3 = 0;
        // for (result=base; v3<76 && *(DWORD*)result; result+=40) /*0x57c36c*/
        if (base) {
            const StaffModelRecord* result = base;
            while (v3 < 76 && result->roleKey != 0) {
                if (in.selectorA == result->roleKey) {  // /*0x57c388*/
                    out.kind = StaffModelResult::kTableRecord;
                    out.record = result;
                    return out;
                }
                ++v3;
                ++result;
            }
        }
        // return &v2[40*v3]  /*0x57c3e1*/  (record index v3 within the same table)
        out.kind = StaffModelResult::kTableRecord;
        out.record = base ? (base + v3) : nullptr;
        if (!base) out.kind = StaffModelResult::kNone; // v2==0 -> &(0)[..] == 0
        return out;
    }

    if (in.staffFlagB) {                       // /*0x57c3e2*/
        const StaffModelTable& tab =
            (in.genderByte == 0) ? in.shortFemale  // unk_63DAC8 /*0x57c3f5*/
          : (in.genderByte == 1) ? in.shortMale    // unk_63DF00 /*0x57c43e*/
                                 : StaffModelTable{};
        const StaffModelRecord* base = tab.records;
        int v3 = 0;
        if (base) {
            const StaffModelRecord* result = base;
            while (v3 < 27 && result->roleKey != 0) {  // /*0x57c412*/
                if (in.selectorB == result->roleKey) { // /*0x57c42d*/
                    out.kind = StaffModelResult::kTableRecord;
                    out.record = result;
                    return out;
                }
                ++v3;
                ++result;
            }
        }
        if (v3 >= 27) {
            // fall through to LABEL_39 (by-gender fallback) /*0x57c448*/
        } else {
            // return &v2[40*v3] /*0x57c3e1*/
            out.kind = StaffModelResult::kTableRecord;
            out.record = base ? (base + v3) : nullptr;
            if (!base) out.kind = StaffModelResult::kNone;
            return out;
        }
    }
    // else (neither A nor B): goto LABEL_39 /*0x57c3e2*/

    // LABEL_39 — by-gender base fallback. /*0x57c459*/
    if (in.genderByte == 0) {
        out.kind = StaffModelResult::kTableRecord;
        out.record = in.shortFemale.records; // unk_63DAC8 /*0x57c463*/
        if (!out.record) out.kind = StaffModelResult::kNone;
    } else if (in.genderByte == 1) {
        out.kind = StaffModelResult::kTableRecord;
        out.record = in.shortMale.records;    // unk_63DF00 /*0x57c47e*/
        if (!out.record) out.kind = StaffModelResult::kNone;
    } else {
        // return v2 — which, on the A/B-failed fall-through, is 0. /*0x57c473*/
        out.kind = StaffModelResult::kNone;
    }
    return out;
}

// -------------------------------------------------------------------------------------
// 0x47e6c4 — VIBE_Office_AddEntryDefault
//   return VIBE_Office_AddTableEntry(al, 0, 3, 0, 255);   /*0x47e6e1*/
// -------------------------------------------------------------------------------------
int OfficeAddEntryDefault(u8 holderKey, const OfficeAddTableEntryHook& hook) {
    if (!hook.fn) return -1; // inert default: no command path wired
    return hook.fn(holderKey, /*primaryId*/ 0, /*officeType*/ 3,
                   /*succId*/ 0, /*flag*/ 255, hook.user);
}

// -------------------------------------------------------------------------------------
// 0x49da18 — VIBE_Office_SpawnSessionActor : model-name fallback selection.
//   v7 = RandInt(3): 1 -> "buerger_MANN" ; 2 -> "buerger2_MANN" ; else -> "handwerker3_MANN"
//   (/*0x49daab..0x49dafd*/; strings 0x61C388 / 0x61C398 / 0x61C3A8)
// -------------------------------------------------------------------------------------
const char* OfficeSpawnActorFallbackModel(int rand3) {
    if (rand3 == 1) return "buerger_MANN";    // aBuergerMann   /*0x49daad*/
    if (rand3 == 2) return "buerger2_MANN";   // aBuerger2Mann  /*0x49daf6*/
    return "handwerker3_MANN";                // aHandwerker3Man/*0x49dafd*/
}

// -------------------------------------------------------------------------------------
// 0x51fc50 — VIBE_Office_ShowCandidacyDialog : rules.
// -------------------------------------------------------------------------------------
int OfficeCandidacyCountCandidates(const u8* officeTypeByOf, int entityCount, u8 officeKey) {
    // for (idx=0; idx<entityCount && bytesUsed<16; ++idx)
    //   if (record valid && record+360 == officeKey) { bytesUsed+=4; ++count; }
    // (Original: word_12CE910[..]!=-1 validity check; here the caller passes the
    //  pre-filtered office-type byte per slot, sentinel 0xFF for invalid.) /*0x51fcce..0x51fd01*/
    if (!officeTypeByOf) return 0;
    int count = 0;
    int bytesUsed = 0;
    for (int idx = 0; idx < entityCount && bytesUsed < 16; ++idx) {
        const u8 ot = officeTypeByOf[idx];
        if (ot == 0xFF) continue;          // invalid record marker (word == -1)
        if (ot == officeKey) {             // entity+360 == officeKey /*0x51fce2*/
            bytesUsed += 4;
            ++count;
        }
    }
    return count;
}

bool OfficeCandidacyApplyEnabled(int candidateCount, bool holderHasOffice) {
    // if (v35 >= 4 || *(holder+360)) enabled=0 else enabled=1   /*0x51ffc2/0x51ffcf*/
    return !(candidateCount >= 4 || holderHasOffice);
}

void OfficeCandidacyCardLayout(int index, int formWidthRaw, int marginRaw,
                               int* outX, int* outY) {
    // v11 = (formW>>16) - 2*(margin>>16)              /*0x51fe39*/
    // v33 = v11 / 3 ; v34 = (v11 % 3) / 2
    // x = v34 + v33*(i%2+1) + 10*(i%2-1) + (margin>>16)*(i%2)   /*0x51feb5*/
    // y = 130*(i/2) + 40                                          /*0x51fece*/
    const int formW  = formWidthRaw >> 16;
    const int margin = marginRaw >> 16;
    const int usable = formW - 2 * margin;
    const int colW   = usable / 3;
    const int extra  = (usable % 3) / 2;
    const int p      = index % 2;
    if (outX) *outX = extra + colW * (p + 1) + 10 * (p - 1) + margin * p;
    if (outY) *outY = 130 * (index / 2) + 40;
}

// -------------------------------------------------------------------------------------
// 0x4a03f4 / 0x4a04f4 — VIBE_Office_PrepareSuccessorChoice : gate.
// -------------------------------------------------------------------------------------
SuccessorChoice OfficePrepareSuccessorChoice(const SuccessorGateInputs& in) {
    // if (entity+156 != 0) -> abort (original: only proceeds when HIDWORD(a1)==0) /*0x4a041c*/
    if (!in.entitySlot156Zero) return SuccessorChoice::kAbort;
    // holder = FindRecordById(entity+16); if !holder -> abort /*0x4a0433*/
    if (!in.holderValid) return SuccessorChoice::kAbort;
    // if (!(holder+358) || (holder+433)) -> abort /*0x4a043e*/
    if (!in.holderHoldsOffice || in.holderBlocked) return SuccessorChoice::kAbort;
    // dispatch on slot-type
    if (in.slotType == 1) {                          // /*0x4a044f*/
        return in.twoPersonsValid ? SuccessorChoice::kTwoPerson : SuccessorChoice::kAbort;
    }
    if (in.slotType == 2) {                          // /*0x4a0454*/
        return in.nextRankInCategory ? SuccessorChoice::kCategory : SuccessorChoice::kAbort;
    }
    return SuccessorChoice::kAbort;                  // other slot-types: no dialog
}

// -------------------------------------------------------------------------------------
// 0x5210e4 / 0x521234 — Guild Level3 dialog body text-id.
//   base = (genderByteLow ? 560 : 525) + officeNameByte   /*0x52116a / 0x5212ba*/
// -------------------------------------------------------------------------------------
int GuildLevel3DialogBodyTextId(bool genderByteLow, u8 officeNameByte) {
    return (genderByteLow ? 560 : 525) + (int)officeNameByte;
}

// -------------------------------------------------------------------------------------
// 0x56499c — VIBE_Privilege_BuildOfficeMemberTable : portable pieces.
// -------------------------------------------------------------------------------------
// Both amount paths use VIBE_Coord_ConvertX, which truncates the FP result to int.
// Factors dbl_624D64 and flt_624D6C are both 0.01 (verified via get_bytes).
i32 PrivilegeMiracleAmountCase0(int rand3, i32 wealth) {
    // v69 = rand%3 + 2 ; v6 = (double)v69 * ((double)wealth * 0.01) ; (int)v6 /*0x564a63..0x564a9c*/
    const int mult = rand3 + 2;
    const double v = (double)mult * ((double)wealth * 0.01);
    return (i32)v;
}

i32 PrivilegeMiracleAmountCase5(int rand5, i32 wealth) {
    // v67 = rand%5 + 3 ; v28 = (double)v67 * ((double)wealth * (float)0.01) /*0x564c88..0x564cca*/
    const int mult = rand5 + 3;
    const double v = (double)mult * ((double)wealth * (double)(float)0.01f);
    return (i32)v;
}

int PrivilegeMiraclePickLeastWealthy(const i32* wealth, int count) {
    // v64 = wealth[0]; v24 = cand[0];
    // for (j=1; j<count; ++j) if (wealth[j] < v64) { v64 = wealth[j]; v24 = cand[j]; }
    // (Original keeps the running minimum; returns the index here.) /*0x564bf6..0x564c4c*/
    if (!wealth || count <= 0) return -1;
    int best = 0;
    i32 bestW = wealth[0];
    for (int j = 1; j < count; ++j) {
        if (wealth[j] < bestW) {  // strict < (original: v27 < v26) /*0x564c37*/
            bestW = wealth[j];
            best = j;
        }
    }
    return best;
}

} // namespace guild::world
