#include "world/privilege.h"

// Translation unit for the privilege subsystem. The bit-check primitives are
// header-inline (see privilege.h). The ~30 VIBE_Privilege_* panels and
// VIBE_Panel_ShowPrivileges (0x55fe60) are GUI/network dialogs and are DEFERRED;
// they call Form_*/Text_*/Command_* and read live sim Person bitfields.
//
// DEFERRED privilege panels (address — reason):
//   VIBE_Panel_ShowPrivileges        0x55fe60  GUI grid + per-privilege dispatch
//   VIBE_Privilege_ShowDialog        0x571218  Form/Text dialog runner
//   VIBE_Privilege_SendSimpleCmd     0x561700  network command issue (gate ported)
//   VIBE_Privilege_SendBuildCmd      0x561a74  network command issue
//   VIBE_Privilege_Panel{Medicus,Divorce,Blackmail,LawScroll,InstillFear,
//     EnactLaw,RemoveFromOffice,Embezzlement,Charm,CounterEspionage,SwapSeats,
//     GenerateHatred,Interrogation,ExpelWorker,MakePeace,Convert,Apology,
//     Miracle,ChangeProfession,Evidence*}                all GUI dialogs

namespace guild::world {

// Anchor a compile-time sanity check for the recovered masks.
static_assert(kPrivBitBlackmailImmune == 0x4,   "immune mask");
static_assert(kPrivBitHoldsOffice     == 0x100, "office mask");

// gilde.exe 0x561700 — VIBE_Privilege_SendSimpleCmd return-code rule.
int PrivilegeSimpleCmdResult(int subjectKindByte, bool officeHolderImmune,
                             bool targetExists) {
    if (subjectKindByte == 6 || subjectKindByte == 7) {
        // office-overview path: v32 starts at 2, becomes -127 on an immune pick.
        return officeHolderImmune ? kPrivRetImmune : kPrivRetSimpleOffice;
    }
    return targetExists ? kPrivRetCommitted : kPrivRetNoTarget;
}

// gilde.exe 0x561a74 — VIBE_Privilege_SendBuildCmd return-code rule.
int PrivilegeBuildCmdResult(int subjectKindByte, bool overviewShown,
                            bool targetExists) {
    if (subjectKindByte == 6 || subjectKindByte == 7) {
        // office-overview path: returns 1 when the overview ran, else 0.
        return overviewShown ? kPrivRetBuildOffice : kPrivRetBuildCancel;
    }
    return targetExists ? kPrivRetCommitted : kPrivRetNoTarget;
}

} // namespace guild::world
