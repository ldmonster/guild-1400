#pragma once
// Mission dialog flow — the recoverable deterministic cores extracted from the
// VIBE_Mission_Run*Dialog family + the dispatcher (gilde.exe). The dialogs are GUI
// frame loops (VIBE_GameLogic_RunFrameLoop / VIBE_Form_*) routed by the engine;
// what is recoverable and golden-vector testable is the dispatch decision, the
// owned-mission slot scan, the descriptor resolve, the history-reward selection
// seed, and the per-button outcome decode (which command/follow-up fires).
//
// Source functions:
//   VIBE_Mission_DialogDispatcher   0x53b0dc  (route to give/accept/offer/...)
//   VIBE_Mission_RunGiveDialog      0x53aea8  (owned-slot scan + give-outcome decode)
//   VIBE_Mission_RunAcceptDialog    0x53a4dc  (accept-outcome decode)
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// VIBE_Mission_DialogDispatcher 0x53b0dc.
// ===========================================================================
// Picks the sub-dialog from three globals:
//   dword_764CE0 == -1            -> Give
//   else if activeMissionId == personMissionId:
//       byte_63C8F4 == 5 -> Accept   else -> Offer
//   else byte_63C8F4 == 5 -> Completion   else -> Result
// (activeMissionId == dword_63CC24; personMissionId ==
//  dword_12CE914[134*word_63CC5C], the active person's id column.)
enum class MissionDialogKind : int {
    kGive       = 0,
    kAccept     = 1,
    kOffer      = 2,
    kCompletion = 3,
    kResult     = 4,
};
MissionDialogKind MissionDispatchDialog(i32 giveFlag,        // dword_764CE0
                                        i32 activeMissionId,  // dword_63CC24
                                        i32 personMissionId,  // dword_12CE914[...]
                                        u8  slotMode);        // byte_63C8F4

// ===========================================================================
// VIBE_Mission_RunGiveDialog 0x53aea8 — owned-mission slot scan.
// ===========================================================================
// Walks the 128-slot mission table for the first occupied slot whose owner equals
// the active person's id (`personId`), stepping one slot (9 dwords) at a time:
//   while (!byte_122FEC0[v5*4] || dword_122FEC4[v5] != personId) v5 += 9;
//   stop at v5 >= 1152 (128 slots).
// Returns the slot index of the owned mission, or -1 when the person has none.
int MissionFindOwnedSlot(i32 personId);

// ===========================================================================
// VIBE_Mission_RunGiveDialog 0x53aea8 — give-outcome decode.
// ===========================================================================
// After the history-reward dialog returns a code v7, the give path branches:
//   v7 == 0   -> decline   (byte_63C8F4 = -1, send {personId, flag 0})
//   v7 == -1  -> abandon    (byte_63C8F4 = -1, send {personId, flag 1}, reload)
//   else      -> register a new mission of type v7 for personId, send {personId, 0}
// `reload` (set in the abandon branch) drives the InitOrLoadSession at the end.
enum class MissionGiveOutcome : int {
    kDecline   = 0,   // v7 == 0
    kAbandon   = 1,   // v7 == -1  (triggers session reload)
    kRegister  = 2,   // else      (v7 is the new mission type)
};
MissionGiveOutcome MissionDecodeGive(int historyChoice);
// True when the give outcome reloads the session (abandon path).
bool MissionGiveTriggersReload(MissionGiveOutcome outcome);

// History-reward selection seed the give dialog passes to RunHistoryRewardDialog:
//   v16 = byte_63CD4C[descriptorIndex + 1] + 1  ==  descriptor.category + 1
// (-1 when no descriptor matched the owned slot's type). `descriptorCategory` is
// the matched EventDesc.category (+5); pass <0 to mean "no descriptor".
int MissionGiveHistorySeed(int descriptorCategory);

// ===========================================================================
// VIBE_Mission_RunAcceptDialog 0x53a4dc — accept-outcome decode.
// ===========================================================================
// Three buttons drive the accept dialog (button object ids resolved at runtime):
//   accept  -> register a new mission (PickRandomByType(slotMode)), send {id,0}
//   decline -> byte_63C8F4 = -1, send {id, flag 0}
//   later   -> byte_63C8F4 = -1, send {id, flag 1, ...}, set reload
// Only the "later" path sets dword_63CC30=1 and reloads the session.
enum class MissionAcceptOutcome : int {
    kAccept  = 0,
    kDecline = 1,
    kLater   = 2,   // triggers session reload
};
// True when the accept outcome reloads the session (the "later" button).
bool MissionAcceptTriggersReload(MissionAcceptOutcome outcome);

} // namespace guild::world
