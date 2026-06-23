// Unit tests for the office/council/trial GUI form-build leaves + the cutscene
// presentation builders:
//   office_forms   : election speech lines, vote-panel columns, vote markers,
//                    successor candidate buttons, torture-choice buttons + wiring.
//   cutscene_build : scene path template, speech-packet field layout (byte-for-byte).
#include "world/office_forms.h"
#include "gui/cutscene_build.h"
#include "tests/framework/test.h"

#include <cstring>

using namespace guild::world;
using namespace guild::gui;

// ===========================================================================
// VotePanel — 3-column tally panel (BuildVotePanel).
// ===========================================================================
TEST(OfficeForms, VotePanelInitBuildsThreeHeaders) {
    VotePanel p;
    p.Init();
    CHECK_EQ(p.objects.size(), 3u);
    for (int c = 0; c < 3; ++c) {
        CHECK(p.objects[c].kind == VotePanelObject::kHeader);
        CHECK_EQ(p.objects[c].textIdOrIcon, kVotePanelHeaderText[c]);
        CHECK_EQ(p.objects[c].color, kVotePanelHeaderColor); // 67
        // gilde.exe 0x49dc72: AddTextLabel(0, ...) — all 3 headers at x=0 (column x is
        // the MARKER x). Prior golden wrongly expected the column x for headers.
        CHECK_EQ(p.objects[c].x, 0);
    }
    CHECK_EQ(p.count[0], 0);
    CHECK_EQ(p.count[1], 0);
    CHECK_EQ(p.count[2], 0);
}

TEST(OfficeForms, VotePanelMarkColumnGeometry) {
    VotePanel p;
    p.Init();
    // 2 yes, 1 no, 3 abstain.
    CHECK(p.Mark(0));
    CHECK(p.Mark(0));
    CHECK(p.Mark(1));
    CHECK(p.Mark(2));
    CHECK(p.Mark(2));
    CHECK(p.Mark(2));
    CHECK_EQ(p.count[0], 2);
    CHECK_EQ(p.count[1], 1);
    CHECK_EQ(p.count[2], 3);
    // Out-of-range code is a no-op.
    CHECK(!p.Mark(3));
    CHECK(!p.Mark(-1));

    // Markers: 3 headers + 6 markers.
    CHECK_EQ(p.objects.size(), 9u);
    // First yes marker: x=32, y = 10*0 + 80 = 80.
    const VotePanelObject& m0 = p.objects[3];
    CHECK(m0.kind == VotePanelObject::kMarker);
    CHECK_EQ(m0.x, 32);
    CHECK_EQ(m0.y, 80);
    CHECK_EQ(m0.textIdOrIcon, kVotePanelMarkerIcon); // 1162
    // Second yes marker: y = 10*1 + 80 = 90.
    CHECK_EQ(p.objects[4].y, 90);
    // Third abstain marker (column 2): x=47, y = 10*2 + 80 = 100.
    const VotePanelObject& last = p.objects[8];
    CHECK_EQ(last.column, 2);
    CHECK_EQ(last.x, 47);
    CHECK_EQ(last.y, 100);
}

// ===========================================================================
// Office_AddVoteMarker — election ballot markers step left by 10.
// ===========================================================================
TEST(OfficeForms, VoteMarkerStepsLeft) {
    int count = 0;
    VotePanelObject m0 = Office_AddVoteMarker(count);
    CHECK_EQ(m0.x, 68);    // 68 - 10*0
    CHECK_EQ(m0.y, 140);
    CHECK_EQ(m0.textIdOrIcon, 1162);
    CHECK_EQ(count, 1);
    VotePanelObject m1 = Office_AddVoteMarker(count);
    CHECK_EQ(m1.x, 58);    // 68 - 10*1
    VotePanelObject m2 = Office_AddVoteMarker(count);
    CHECK_EQ(m2.x, 48);    // 68 - 10*2
    CHECK_EQ(count, 3);
}

// ===========================================================================
// Office_BuildElectionForm — per-seat speech lines.
// ===========================================================================
static ElectionSeat MakeSeat(int handle, int role) {
    ElectionSeat s{};
    s.personHandle = handle;
    s.textId = handle;
    s.role = role;
    s.present = true;
    s.eligible = true;
    return s;
}

TEST(OfficeForms, ElectionFormSuccessSpeechLines) {
    // Seats 0,1 speak (role 6/7); seat 2 vacant (role 15 -> skip); seats 3,4,5 mixed.
    std::vector<ElectionSeat> seats = {
        MakeSeat(100, 6),   // seat0 -> 3634, packet
        MakeSeat(101, 7),   // seat1 -> 3635, packet
        MakeSeat(102, 15),  // seat2 vacant -> skipped
        MakeSeat(103, 6),   // seat3 -> 3637, BuildSpeechPacket (success path)
        MakeSeat(104, 3),   // seat4 role 3 -> skipped (not 6/7)
        MakeSeat(105, 7),   // seat5 -> 3639, packet
    };
    ElectionFormLayout l = Office_BuildElectionForm(seats, /*speakerSeat*/0, /*fail*/0);
    CHECK_EQ(l.failureCode, 0);
    // Speaking seats: 0,1,3,5 -> 4 lines.
    CHECK_EQ(l.lines.size(), 4u);
    CHECK_EQ(l.lines[0].seat, 0);
    CHECK_EQ(l.lines[0].textId, 3634);
    CHECK(l.lines[0].viaSpeechPacket);
    CHECK_EQ(l.lines[1].textId, 3635);
    CHECK_EQ(l.lines[2].seat, 3);
    CHECK_EQ(l.lines[2].textId, 3637);
    // gilde.exe 0x4a0ce1: success path sends seat 3 via BuildSpeechPacket too (the He
    // message broadcast is the FAILURE path). Prior golden wrongly expected He message.
    CHECK(l.lines[2].viaSpeechPacket);
    CHECK_EQ(l.lines[3].seat, 5);
    CHECK_EQ(l.lines[3].textId, 3639);
    CHECK(l.lines[3].viaSpeechPacket);
}

TEST(OfficeForms, ElectionFormFailureStatusText) {
    std::vector<ElectionSeat> seats;
    for (int code = 1; code <= 7; ++code) {
        ElectionFormLayout l = Office_BuildElectionForm(seats, -1, code);
        CHECK_EQ(l.failureCode, code);
        CHECK_EQ(l.statusText, kElectionFailText[code]);
        CHECK(l.lines.empty());
    }
    // Code 6 -> 3649, code 7 -> 3648, code 1 -> 3642.
    CHECK_EQ(Office_BuildElectionForm(seats, -1, 6).statusText, 3649);
    CHECK_EQ(Office_BuildElectionForm(seats, -1, 7).statusText, 3648);
    CHECK_EQ(Office_BuildElectionForm(seats, -1, 1).statusText, 3642);
}

// ===========================================================================
// Successor dialogs A / B.
// ===========================================================================
TEST(OfficeForms, SuccessorDialogACentersSlider) {
    SuccessorDialogA d = Office_BuildSuccessorDialogA(/*winW*/640);
    CHECK_EQ(d.promptText, 3848);
    CHECK(std::strcmp(d.scene, kSceneSitzungen) == 0);
    // x = ((640-300)/2)-8 = 170-8 = 162.
    CHECK_EQ(d.sliderX, 162);
}

TEST(OfficeForms, SuccessorDialogBButtonsAndPrompt) {
    // 3 candidates -> 3 buttons + 1 padded slot, prompt 3871 (>=2).
    std::vector<i32> cands = {500, 501, 502};
    SuccessorDialogB d = Office_BuildSuccessorDialogB(cands, /*collected*/3, /*winW*/640);
    CHECK(d.valid);
    CHECK_EQ(d.promptText, kSuccessorPromptMulti); // 3871
    CHECK_EQ(d.buttons.size(), (size_t)kSuccessorMaxButtons); // padded to 4
    CHECK_EQ(d.buttons[0].candidateObj, 500);
    CHECK_EQ(d.buttons[2].candidateObj, 502);
    CHECK_EQ(d.buttons[3].candidateObj, -1);  // padded
    CHECK_EQ(d.buttons[3].objectId, -1);

    // Click candidate 1 -> 501.
    CHECK_EQ(Office_DispatchSuccessorDialogB(d, d.buttons[1].objectId), 501);
    // Click padded/unknown -> -1.
    CHECK_EQ(Office_DispatchSuccessorDialogB(d, 9999), -1);
}

TEST(OfficeForms, SuccessorDialogBSingleAndEmpty) {
    SuccessorDialogB one = Office_BuildSuccessorDialogB({700}, /*collected*/1, 640);
    CHECK(one.valid);
    CHECK_EQ(one.promptText, kSuccessorPromptSingle); // 3870 (<2)
    // Empty candidate set -> invalid (return -1, no form).
    SuccessorDialogB none = Office_BuildSuccessorDialogB({-1, -1}, 0, 640);
    CHECK(!none.valid);
    CHECK(none.buttons.empty());
    CHECK_EQ(Office_DispatchSuccessorDialogB(none, 0), -1);
}

// ===========================================================================
// Torture-choice form (the 7 crime cases).
// ===========================================================================
TEST(OfficeForms, TortureYesNoCases) {
    // Case 0: prozess, 4321, default 0; 1210->1, 1155->0.
    TortureChoiceForm c0 = Office_BuildTortureChoiceForm(0, 640, 0);
    CHECK(c0.built);
    CHECK(c0.isYesNo);
    CHECK_EQ(c0.headerText, 4321);
    CHECK(std::strcmp(c0.scene, kSceneProzess) == 0);
    CHECK_EQ(Office_DispatchTortureChoice(c0, kClickOk), 1);
    CHECK_EQ(Office_DispatchTortureChoice(c0, kClickCancel), 0);

    // Case 3: folterwahl, 4360.
    TortureChoiceForm c3 = Office_BuildTortureChoiceForm(3, 640, 0);
    CHECK_EQ(c3.headerText, 4360);
    CHECK(std::strcmp(c3.scene, kSceneFolterwahl) == 0);

    // Case 4: 4430, default 1; 1155->1.
    TortureChoiceForm c4 = Office_BuildTortureChoiceForm(4, 640, 0);
    CHECK_EQ(c4.headerText, 4430);
    CHECK_EQ(c4.defaultResult, 1);
    CHECK_EQ(Office_DispatchTortureChoice(c4, kClickCancel), 1);

    // Case 6: 4407, default 0; 1210->0.
    TortureChoiceForm c6 = Office_BuildTortureChoiceForm(6, 640, 0);
    CHECK_EQ(c6.headerText, 4407);
    CHECK_EQ(Office_DispatchTortureChoice(c6, kClickOk), 0);
    CHECK_EQ(Office_DispatchTortureChoice(c6, kClickCancel), 1);
}

TEST(OfficeForms, TortureCase2InstrumentButtonsAndCost) {
    // wealthTier = (wealthA+wealthB)/2000 = 5; identity shuffle order.
    int order[7] = {0, 1, 2, 3, 4, 5, 6};
    TortureChoiceForm f = Office_BuildTortureChoiceForm(2, 640, /*wealthTier*/5, order);
    CHECK(f.built);
    CHECK(!f.isYesNo);
    CHECK_EQ(f.headerText, 4352);
    // gilde.exe 0x4a410a: the build loop runs only 3 times -> 3 buttons (the first 3
    // shuffled instruments). Prior golden wrongly expected all 7.
    CHECK_EQ(f.buttons.size(), 3u);
    // Costs = tier * cost-byte table {8,10,12} for the first 3 instruments.
    const int expect[3] = {40, 50, 60};
    for (int k = 0; k < 3; ++k) {
        CHECK_EQ(f.buttons[k].textId, 4353);
        CHECK_EQ(f.buttons[k].cost, expect[k]);
        CHECK_EQ(f.buttons[k].payload, k); // instrument index = shuffle value
    }
    // Clicking instrument 2's button selects payload 2.
    CHECK_EQ(Office_DispatchTortureChoice(f, f.buttons[2].objectId), 2);
}

TEST(OfficeForms, TortureCase5TierButtonsDisable) {
    // ClassifyWealthTier = 2 (tier base); tier 1 disabled by ValidatePunishmentType.
    bool disabled[3] = {false, true, false};
    TortureChoiceForm f = Office_BuildTortureChoiceForm(5, 640, 0, nullptr,
                                                        /*wealthTierIdx*/2, disabled);
    CHECK(f.built);
    CHECK_EQ(f.headerText, 4439);
    CHECK_EQ(f.buttons.size(), 3u);
    CHECK_EQ(f.buttons[0].payload, 2); // tier + 0
    CHECK_EQ(f.buttons[1].payload, 3); // tier + 1
    CHECK_EQ(f.buttons[2].payload, 4); // tier + 2
    CHECK(f.buttons[0].enabled);
    CHECK(!f.buttons[1].enabled);      // disabled
    CHECK(f.buttons[2].enabled);
    CHECK_EQ(Office_DispatchTortureChoice(f, f.buttons[2].objectId), 4);
    // gilde.exe 0x4a46da: default = v58+1 (middle tier), since v77 (button[1] id) != -1.
    // Prior golden wrongly expected the tier base (+0).
    CHECK_EQ(Office_DispatchTortureChoice(f, 99999), 3);
}

TEST(OfficeForms, TortureUnknownCaseNotBuilt) {
    TortureChoiceForm f = Office_BuildTortureChoiceForm(99, 640, 0);
    CHECK(!f.built);
    CHECK(f.buttons.empty());
}

// ===========================================================================
// cutscene_build — scene path + speech packet layout.
// ===========================================================================
TEST(CutsceneBuild, SceneResourcePath) {
    CHECK(Cutscene_SceneResourcePath("Gericht.ed3") == std::string("scenes/*Gericht.ed3"));
    CHECK(Cutscene_SceneResourcePath("Sitzung_Wohnsitz.ed3")
          == std::string("scenes/*Sitzung_Wohnsitz.ed3"));
}

TEST(CutsceneBuild, SpeechPacketNoVoiceLayout) {
    SpeechSpeaker who{};
    who.speakerHandle = 0x1234;
    who.textId = 77;
    who.roomId = -1;       // no room -> flags 12
    who.roomVoiceBit = false;
    SpeechPacket pkt = Cutscene_BuildSpeechPacket(who, "Hallo");
    CHECK_EQ(pkt.kind, kSpeechHeaderKind);  // 17
    CHECK_EQ(pkt.speaker, 0x1234);
    CHECK_EQ(pkt.replyTo, -1);
    CHECK_EQ(pkt.msgId, 1418);
    CHECK_EQ(pkt.priority, (guild::u8)9);
    CHECK_EQ(pkt.flags, (guild::u8)12);     // no room voice
    CHECK_EQ(pkt.roomId, -1);
    CHECK_EQ(pkt.objId, -1);
    CHECK_EQ(pkt.textId, 77);
    // totalLen = strlen("Hallo")+1 = 6; voiceLen = 5.
    CHECK_EQ(pkt.totalLen, 6);
    CHECK_EQ(pkt.voiceLen, 5);
    CHECK(pkt.payload == std::string("Hallo"));
}

TEST(CutsceneBuild, SpeechPacketRoomVoiceFlags14) {
    SpeechSpeaker who{};
    who.roomId = 42;        // has room
    who.roomVoiceBit = true; // and uses room voice -> flags 14
    who.resolvedObjId = 9;
    SpeechPacket pkt = Cutscene_BuildSpeechPacket(who, "X");
    CHECK_EQ(pkt.flags, (guild::u8)14);
    CHECK_EQ(pkt.roomId, 42);
    CHECK_EQ(pkt.objId, 9);  // resolved storable object id (v27)
}

TEST(CutsceneBuild, SpeechPacketWithVoiceClip) {
    SpeechSpeaker who{};
    who.roomId = -1;
    SpeechPacket pkt = Cutscene_BuildSpeechPacket(who, "Hi", "clip01");
    // flags base 12 | 0x10 attach = 28.
    CHECK_EQ(pkt.flags, (guild::u8)(12 | 0x10));
    // v16 = strlen("Hi")+1 = 3; v17 = strlen("clip01")+1 = 7.
    // totalLen = v17 + v16 = 10; voiceLen = v17 - 1 = 6.
    CHECK_EQ(pkt.totalLen, 10);
    CHECK_EQ(pkt.voiceLen, 6);
    // payload = "Hi\0clip01".
    CHECK_EQ(pkt.payload.size(), (size_t)9);
    CHECK(std::memcmp(pkt.payload.data(), "Hi\0clip01", 9) == 0);
}
