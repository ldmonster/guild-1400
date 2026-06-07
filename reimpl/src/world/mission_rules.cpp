#include "world/mission_rules.h"

#include "world/event.h"   // g_eventTable / g_eventTableCount (descriptor table)

// Faithful port of the recoverable rule cores buried in the VIBE_Mission_Run*Dialog
// family. The GUI frame loops are the engine's; the deterministic logic — the
// descriptor lookup-by-value, the history-reward selection seed, the option->mode
// map, and the completion/result outcome decode — is reproduced here.

namespace guild::world {

// gilde.exe 0x5387f1 / 0x53ac81 — scan byte_63CD4C (the +4 value byte) for `value`,
// stride 24, up to 24 * dword_5383F0. byte_63CD4C[24*i] == g_eventTable[i].value.
int MissionFindDescriptorByValue(u8 value) {
    if (g_eventTableCount <= 0)
        return -1;
    for (int i = 0; i < g_eventTableCount; ++i) {
        if (g_eventTable[i].value == value)
            return i;
    }
    return -1;
}

bool MissionResolveReward(u8 value, MissionRewardInfo* out) {
    int idx = MissionFindDescriptorByValue(value);
    if (idx < 0)
        return false;
    const EventDesc& rec = g_eventTable[idx];
    out->descriptorIndex = idx;
    // RunSpecialDialog: VIBE_Text_RenderRichString(*((_DWORD*)v14 + 1) + 1) — the
    // dword at rec+4 used as a text id, +1; RunRewardSummary uses the same dword +2.
    out->nameTextId = static_cast<int>(rec.value) + 1;   // rec+4 (+1)
    out->bodyTextId = static_cast<int>(rec.value) + 2;   // rec+4 (+2)
    // RunRewardSummary / RunSpecialDialog voice suffix: *(_DWORD*)(v37 + 12) == rec+0x0C.
    out->voiceIndex = rec.paramB;                        // rec+0x0C
    return true;
}

// gilde.exe 0x538ec6.. — history-reward radio selection seed.
int MissionHistoryRewardSelection(int completed) {
    int sel = 0;                       // v12 starts from the radio-group default
    if (completed > 0) sel = 2;        // a1 > 0: disable opt1, sel = 2
    if (completed > 1) ++sel;          // a1 > 1: ++sel
    if (completed > 2) ++sel;          // a1 > 2: ++sel
    if (completed > 3) ++sel;          // a1 > 3: ++sel
    if (completed > 4) sel = 0;        // a1 > 4: all consumed, reset to top
    return sel;
}

// gilde.exe 0x538f8c.. — chosen option index -> byte_63C8F4 mode.
int MissionHistoryRewardMode(int optionIndex) {
    switch (optionIndex) {
        case 1: return 0;   // opt1 (ChildObjectId+1 == v16)
        case 2: return 1;   // opt2 (v2)
        case 3: return 2;   // opt3 (v3)
        case 4: return 3;   // opt4 (v4)
        case 5: return 4;   // opt5 (v14)
        default: return -1; // opt0 (ChildObjectId) and opt6 (v13) -> cancel/back
    }
}

// gilde.exe 0x53ad50 switch.
MissionCompletionOutcome MissionDecodeCompletion(int outcomeCode) {
    switch (outcomeCode) {
        case 1: return MissionCompletionOutcome::kFailure;
        case 2: return MissionCompletionOutcome::kInfo;
        case 3: return MissionCompletionOutcome::kLoadSession;
        default: return MissionCompletionOutcome::kNone;
    }
}

// gilde.exe 0x53ae6a — v6 == 2 -> load session.
bool MissionResultIsLoadSession(int resultCode) {
    return resultCode == 2;
}

} // namespace guild::world
