// End-to-end: build a council ELECTION flow — the vote-panel tally, the per-voter
// ballot markers, the candidate (successor-pick) buttons — for a synthetic candidate
// set, then verify the widget tree + marker placement and that a vote click resolves
// to the expected candidate/command. Also exercises the torture-choice form + the
// speech-packet build leaf end-to-end with a recording command hook.
#include "world/office_forms.h"
#include "gui/cutscene_build.h"
#include "tests/framework/test.h"

#include <cstring>
#include <vector>

using namespace guild::world;
using namespace guild::gui;

namespace {
// Recording sink for the speech-packet queue hook (the deferred command leaf).
struct SpeechSink {
    std::vector<SpeechPacket> packets;
};
SpeechSink g_sink;
void RecordSpeech(const SpeechPacket& pkt, void* ctx) {
    static_cast<SpeechSink*>(ctx)->packets.push_back(pkt);
}
} // namespace

// A whole council election: 3 voters cast ballots, the vote panel tallies columns,
// markers are placed per cast ballot, and the 3 candidates become clickable buttons.
TEST(OfficeFormsE2E, ElectionVotePanelAndCandidateButtons) {
    // --- Vote panel: 3 columns (yes/no/abstain) ---------------------------
    VotePanel panel;
    panel.Init();
    CHECK_EQ(panel.objects.size(), 3u); // headers

    // 5 councillors vote: yes, yes, no, abstain, yes.
    int votes[5] = {0, 0, 1, 2, 0};
    for (int v : votes)
        CHECK(panel.Mark(v));
    CHECK_EQ(panel.count[0], 3); // yes
    CHECK_EQ(panel.count[1], 1); // no
    CHECK_EQ(panel.count[2], 1); // abstain

    // Header(3) + markers(5) = 8 objects. Verify the 3rd yes marker geometry.
    CHECK_EQ(panel.objects.size(), 8u);
    // The yes-column markers are placed at x=32, y=80/90/100.
    std::vector<int> yesYs;
    for (const auto& o : panel.objects)
        if (o.kind == VotePanelObject::kMarker && o.column == 0)
            yesYs.push_back(o.y);
    CHECK_EQ(yesYs.size(), 3u);
    CHECK_EQ(yesYs[0], 80);
    CHECK_EQ(yesYs[1], 90);
    CHECK_EQ(yesYs[2], 100);

    // --- Per-ballot election markers on the election form (x steps left) ---
    int markerCount = 0;
    std::vector<VotePanelObject> ballotMarkers;
    for (int i = 0; i < 4; ++i)
        ballotMarkers.push_back(Office_AddVoteMarker(markerCount));
    CHECK_EQ(markerCount, 4);
    CHECK_EQ(ballotMarkers[0].x, 68);
    CHECK_EQ(ballotMarkers[3].x, 38); // 68 - 30
    for (const auto& m : ballotMarkers) {
        CHECK_EQ(m.y, 140);
        CHECK_EQ(m.textIdOrIcon, 1162);
    }

    // --- Candidate buttons (successor pick) -------------------------------
    std::vector<i32> candidates = {811, 822, 833};
    SuccessorDialogB pick = Office_BuildSuccessorDialogB(candidates, /*collected*/3, 800);
    CHECK(pick.valid);
    CHECK_EQ(pick.promptText, 3871);             // multi prompt
    CHECK_EQ(pick.buttons.size(), 4u);           // padded to 4
    CHECK_EQ(pick.sliderX, ((800 - 300) / 2) - 8); // 242

    // A "vote click" on the 2nd candidate button resolves to candidate 822.
    int clicked = pick.buttons[1].objectId;
    i32 winner = Office_DispatchSuccessorDialogB(pick, clicked);
    CHECK_EQ(winner, 822);
    // Click outside any button -> no winner.
    CHECK_EQ(Office_DispatchSuccessorDialogB(pick, -42), -1);
}

// The election announcement form produces a speech packet per speaking seat; route
// the packets through the recording command hook and verify their wired fields.
TEST(OfficeFormsE2E, ElectionAnnouncementSpeechPackets) {
    g_sink.packets.clear();
    Cutscene_SetSpeechQueueHook(&RecordSpeech, &g_sink);

    // Two speaking seats (roles 6 and 7).
    std::vector<ElectionSeat> seats;
    auto mk = [](int h, int role) {
        ElectionSeat s{}; s.personHandle = h; s.textId = h; s.role = role;
        s.present = true; s.eligible = true; return s;
    };
    seats.push_back(mk(900, 6));  // seat0 -> 3634
    seats.push_back(mk(901, 7));  // seat1 -> 3635

    ElectionFormLayout l = Office_BuildElectionForm(seats, 0, 0);
    CHECK_EQ(l.lines.size(), 2u);

    // Emit a speech packet for each speech line (the form's BuildSpeechPacket leaf).
    for (const auto& line : l.lines) {
        if (!line.viaSpeechPacket)
            continue;
        SpeechSpeaker who{};
        who.speakerHandle = line.speaker;
        who.textId = line.textId;
        who.roomId = -1;
        Cutscene_BuildSpeechPacket(who, "Rede");
    }
    CHECK_EQ(g_sink.packets.size(), 2u);
    CHECK_EQ(g_sink.packets[0].speaker, 900);
    CHECK_EQ(g_sink.packets[0].kind, (guild::u8)17);
    CHECK_EQ(g_sink.packets[0].msgId, 1418);
    CHECK_EQ(g_sink.packets[0].flags, (guild::u8)12);
    CHECK_EQ(g_sink.packets[1].speaker, 901);

    Cutscene_SetSpeechQueueHook(nullptr, nullptr);
}

// A torture-choice form (case 2: instrument selection) end-to-end: build the 7
// instrument buttons with their wealth-scaled costs, then a click resolves to the
// chosen instrument index (the a1+148 command value).
TEST(OfficeFormsE2E, TortureInstrumentChoiceFlow) {
    // wealthA=4000, wealthB=6000 -> tier = (4000+6000)/2000 = 5. Shuffle = identity.
    int order[7] = {6, 5, 4, 3, 2, 1, 0}; // reversed shuffle
    TortureChoiceForm f = Office_BuildTortureChoiceForm(2, 1024, /*tier*/5, order);
    CHECK(f.built);
    CHECK_EQ(f.buttons.size(), 7u);
    CHECK(std::strcmp(f.scene, kSceneFolterwahl) == 0);

    // Button k uses instrument order[k]; cost = 5 * costByte[order[k]].
    const int costByte[7] = {8, 10, 12, 15, 18, 21, 24};
    for (int k = 0; k < 7; ++k) {
        int inst = order[k];
        CHECK_EQ(f.buttons[k].payload, inst);
        CHECK_EQ(f.buttons[k].cost, 5 * costByte[inst]);
    }
    // Clicking the 3rd button selects instrument order[2] = 4.
    int sel = Office_DispatchTortureChoice(f, f.buttons[2].objectId);
    CHECK_EQ(sel, 4);

    // The timed slider is centered for the 1024-px window.
    CHECK_EQ(f.sliderX, ((1024 - 300) / 2) - 8); // 354
}

// The scene-loader leaf: format the resource path and fire the (recording) leaves.
TEST(OfficeFormsE2E, CutsceneSceneLoadPipeline) {
    struct Rec {
        std::string path, fadePalette;
        int steps = 0, delay = 0;
        bool presented = false;
    } rec;
    CutsceneSceneLeaves leaves{};
    leaves.ctx = &rec;
    leaves.loadStream = [](const char* p, void* c) {
        static_cast<Rec*>(c)->path = p;
    };
    leaves.present = [](void* c) { static_cast<Rec*>(c)->presented = true; };
    leaves.fadeBlack = [](const char* pal, int s, int d, void* c) {
        auto* r = static_cast<Rec*>(c);
        r->fadePalette = pal; r->steps = s; r->delay = d;
    };

    bool ran = Cutscene_LoadScene("Gericht.ed3", leaves, /*guard*/false);
    CHECK(ran);
    CHECK(rec.path == std::string("scenes/*Gericht.ed3"));
    CHECK(rec.presented);
    CHECK(rec.fadePalette == std::string("BLACK"));
    CHECK_EQ(rec.steps, 90);
    CHECK_EQ(rec.delay, 10);

    // With the re-entry guard set, nothing runs.
    Rec rec2{};
    leaves.ctx = &rec2;
    CHECK(!Cutscene_LoadScene("X", leaves, /*guard*/true));
    CHECK(rec2.path.empty());
}
