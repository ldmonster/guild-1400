#pragma once
// guild::world — the OFFICE / COUNCIL / TRIAL GUI FORM-BUILD LEAVES.
//
// These are the form/widget-construction leaves the council/trial/election cutscene
// shells (council_session / trial_session / election_form) defer. Each builds a
// window's child-object tree (candidate buttons, vote markers, speech packets,
// torture-choice buttons), wires the buttons to a vote/choice resolution, and runs
// a modal frame loop. Translated 1:1 here is the WIDGET TREE + CONTENT + BUTTON
// WIRING; the glyph blit, modal frame loop and voice playback are forward-declared
// /stubbed (deferred — see report).
//
//   VIBE_Office_BuildElectionForm     (gilde.exe 0x4a0610, ~2.2 KB) — per-candidate
//       speech packets for the council election announcement.
//   VIBE_Office_BuildVotePanel        (gilde.exe 0x49dc18) — the 3-column yes/no/
//       abstain vote tally panel (headers + per-vote markers).
//   VIBE_Office_AddVoteMarker         (gilde.exe 0x49dbe8) — one ballot marker
//       (election form, x steps left by 10).
//   VIBE_Office_BuildSuccessorDialogA (gilde.exe 0x4a003c) — single-line successor
//       prompt + a 1500-tick timed slider.
//   VIBE_Office_BuildSuccessorDialogB (gilde.exe 0x4a01a4) — successor pick: up to
//       4 candidate buttons + slider; click resolves to a candidate object.
//   VIBE_Office_BuildTortureChoiceForm(gilde.exe 0x4a3dc8, ~2.6 KB) — the per-crime-
//       category torture/verdict choice form (one of 7 cases), with yes/no or
//       multi-instrument buttons + slider.
//
// Mutations (the QueueRequest* commands) route through the existing council command
// hooks / a settable mock; this module only builds the widget tree + resolves clicks.

#include "guild/common/types.h"
#include "gui/cutscene_build.h"   // SpeechSpeaker / SpeechPacket (reused leaf)

#include <vector>

namespace guild::world {

using guild::i32;
using guild::u8;

// ===========================================================================
// Shared scene paths + click ids (recovered via get_string / the dword_75BF38 reads).
// ===========================================================================
inline constexpr const char* kSceneSitzungen = "cutscenes\\sitzungen"; // @0x61c5e4
inline constexpr const char* kSceneProzess   = "cutscenes\\prozess";   // @0x61cd04
inline constexpr const char* kSceneFolterwahl= "cutscenes\\folterwahl";// @0x61cd18

// dword_75BF38 last-clicked widget id: 1210 == OK/yes, 1155 == Cancel/no.
inline constexpr int kClickOk     = 1210;
inline constexpr int kClickCancel = 1155;

// The timed modal slider that every form spawns: AddSliderToWindow(cx, 8, 0, 300,
// 100, 100, 66, win) then UpdateProgressBar(slider, 1500, 1). x is centered:
//   x = ((window.w - 300) / 2) - 8 ;  y=8; min=0; max=300; val=100; step=100; id=66.
inline constexpr int kSliderY      = 8;
inline constexpr int kSliderMin    = 0;
inline constexpr int kSliderMax    = 300;
inline constexpr int kSliderVal    = 100;
inline constexpr int kSliderStep   = 100;
inline constexpr int kSliderId     = 66;
inline constexpr int kSliderWidth  = 300;
inline constexpr int kSliderTimeout= 1500; // UpdateProgressBar ticks

// gilde.exe — the centered timer-slider x for a window of pixel width `winW`.
inline int OfficeSliderX(int winW) { return ((winW - kSliderWidth) / 2) - 8; }

// ===========================================================================
// VIBE_Office_AddVoteMarker (gilde.exe 0x49dbe8) — election-form ballot marker.
//   VIBE_Object_AddToWindow(win, 68 - 10*markerCount, 140, 1162); ++markerCount;
// The marker icon is 1162; markers start at x=68 and step LEFT by 10; y is fixed 140.
// ===========================================================================
inline constexpr int kVoteMarkerIcon  = 1162;
inline constexpr int kVoteMarkerBaseX = 68;
inline constexpr int kVoteMarkerStepX = 10;
inline constexpr int kVoteMarkerY     = 140;
inline int OfficeVoteMarkerX(int markerCount) {
    return kVoteMarkerBaseX - kVoteMarkerStepX * markerCount;
}

// ===========================================================================
// VIBE_Office_BuildVotePanel (gilde.exe 0x49dc18) — 3-column tally panel.
//   init: RemoveChildren; 3 text labels (3861/3862/3863), each SetColor(67); reset
//         the 3 column counters dword_11B4E48/4C/50.
//   mark code 0/1/2: AddToWindow(win, x, 10*count+80, 1162) at x={32,62,47}; ++count.
// ===========================================================================
inline constexpr int kVotePanelHeaderText[3] = {3861, 3862, 3863}; // yes/no/abstain
inline constexpr int kVotePanelColumnX[3]     = {32, 62, 47};
inline constexpr int kVotePanelHeaderColor    = 67;   // SetColor arg
inline constexpr int kVotePanelMarkerIcon     = 1162;
inline int VotePanelMarkerY(int countInColumn) { return 10 * countInColumn + 80; }

// One placed object the vote panel builds (a header label or a column marker).
struct VotePanelObject {
    enum Kind { kHeader, kMarker } kind = kHeader;
    int column = 0;     // 0 yes / 1 no / 2 abstain
    int x = 0, y = 0;
    int textIdOrIcon = 0; // header text id, or marker icon (1162)
    int color = 0;        // header color (67); 0 for markers
};

// The panel state: the 3 running column counters + every object placed so far.
struct VotePanel {
    int count[3] = {0, 0, 0};   // dword_11B4E48 / 4C / 50
    std::vector<VotePanelObject> objects;

    // gilde.exe 0x49dc18 (a1 != 0) — clear the columns + render the 3 headers.
    void Init();
    // gilde.exe 0x49dc18 (a1 == 0 / a3 == 0/1/2) — place a marker in column `code`.
    // Returns false (no-op) for out-of-range codes (the original handles 0/1/2).
    bool Mark(int code);
};

// gilde.exe 0x49dbe8 — place one election-form ballot marker; bumps `markerCount`.
// Returns the placed marker object (x = 68 - 10*markerCount, y = 140, icon 1162).
VotePanelObject Office_AddVoteMarker(int& markerCount);

// ===========================================================================
// VIBE_Office_BuildElectionForm (gilde.exe 0x4a0610) — per-candidate speech packets
// for the council election announcement.
//
// The original resolves up to 6 candidate seats (a1+52,+56,+60,+64,+68,+72 via
// Person_FindRecordById), picks a "speaker" (the seat whose held-handler matches),
// and on the success path renders one speech line per eligible seat and sends it
// (BuildSpeechPacket / He_SendEntityMessage). Seat eligibility: the person exists,
// person[2] (role) != 15, person[8] set, person[433] set. A seat "speaks" when
// person[2] (role byte) is 6 or 7.
//
// Recovered speech text ids (RenderFormattedMessage, the success path):
//   3634 seat0 line ; 3635 seat1 ; 3636 seat2 ; 3637 shared body ; 3639 seat5.
// Seats 3,4 reuse the 3637 body. Failure cases (no eligible speaker) pick a single
// status text 3642..3649 by the failure code. The He message channel is 1418.
// ===========================================================================
inline constexpr int kElectionSeatCount = 6;          // a1+52 .. a1+72 (stride 4)
inline constexpr int kElectionSeatOff[kElectionSeatCount] = {52, 56, 60, 64, 68, 72};
inline constexpr int kElectionMsgChannel = 1418;      // He_SendEntityMessage id
inline constexpr int kElectionSpeakRoleA = 6;         // person[2] speaks
inline constexpr int kElectionSpeakRoleB = 7;
inline constexpr int kElectionRoleVacant = 15;        // person[2] == 15 -> ineligible

// Per-seat speech text ids on the success path (index = seat 0..5; seats 3,4 -> 3637).
inline constexpr int kElectionSeatText[kElectionSeatCount] =
    {3634, 3635, 3636, 3637, 3637, 3639};

// Failure status texts, by failure code 1..7 (code 0 = success / has speaker).
//   1->3642 2->3643 3->3644 4->3645 5->3646 6->3649(speaker) 7->3648(speaker).
inline constexpr int kElectionFailText[8] =
    {0, 3642, 3643, 3644, 3645, 3646, 3649, 3648};

// One candidate/seat the election form describes.
struct ElectionSeat {
    i32  personHandle = -1; // *(person+1)  (entity handle used by He message)
    i32  textId       = 0;  // *person      (the localized seat-name id)
    int  role         = 0;  // person[2]    (15 = vacant; 6/7 = speaks)
    bool present      = false; // person[8] set
    bool eligible     = false; // person[433] set (can be the matched speaker)
    bool heldHandlerMatch = false; // this seat holds the office handler (the speaker)
};

// One produced speech line in the election announcement.
struct ElectionSpeechLine {
    int  seat   = 0;
    int  textId = 0;       // RenderFormattedMessage id used
    i32  speaker = -1;     // *(person+1)
    bool viaSpeechPacket = false; // seats 0..2,5 -> BuildSpeechPacket; else He message
};

struct ElectionFormLayout {
    int  failureCode = 0;            // 0 = success (a speaker found); else 1..7
    int  speakerSeat = -1;           // the seat that "speaks" (held-handler match)
    int  statusText  = 0;            // failure status text id (when failureCode != 0)
    std::vector<ElectionSpeechLine> lines; // success-path speech lines
};

// gilde.exe 0x4a0610 — build the election announcement form for `seats`.
//   `speakerSeat` is the index (0..5) of the seat holding the office handler, or -1
//   if none was found (mirrors the He_FindFirstHandlerByFilter match loop result).
//   `failureCode` mirrors the original's v15 (0 = success path; 1..7 = a status
//   text only). When success, one speech line is produced per speaking seat.
ElectionFormLayout Office_BuildElectionForm(const std::vector<ElectionSeat>& seats,
                                            int speakerSeat,
                                            int failureCode);

// ===========================================================================
// VIBE_Office_BuildSuccessorDialogA (gilde.exe 0x4a003c) — single prompt + slider.
//   GameTick_Finalize("cutscenes\\sitzungen"); SelectWindow(form,0);
//   RenderRichString(3848, *successor, *holder);  3 child object ids reserved;
//   AddSliderToWindow(centered, ...); UpdateProgressBar(1500,1); modal loop.
// ===========================================================================
inline constexpr int kSuccessorTextA = 3848; // single-line successor prompt

struct SuccessorDialogA {
    const char* scene = kSceneSitzungen;
    int  promptText = kSuccessorTextA;
    int  sliderX = 0;
    int  childObjBase = -1; // first reserved child id (the 3 GetChildObjectId calls)
};

// gilde.exe 0x4a003c — build the successor-prompt dialog. `winW` = window pixel
// width (for slider centering). The modal loop / voice are deferred.
SuccessorDialogA Office_BuildSuccessorDialogA(int winW);

// ===========================================================================
// VIBE_Office_BuildSuccessorDialogB (gilde.exe 0x4a01a4) — successor PICK.
//   CollectSuccessorCandidates -> up to N; for each valid candidate render a button
//   with template "%ia[%1N3]$A" (3870 single / 3871 multi prompt); pad to 4 slots
//   with id -1; AddSliderToWindow; modal loop; click resolves to candidate object.
// ===========================================================================
inline constexpr int kSuccessorPromptSingle = 3870; // < 2 candidates
inline constexpr int kSuccessorPromptMulti  = 3871; // >= 2 candidates
inline constexpr const char* kSuccessorButtonFmt = "%ia[%1N3]$A"; // aIa1n3A @0x61c5f8
inline constexpr int kSuccessorMaxButtons = 4;      // candidate button slots

// One successor candidate button.
struct SuccessorButton {
    i32 candidateObj = -1; // the candidate's object id (the click result)
    i32 textId       = 0;  // **candidate (the name id rendered by the button fmt)
    int objectId     = -1; // GetChildObjectId result (the clickable widget id)
};

struct SuccessorDialogB {
    const char* scene = kSceneSitzungen;
    int  promptText   = kSuccessorPromptSingle;
    int  sliderX      = 0;
    bool valid        = true; // false => GetEntryByCity/GetDefinition/no candidate -> -1
    std::vector<SuccessorButton> buttons; // exactly 4 slots; unused = id -1
};

// gilde.exe 0x4a01a4 — build the successor-pick dialog from the candidate object ids.
//   `winW` = window width.  Up to 4 buttons; pads to 4 slots (id -1). The prompt is
//   3871 when >= 2 collected candidates else 3870 (mirrors v29 >= 2).
//   Returns valid=false (and no buttons) when there are zero resolvable candidates.
SuccessorDialogB Office_BuildSuccessorDialogB(const std::vector<i32>& candidateObjs,
                                              int collectedCount, int winW);

// gilde.exe 0x4a01a4 (click loop) — resolve a clicked widget id to a candidate
// object id (the v22 = *(candidate+4) result). Returns -1 when the click hits none.
i32 Office_DispatchSuccessorDialogB(const SuccessorDialogB& d, int clickedObj);

// ===========================================================================
// VIBE_Office_BuildTortureChoiceForm (gilde.exe 0x4a3dc8) — per-crime torture form.
//
// The form switches on the crime category (person.crime byte, 0..6) and builds one
// of 7 layouts. Each loads a scene (prozess / folterwahl), renders prompt text,
// builds buttons, spawns the timer slider, and runs a modal loop whose result is
// written to a1+148 (the chosen punishment / instrument index). Recovered constants:
//
//   case 0 (prozess)  text 4321; yes/no via click 1155->0 / 1210->1.   a1+148.
//   case 1 (prozess)  text 4336; yes/no.
//   case 2 (folterwahl) text 4352 header + 7 shuffled instrument buttons (text 4353
//         with per-instrument cost = wealthTier * costByte). Cost table @0x49D7C4 =
//         {8,10,12,15,18,21,24}. RandInt(3) preselects one of the first 3 shuffled.
//   case 3 (folterwahl) text 4360; yes/no; 1210 also queues op-35 command.
//   case 4 (prozess)  texts 4430/4431/4432; yes/no (default a1+148 = 1).
//   case 5 (prozess)  text 4439 + 3 tier buttons (4440), one disabled per
//         He_ValidatePunishmentType; tiers from ClassifyWealthTier.
//   case 6 (prozess)  text 4407; yes/no (default a1+148 = 0).
// ===========================================================================
inline constexpr int kTortureCostCount = 7;            // dword_49D7C4 byte count
inline constexpr u8  kTortureCostTable[kTortureCostCount] =
    {8, 10, 12, 15, 18, 21, 24};                       // @0x49D7C4
inline constexpr int kTortureCase2Header = 4352;       // folterwahl header
inline constexpr int kTortureCase2Button = 4353;       // per-instrument button text
inline constexpr int kTortureCase2Divisor= 2000;       // (wealthA+wealthB)/2000 tier
inline constexpr int kTortureCase2RandTop= 3;          // RandInt(3) preselect among the 3
// gilde.exe 0x4a410a: the case-2 button-build loop runs 3 times (ecx 0,4,8; cmp 0Ch),
// so only 3 of the 7 shuffled instruments get buttons (stored at v74[0..2]).
inline constexpr int kTortureCase2Buttons = 3;
inline constexpr int kTortureCase5Prompt = 4439;       // tier-choice prompt
inline constexpr int kTortureCase5Button = 4440;       // tier button text base
inline constexpr int kTortureCase5TierBase = 4441;     // v82 text base for tiers
inline constexpr int kTortureCase5TierCount = 3;       // 3 tier buttons

// Per-case yes/no prompt text ids (index = case 0..6; 0 = no simple prompt).
inline constexpr int kTortureCasePrompt[7] = {4321, 4336, 0, 4360, 4430, 0, 4407};

// One torture-form button.
struct TortureButton {
    int objectId = -1;  // clickable widget id (GetChildObjectId / click id)
    int textId   = 0;   // rendered text id
    int cost     = 0;   // case-2 instrument cost (tier * costByte)
    int payload  = 0;   // the a1+148 value this button selects
    bool enabled = true;// case-5 ValidatePunishmentType may disable a tier
};

struct TortureChoiceForm {
    int  crimeCase = 0;          // the switch selector (0..6)
    const char* scene = nullptr; // prozess / folterwahl
    int  headerText = 0;         // primary prompt text id
    int  sliderX = 0;
    int  defaultResult = 0;      // initial a1+148 value
    bool isYesNo = false;        // cases 0,1,3,4,6 use 1155/1210
    std::vector<TortureButton> buttons; // cases 2,5 (multi-button)
    bool built = false;          // false => the case's id guard failed (early return)
};

// gilde.exe 0x4a3dc8 — build the torture-choice form for `crimeCase` (0..6).
//   `winW`   = window pixel width (slider centering).
//   `wealthTier` = (wealthA+wealthB)/2000 for case 2 (instrument costing).
//   `shuffledOrder` = the 7-element shuffled instrument order (case 2; pass the
//                     identity 0..6 for a deterministic test).
//   `wealthTierIdx` = ClassifyWealthTier result (case 5 tier base index).
//   `tierDisabled` = per-tier ValidatePunishmentType flag (case 5; true => disabled).
// Cases 0/1/3/4/6 produce a yes/no form (no buttons vector). Cases 2/5 fill buttons.
TortureChoiceForm Office_BuildTortureChoiceForm(int crimeCase, int winW,
                                                int wealthTier,
                                                const int* shuffledOrder = nullptr,
                                                int wealthTierIdx = 0,
                                                const bool* tierDisabled = nullptr);

// gilde.exe 0x4a3dc8 (click loops) — resolve a clicked widget id to the a1+148 value
// the original would store. For yes/no forms `clickedObj` is the click id (1155/1210);
// for button forms it is the button's object id. Returns the chosen payload, or the
// form's defaultResult when the click hits nothing.
int Office_DispatchTortureChoice(const TortureChoiceForm& f, int clickedObj);

} // namespace guild::world
