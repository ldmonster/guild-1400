#pragma once
// Privilege command dispatch: the load-bearing bodies of VIBE_Privilege_SendSimpleCmd
// (0x561700) and VIBE_Privilege_SendBuildCmd (0x561a74) — the two privilege-action
// command issuers shared by the ~30 GUI panels. Faithful 1:1 port of the decision
// + command-emit core. The GUI portions (Form/Text rendering, the office-overview
// window) are stubbed/forwarded; the deterministic dispatch + the network-command
// emit are recovered.
//
// The privilege CHECK rules (the (person+457) & 4 immune bit and the subjectKind
// 6/7 office-overview branch) live in privilege.h (header-inline, reused here).
//
// Translated functions:
//   VIBE_Privilege_SendSimpleCmd  0x561700  (dispatch + RandomModulo rolls + emit)
//   VIBE_Privilege_SendBuildCmd   0x561a74  (dispatch + emit)
#include "guild/common/types.h"
#include "world/privilege.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Command hook (mock) — the privilege command the issuers enqueue.
// ---------------------------------------------------------------------------
// SendSimpleCmd packs build-op 90 (slot reset 28 + op90) with two random text ids
// (RandomModulo(9)+6509 / +6518); SendBuildCmd packs build-op 90 with op = -2.
struct PrivilegeCommand {
    int opcode;        // 90
    i32 actorId;       // *(rec+4) of the acting office holder
    i32 targetId;      // *(targetRec+4) (the resolved target person)
    int op;            // SendBuildCmd passes -2; SendSimpleCmd 0
    int textIdLow;     // v30 (RandomModulo(9)+6509); 0 for build cmd
    int textIdHigh;    // v31 (RandomModulo(9)+6518); 0 for build cmd
};
using PrivilegeCommandHook = void (*)(const PrivilegeCommand& cmd, void* ctx);
void PrivilegeSetCommandHook(PrivilegeCommandHook hook, void* ctx);
void PrivilegeCommandLogReset();
const PrivilegeCommand* PrivilegeCommandLog(int* outCount);

// ---------------------------------------------------------------------------
// Dispatch inputs (modeling the two pointers a1/a2 the originals receive).
// ---------------------------------------------------------------------------
// The acting office holder is read at a1: byte +2 == subjectKind, +4 == actor id.
// The target is resolved via VIBE_Person_FindRecordById(*(a2+532)); we surface
// the resolved presence + its +4 id directly.
struct PrivilegeDispatch {
    u8  subjectKind;   // *(a1+2)  (6/7 -> office overview path)
    i32 actorId;       // *(a1+4)
    bool targetResolves; // VIBE_Person_FindRecordById(*(a2+532)) != null
    i32 targetId;      // *(targetRec+4) when targetResolves
    // Office-overview-path inputs (subjectKind 6/7):
    bool officeHolderPicked; // RunOfficeOverviewWindow returned a holder
    u32  officeHolderFlags457; // that holder's +457 bitfield (immune bit 4)
    bool overviewShown;      // SendBuildCmd: overview returned nonzero
};

// gilde.exe 0x561700 — VIBE_Privilege_SendSimpleCmd.
// Office-overview path (subjectKind 6/7): returns -127 if the picked holder is
// immune (+457 & 4), else 2. Concrete-target path: when the target resolves,
// rolls two RandomModulo(9) text ids (via crt::RandNext), emits the command, and
// returns 16; otherwise returns 96. `outTextLow/High` receive the rolled ids.
int PrivilegeSendSimpleCmd(const PrivilegeDispatch& d, int* outTextLow,
                           int* outTextHigh);

// gilde.exe 0x561a74 — VIBE_Privilege_SendBuildCmd.
// Office-overview path: returns 1 if the overview ran, else 0. Concrete-target
// path: when the target resolves, emits the build command (op -2) and returns 16;
// otherwise 96.
int PrivilegeSendBuildCmd(const PrivilegeDispatch& d);

} // namespace guild::world
