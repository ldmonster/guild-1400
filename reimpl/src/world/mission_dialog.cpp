#include "world/mission_dialog.h"

#include "world/mission.h"   // g_missionSlots, kMissionSlotCount

// Faithful port of the recoverable cores of VIBE_Mission_DialogDispatcher
// (0x53b0dc), VIBE_Mission_RunGiveDialog (0x53aea8) and VIBE_Mission_RunAcceptDialog
// (0x53a4dc). The Form/Audio/Text frame loops are the engine's; the dispatch
// decision, the owned-slot scan, the descriptor-derived history seed, and the
// per-button outcome decodes are reproduced here.

namespace guild::world {

// gilde.exe 0x53b0dc — VIBE_Mission_DialogDispatcher.
MissionDialogKind MissionDispatchDialog(i32 giveFlag, i32 activeMissionId,
                                        i32 personMissionId, u8 slotMode) {
    if (giveFlag == -1)                              // dword_764CE0 == -1
        return MissionDialogKind::kGive;
    if (activeMissionId == personMissionId) {        // dword_63CC24 == dword_12CE914[...]
        if (slotMode == 5)                           // byte_63C8F4 == 5
            return MissionDialogKind::kAccept;
        return MissionDialogKind::kOffer;
    }
    if (slotMode == 5)
        return MissionDialogKind::kCompletion;
    return MissionDialogKind::kResult;
}

// gilde.exe 0x53aea8 — owned-mission slot scan (the leading while loop).
int MissionFindOwnedSlot(i32 personId) {
    for (int i = 0; i < kMissionSlotCount; ++i) {
        // !byte_122FEC0[v5*4] (empty slot) -> skip; owner mismatch -> skip.
        if (g_missionSlots[i].type == 0)
            continue;
        if (g_missionSlots[i].owner == personId)     // dword_122FEC4[v5] == personId
            return i;
    }
    return -1;                                        // v5 >= 1152 -> not found
}

// gilde.exe 0x53afbd switch on the RunHistoryRewardDialog return.
MissionGiveOutcome MissionDecodeGive(int historyChoice) {
    if (historyChoice == 0)
        return MissionGiveOutcome::kDecline;          // !v7
    if (historyChoice == -1)
        return MissionGiveOutcome::kAbandon;          // v7 == -1
    return MissionGiveOutcome::kRegister;             // else: v7 is the new type
}

bool MissionGiveTriggersReload(MissionGiveOutcome outcome) {
    return outcome == MissionGiveOutcome::kAbandon;   // v2 -> InitOrLoadSession
}

// gilde.exe 0x53af53 — v16 = byte_63CD4C[v6+1] + 1 == descriptor.category + 1.
int MissionGiveHistorySeed(int descriptorCategory) {
    if (descriptorCategory < 0)
        return -1;                                    // no descriptor matched (v16 = -1)
    return descriptorCategory + 1;
}

// gilde.exe 0x53a830 — only the "later" button sets dword_63CC30/reload.
bool MissionAcceptTriggersReload(MissionAcceptOutcome outcome) {
    return outcome == MissionAcceptOutcome::kLater;
}

} // namespace guild::world
