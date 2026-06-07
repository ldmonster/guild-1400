#include "world/privilege_cmd.h"

#include "crt/rand.h"

// Faithful 1:1 port of the privilege command issuers (gilde.exe 0x561700 /
// 0x561a74). The two dispatchers share the same shape: subjectKind 6/7 opens the
// office-overview window (a different interaction); otherwise the action targets a
// concrete person (resolved via FindRecordById(*(a2+532))) and enqueues a command.
// SendSimpleCmd additionally rolls two RandomModulo(9) text ids before emitting.
//   VIBE_Math_RandomModulo(9) == (int)RandNext() % 9  (rand.h crt LCG).

namespace guild::world {

namespace {
constexpr int kCmdLogCap = 16;
PrivilegeCommand g_cmdLog[kCmdLogCap];
int g_cmdLogCount = 0;
void DefaultHook(const PrivilegeCommand& c, void*) {
    if (g_cmdLogCount < kCmdLogCap) g_cmdLog[g_cmdLogCount++] = c;
}
PrivilegeCommandHook g_hook = &DefaultHook;
void* g_ctx = nullptr;

// VIBE_Math_RandomModulo(9): (int)RandNext() % 9 (0 if arg is 0; arg is 9 here).
int RandomModulo9() { return guild::crt::RandNext() % 9; }
} // namespace

void PrivilegeSetCommandHook(PrivilegeCommandHook hook, void* ctx) {
    g_hook = hook ? hook : &DefaultHook;
    g_ctx = hook ? ctx : nullptr;
}
void PrivilegeCommandLogReset() {
    g_hook = &DefaultHook; g_ctx = nullptr; g_cmdLogCount = 0;
}
const PrivilegeCommand* PrivilegeCommandLog(int* outCount) {
    if (outCount) *outCount = g_cmdLogCount;
    return g_cmdLog;
}

// gilde.exe 0x561700 — VIBE_Privilege_SendSimpleCmd.
int PrivilegeSendSimpleCmd(const PrivilegeDispatch& d, int* outTextLow,
                           int* outTextHigh) {
    if (d.subjectKind == 6 || d.subjectKind == 7) {
        // v32 = 2; office-overview window. On an immune pick (+457 & 4) -> -127.
        if (d.officeHolderPicked &&
            PrivilegeHasFlag(d.officeHolderFlags457, kPrivBitBlackmailImmune))
            return kPrivRetImmune; // -127
        return kPrivRetSimpleOffice; // 2
    }
    if (!d.targetResolves)
        return kPrivRetNoTarget; // 96

    // Roll the two text ids (order matches the original: v31 then v30).
    int textHigh = RandomModulo9() + 6518; // v31
    int textLow  = RandomModulo9() + 6509; // v30
    if (outTextLow)  *outTextLow  = textLow;
    if (outTextHigh) *outTextHigh = textHigh;

    PrivilegeCommand cmd{90, d.actorId, d.targetId, 0, textLow, textHigh};
    g_hook(cmd, g_ctx);
    return kPrivRetCommitted; // 16
}

// gilde.exe 0x561a74 — VIBE_Privilege_SendBuildCmd.
int PrivilegeSendBuildCmd(const PrivilegeDispatch& d) {
    if (d.subjectKind == 6 || d.subjectKind == 7) {
        return d.overviewShown ? kPrivRetBuildOffice : kPrivRetBuildCancel; // 1 / 0
    }
    if (!d.targetResolves)
        return kPrivRetNoTarget; // 96

    PrivilegeCommand cmd{90, d.actorId, d.targetId, -2, 0, 0};
    g_hook(cmd, g_ctx);
    return kPrivRetCommitted; // 16
}

} // namespace guild::world
