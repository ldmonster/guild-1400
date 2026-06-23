#include "sim/command_recon_syncrange.h"

namespace guild::sim {

// gilde.exe 0x493a1c — VIBE_Command_MarkSyncRangeStart
//   dword_11AA484 = dword_11AA494 + 1;   /*0x493a22*/
//   return dword_11AA494 + 1;            /*0x493a27*/
u32 MarkSyncRangeStart(SyncRangeState& s, u32 sendCount) {
    s.start = sendCount + 1;   // 32-bit wraparound preserved (u32 add)
    return sendCount + 1;
}

// gilde.exe 0x493a28 — VIBE_Command_MarkSyncRangeEnd
//   dword_11AA47C = dword_11AA494 + 1;   /*0x493a2e*/
//   return dword_11AA494 + 1;            /*0x493a33*/
u32 MarkSyncRangeEnd(SyncRangeState& s, u32 sendCount) {
    s.end = sendCount + 1;     // 32-bit wraparound preserved (u32 add)
    return sendCount + 1;
}

} // namespace guild::sim
