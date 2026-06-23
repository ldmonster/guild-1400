#pragma once
// Mission requirement / reward rules — the recoverable data-rules cores extracted
// from the VIBE_Mission_Run*Dialog family (gilde.exe). The dialogs themselves are
// GUI frame loops (VIBE_Form_*/VIBE_GameLogic_RunFrameLoop) and are left to the
// engine; what is recoverable and deterministic is:
//   * the descriptor lookup-by-value (stride-24 scan of byte_63CD4C) the special /
//     completion / reward-summary dialogs use to find the active mission's record
//   * the "already-completed history slots" skip count the history-reward dialog
//     derives from its selection index
//   * the result/completion outcome decode (which follow-up dialog/flag fires)
// These build on the shared descriptor table in world/event.h.
#include "guild/common/types.h"

namespace guild::world {

struct EventDesc;   // world/event.h (the 24-byte descriptor record)

// ---------------------------------------------------------------------------
// Descriptor lookup by value  (gilde.exe 0x5387f1 / 0x53ac81 inner loops).
// ---------------------------------------------------------------------------
// Both VIBE_Mission_RunSpecialDialog and VIBE_Mission_RunCompletionDialog locate
// the active mission's descriptor by scanning the table for the entry whose +4
// value byte (byte_63CD4C[24*i]) equals the wanted value, walking 24 bytes at a
// time and stopping at 24*dword_5383F0. Returns the matching table index, or -1.
// (RunSpecialDialog keys on the dialog's argument byte; RunCompletionDialog keys
// on byte_122FEC0[0], the active mission slot's type.)
int MissionFindDescriptorByValue(u8 value);

// Convenience: returns the matched descriptor index and the text/voice fields the
// special-mission and reward-summary dialogs render. The dialogs key off the +4
// value byte address (v15 = &byte_63CD4C[v4]); the text id lives one dword past it
// (rec+8 == paramA) and each dialog uses a different voice-suffix dword:
//   nameTextId        = *(int*)(rec+8) + 1   RunSpecialDialog VIBE_Text(*(v15+1)+1)
//   bodyTextId        = *(int*)(rec+8) + 2   RunRewardSummary VIBE_Text(*(v42+4)+2)
//   specialVoiceIndex = *(int*)(rec+0x0C)    RunSpecialDialog "_AUFTRAEGE_VERGABE_HS_%.2d"
//   rewardVoiceIndex  = *(int*)(rec+0x10)    RunRewardSummary "_AUFTRAEGE_ERFOLG_HS_%.2d"
// Returns false (and leaves outputs untouched) when no descriptor matches.
struct MissionRewardInfo {
    int descriptorIndex;     // matched table index
    int nameTextId;          // rec.paramA (+8) used as a text id, + 1
    int bodyTextId;          // rec.paramA (+8) used as a text id, + 2
    int specialVoiceIndex;   // rec.paramB (+0x0C) special-dialog voice suffix
    int rewardVoiceIndex;    // rec.paramC (+0x10) reward-summary voice suffix
};
bool MissionResolveReward(u8 value, MissionRewardInfo* out);

// ---------------------------------------------------------------------------
// History-reward skip count  (gilde.exe 0x538ec6.. in RunHistoryRewardDialog).
// ---------------------------------------------------------------------------
// The history-reward dialog disables one radio option per already-completed
// history slot and seeds the radio group's selection index from that count:
//   a1 > 0 -> disable opt1, sel = 2
//   a1 > 1 -> disable opt2, ++sel
//   a1 > 2 -> disable opt3, ++sel
//   a1 > 3 -> disable opt4, ++sel
//   a1 > 4 -> disable opt5, sel = 0   (all consumed; reset to top)
// Returns the seed selection index for `completed` already-done slots (0..6).
int MissionHistoryRewardSelection(int completed);

// gilde.exe 0x538f8c.. RunHistoryRewardDialog: maps the chosen radio object id to
// the byte_63C8F4 mode the follow-up RunChooseMissionDialog receives. The mapping
// (by option index 0..6) is:
//   opt0 (cancel) -> -1   opt1 -> 0   opt2 -> 1   opt3 -> 2   opt4 -> 3
//   opt5         -> 4    opt6 (back) -> -1
// Returns the mode byte (as int; -1 == cancel) for the chosen option index.
int MissionHistoryRewardMode(int optionIndex);

// ---------------------------------------------------------------------------
// Completion outcome decode  (gilde.exe 0x53ad50 switch in RunCompletionDialog).
// ---------------------------------------------------------------------------
// After the completion dialog's frame loop, an outcome code selects the follow-up:
//   1 -> failure dialog   2 -> info dialog   3 -> load-session (set dword_63CC30)
//   else -> nothing
enum class MissionCompletionOutcome : int {
    kNone        = 0,
    kFailure     = 1,
    kInfo        = 2,
    kLoadSession = 3,
};
MissionCompletionOutcome MissionDecodeCompletion(int outcomeCode);

// gilde.exe 0x53ae6a RunResultDialog: result==2 (== (dword_63CC30!=0)+1 when the
// reward was claimed) triggers the load-session path. Returns true when the
// result code indicates the session should reload.
bool MissionResultIsLoadSession(int resultCode);

} // namespace guild::world
