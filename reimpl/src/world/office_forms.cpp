#include "world/office_forms.h"

// Faithful 1:1 port of the office/council/trial GUI form-build leaves:
//   VIBE_Office_BuildElectionForm      (gilde.exe 0x4a0610)
//   VIBE_Office_BuildVotePanel         (gilde.exe 0x49dc18)
//   VIBE_Office_AddVoteMarker          (gilde.exe 0x49dbe8)
//   VIBE_Office_BuildSuccessorDialogA  (gilde.exe 0x4a003c)
//   VIBE_Office_BuildSuccessorDialogB  (gilde.exe 0x4a01a4)
//   VIBE_Office_BuildTortureChoiceForm (gilde.exe 0x4a3dc8)
//
// Recovered here: the widget tree + content (text ids, marker/button geometry) +
// button wiring (click id -> choice). The 3D scene playback, glyph blit, modal
// frame loop (VIBE_GameLogic_RunFrameLoop) and voice playback are deferred.

namespace guild::world {

namespace {
// Stable child-object id base so the built buttons have deterministic, testable
// widget ids (the original gets them from VIBE_Form_GetChildObjectId).
constexpr int kButtonObjBase = 3000;
} // namespace

// ===========================================================================
// VIBE_Office_BuildVotePanel (gilde.exe 0x49dc18) / _AddVoteMarker (0x49dbe8).
// ===========================================================================
void VotePanel::Init() {
    objects.clear();
    // RenderFormattedMessage(3861/3862/3863) -> AddTextLabel -> SetColor(67).
    for (int c = 0; c < 3; ++c) {
        VotePanelObject o{};
        o.kind = VotePanelObject::kHeader;
        o.column = c;
        o.x = kVotePanelColumnX[c];
        o.y = 0;                       // AddTextLabel(0, y, ...) header row
        o.textIdOrIcon = kVotePanelHeaderText[c];
        o.color = kVotePanelHeaderColor;
        objects.push_back(o);
    }
    // dword_11B4E48/4C/50 = 0.
    count[0] = count[1] = count[2] = 0;
}

bool VotePanel::Mark(int code) {
    if (code < 0 || code > 2)          // original only handles a3 == 0/1/2
        return false;
    VotePanelObject o{};
    o.kind = VotePanelObject::kMarker;
    o.column = code;
    o.x = kVotePanelColumnX[code];                 // {32, 62, 47}
    o.y = VotePanelMarkerY(count[code]);           // 10*count + 80
    o.textIdOrIcon = kVotePanelMarkerIcon;         // 1162
    o.color = 0;
    objects.push_back(o);
    ++count[code];                                  // ++dword_11B4E48/4C/50
    return true;
}

// gilde.exe 0x49dbe8 — VIBE_Object_AddToWindow(win, 68 - 10*count, 140, 1162); ++count.
VotePanelObject Office_AddVoteMarker(int& markerCount) {
    VotePanelObject o{};
    o.kind = VotePanelObject::kMarker;
    o.x = OfficeVoteMarkerX(markerCount);  // 68 - 10*markerCount
    o.y = kVoteMarkerY;                    // 140
    o.textIdOrIcon = kVoteMarkerIcon;      // 1162
    ++markerCount;                         // ++*(_DWORD *)(a2 + 44)
    return o;
}

// ===========================================================================
// VIBE_Office_BuildElectionForm (gilde.exe 0x4a0610).
// ===========================================================================
ElectionFormLayout Office_BuildElectionForm(const std::vector<ElectionSeat>& seats,
                                            int speakerSeat,
                                            int failureCode) {
    ElectionFormLayout l{};
    l.failureCode = failureCode;
    l.speakerSeat = speakerSeat;

    if (failureCode != 0) {
        // Failure path: a single status text. Codes 1..7 select 3642..3649; codes
        // 6/7 are the speaker-specific texts. (The original case 6 -> 3649, 7 -> 3648.)
        if (failureCode >= 1 && failureCode <= 7)
            l.statusText = kElectionFailText[failureCode];
        return l;
    }

    // Success path: one speech line per "speaking" seat (role 6 or 7). Seats 0..2
    // and 5 emit a BuildSpeechPacket line; seats 3,4 send the shared body via the
    // He message channel (1418). The matched speaker seat is skipped for the He
    // sends (v5 != seat guard) but still produces its packet line.
    for (int i = 0; i < kElectionSeatCount && i < static_cast<int>(seats.size()); ++i) {
        const ElectionSeat& s = seats[i];
        if (s.personHandle == -1 && s.textId == 0 && !s.present)
            continue; // FindRecordById failed (null seat)
        if (s.role != kElectionSpeakRoleA && s.role != kElectionSpeakRoleB)
            continue; // only roles 6/7 speak

        ElectionSpeechLine line{};
        line.seat    = i;
        line.textId  = kElectionSeatText[i];      // 3634/3635/3636/3637/3637/3639
        line.speaker = s.personHandle;
        // Seats 0,1,2,5 go through BuildSpeechPacket; seats 3,4 share the body and
        // are sent via He_SendEntityMessage when they are not the matched speaker.
        line.viaSpeechPacket = (i == 0 || i == 1 || i == 2 || i == 5);
        l.lines.push_back(line);
    }
    return l;
}

// ===========================================================================
// VIBE_Office_BuildSuccessorDialogA (gilde.exe 0x4a003c).
// ===========================================================================
SuccessorDialogA Office_BuildSuccessorDialogA(int winW) {
    SuccessorDialogA d{};
    d.scene      = kSceneSitzungen;
    d.promptText = kSuccessorTextA;         // RenderRichString(3848, *successor, *holder)
    d.childObjBase = -1;                    // the 3 reserved GetChildObjectId slots
    d.sliderX    = OfficeSliderX(winW);     // ((winW-300)/2)-8
    return d;
}

// ===========================================================================
// VIBE_Office_BuildSuccessorDialogB (gilde.exe 0x4a01a4).
// ===========================================================================
SuccessorDialogB Office_BuildSuccessorDialogB(const std::vector<i32>& candidateObjs,
                                              int collectedCount, int winW) {
    SuccessorDialogB d{};
    d.scene = kSceneSitzungen;

    // v6 counts resolvable candidates (Person_FindRecordById ok). If none -> -1.
    std::vector<i32> valid;
    for (i32 obj : candidateObjs) {
        if (obj != -1)
            valid.push_back(obj);
    }
    if (valid.empty()) {
        d.valid = false;
        return d;                            // return -1 (no form)
    }

    // Prompt: 3871 when >= 2 collected candidates, else 3870 (mirrors v29 >= 2).
    d.promptText = (collectedCount >= 2) ? kSuccessorPromptMulti
                                         : kSuccessorPromptSingle;
    d.sliderX = OfficeSliderX(winW);

    // Up to 4 candidate buttons; each gets a GetChildObjectId widget id. Slots
    // beyond v6 are padded with object id -1 (the v26[++v17+3] = -1 loop).
    d.buttons.resize(kSuccessorMaxButtons);
    int n = static_cast<int>(valid.size());
    if (n > kSuccessorMaxButtons)
        n = kSuccessorMaxButtons;
    for (int i = 0; i < n; ++i) {
        SuccessorButton b{};
        b.candidateObj = valid[i];
        b.textId       = valid[i];           // **candidate (name id; modeled as obj)
        b.objectId     = kButtonObjBase + i; // GetChildObjectId result
        d.buttons[i] = b;
    }
    for (int i = n; i < kSuccessorMaxButtons; ++i)
        d.buttons[i] = SuccessorButton{};    // candidateObj/objectId default -1
    return d;
}

// gilde.exe 0x4a01a4 (click loop) — match dword_62D22C (clicked obj) to a button.
i32 Office_DispatchSuccessorDialogB(const SuccessorDialogB& d, int clickedObj) {
    if (!d.valid)
        return -1;
    for (const auto& b : d.buttons) {
        if (b.objectId != -1 && b.objectId == clickedObj)
            return b.candidateObj;           // v22 = *(candidate+4)
    }
    return -1;
}

// ===========================================================================
// VIBE_Office_BuildTortureChoiceForm (gilde.exe 0x4a3dc8).
// ===========================================================================
TortureChoiceForm Office_BuildTortureChoiceForm(int crimeCase, int winW,
                                                int wealthTier,
                                                const int* shuffledOrder,
                                                int wealthTierIdx,
                                                const bool* tierDisabled) {
    TortureChoiceForm f{};
    f.crimeCase = crimeCase;
    f.sliderX   = OfficeSliderX(winW);

    switch (crimeCase) {
    case 0:  // prozess, yes/no, default 0
        f.scene = kSceneProzess;
        f.headerText = kTortureCasePrompt[0]; // 4321
        f.isYesNo = true;
        f.defaultResult = 0;
        f.built = true;
        break;

    case 1:  // prozess, yes/no, default 0
        f.scene = kSceneProzess;
        f.headerText = kTortureCasePrompt[1]; // 4336
        f.isYesNo = true;
        f.defaultResult = 0;
        f.built = true;
        break;

    case 2: { // folterwahl: header 4352 + 7 instrument buttons (text 4353), cost
              // = wealthTier * costByte; RandInt(3) preselects one of first 3.
        f.scene = kSceneFolterwahl;
        f.headerText = kTortureCase2Header;   // 4352
        f.isYesNo = false;
        f.built = true;
        // VIBE_Util_InitAndShuffleDwordArray(7, v74); then 7 buttons in shuffled
        // order. The button payload IS the instrument index (a1+148 = v74[idx]).
        for (int k = 0; k < kTortureCostCount; ++k) {
            int instrument = shuffledOrder ? shuffledOrder[k] : k;
            TortureButton b{};
            b.objectId = kButtonObjBase + k;  // GetChildObjectId per row
            b.textId   = kTortureCase2Button; // 4353
            // cost = wealthTier * dword_49D7C4[instrument]  (the byte cost table).
            b.cost     = wealthTier * static_cast<int>(kTortureCostTable[instrument]);
            b.payload  = instrument;          // a1+148 = v74[idx]
            b.enabled  = true;
            f.buttons.push_back(b);
        }
        // RandInt(3) default: a1+148 = v74[rand%3]; modeled as first shuffled.
        f.defaultResult = f.buttons.empty() ? 0 : f.buttons[0].payload;
        break;
    }

    case 3:  // folterwahl, yes/no; 1210 also queues an op-35 command (deferred).
        f.scene = kSceneFolterwahl;
        f.headerText = kTortureCasePrompt[3]; // 4360
        f.isYesNo = true;
        f.defaultResult = 0;
        f.built = true;
        break;

    case 4:  // prozess, texts 4430/4431/4432; yes/no, default 1.
        f.scene = kSceneProzess;
        f.headerText = kTortureCasePrompt[4]; // 4430
        f.isYesNo = true;
        f.defaultResult = 1;
        f.built = true;
        break;

    case 5: { // prozess: prompt 4439 + 3 tier buttons (4440); tier base from
              // ClassifyWealthTier; one button disabled per ValidatePunishmentType.
        f.scene = kSceneProzess;
        f.headerText = kTortureCase5Prompt;   // 4439
        f.isYesNo = false;
        f.built = true;
        for (int i = 0; i < kTortureCase5TierCount; ++i) {
            TortureButton b{};
            b.objectId = kButtonObjBase + i;
            b.textId   = kTortureCase5Button; // 4440
            b.payload  = wealthTierIdx + i;   // a1+148 = tier + i (v57 + i)
            // VIBE_He_ValidatePunishmentType(..) -> SetEnabled(0) disables it.
            b.enabled  = !(tierDisabled && tierDisabled[i]);
            f.buttons.push_back(b);
        }
        f.defaultResult = wealthTierIdx;      // a1+148 default = v57
        break;
    }

    case 6:  // prozess, yes/no, default 0.
        f.scene = kSceneProzess;
        f.headerText = kTortureCasePrompt[6]; // 4407
        f.isYesNo = true;
        f.defaultResult = 0;
        f.built = true;
        break;

    default: // the original returns the unmatched id unchanged (no form built)
        f.built = false;
        break;
    }
    return f;
}

// gilde.exe 0x4a3dc8 (click loops) — resolve a click to the a1+148 value.
int Office_DispatchTortureChoice(const TortureChoiceForm& f, int clickedObj) {
    if (!f.built)
        return f.defaultResult;

    if (f.isYesNo) {
        // dword_75BF38 == 1155 / 1210 paths. The yes(1210)/no(1155) value depends
        // on the case: cases 0,1,3 -> 1155->0,1210->1 ; case 4 -> 1155->1 ; case 6 ->
        // 1155->1. We use the recovered per-case mapping:
        if (clickedObj == kClickOk) {          // 1210
            if (f.crimeCase == 4)              // case 4: 1210 -> tier value (v54); model as default
                return f.defaultResult;
            return (f.crimeCase == 6) ? 0 : 1; // cases 0,1,3 -> 1 ; case 6 -> 0
        }
        if (clickedObj == kClickCancel) {      // 1155
            if (f.crimeCase == 4 || f.crimeCase == 6)
                return 1;                      // case 4/6: 1155 -> 1
            return 0;                          // cases 0,1,3 -> 0
        }
        return f.defaultResult;
    }

    // Button forms (cases 2, 5): match the clicked widget id to a button.
    for (const auto& b : f.buttons) {
        if (b.objectId == clickedObj)
            return b.payload;
    }
    return f.defaultResult;
}

} // namespace guild::world
