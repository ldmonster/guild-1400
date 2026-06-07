#include "test.h"
#include "world/tutorial_mission.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
// Recording fake for the tutorial engine leaves (no reconstructed sibling: these
// are Form/Text/Voice/Widget GUI glue). The deterministic cores decide WHAT to do;
// the fake records it.
struct Rec {
    std::vector<unsigned> rendered;
    int voicePlays = 0;
    int voiceStops = 0;
    int lastSliderValue = -1;
    bool bannerSet = false;
} g_rec;

int  ERenderText(unsigned id)                 { g_rec.rendered.push_back(id); return (int)id; }
int  EPlayVoice(int, int, int, const char*)   { ++g_rec.voicePlays; return 1234; }
void EStopVoice(int, int)                     { ++g_rec.voiceStops; }
void EBanner()                                { g_rec.bannerSet = true; }
int  ESetSlider(int, int, int, int v)         { g_rec.lastSliderValue = v; return 0; }
int  EVoicePlaying(int)                       { return 1; }

TutorialMissionHooks MakeHooks() {
    TutorialMissionHooks h{};
    h.renderText     = &ERenderText;
    h.playVoice      = &EPlayVoice;
    h.stopVoice      = &EStopVoice;
    h.setStatusBanner = &EBanner;
    h.setSliderValue = &ESetSlider;
    h.voiceIsPlaying = &EVoicePlaying;
    return h;
}
} // namespace

// End-to-end: a tutorial step's full runtime lifecycle through the recovered cores
// — reset, open the event panel, show a voiced reminder, drive the progress slider
// down as the step elapses, run the highlight-arrow bob, then check-and-stop the
// voice on the chapter-completion click, and finally shut the tutorial down.
TEST(TutorialMissionE2E, StepLifecycle) {
    TutorialMissionHooks hooks = MakeHooks();
    SetTutorialMissionHooks(&hooks);

    TutorialRuntime rt;
    TutorialResetChapterPointer(rt);
    CHECK_EQ(rt.panelHost, 0);
    CHECK_EQ(rt.chapterId, -1);

    // Arm a panel host + chapter, like the chapter-activation path would.
    rt.panelHost = 0x1000;     // host slot present
    rt.chapterId = 4242;
    rt.phaseByte = kTutPhaseActive;
    rt.stepValid = 1;

    // Open the main event panel: host present, not yet mounted, create succeeds.
    CHECK_EQ(TutorialOpenMainEventPanelResult(rt.panelHost, /*hostOpen=*/false,
                                              /*createOk=*/true), 0);

    // Now a panel is mounted (hostOpen). SetDialogTexts can render.
    CHECK_EQ(TutorialSetDialogTextsResult(rt.panelHost, /*hostOpen=*/true), 0);

    // Show a voiced reminder (step carries text + voice).
    TutorialPanelAction pa = TutorialClassifyShowPanel(rt.stepValid,
                                                       /*panelTextId=*/0x2000,
                                                       /*voicePtr=*/0x3000);
    CHECK(pa == TutorialPanelAction::kShowVoiced);
    if (pa == TutorialPanelAction::kShowVoiced) {
        const auto& h = GetTutorialMissionHooks();
        if (h.setStatusBanner) h.setStatusBanner();
        if (h.renderText) h.renderText(0x2000);
        if (h.playVoice)  rt.voiceHandle = h.playVoice(-7, 0, -1, "REMINDER");
    }
    CHECK_EQ(g_rec.voicePlays, 1);
    CHECK(g_rec.bannerSet);
    CHECK_EQ(rt.voiceHandle, 1234);

    // Drive the progress slider down over the step's 1000-tick duration.
    CHECK(TutorialSliderActive(rt.stepValid, rt.phaseByte));
    int start = 5000;
    int v0 = TutorialProgressSliderValue(start,       start, 1000);
    int vmid = TutorialProgressSliderValue(start + 500, start, 1000);
    int vend = TutorialProgressSliderValue(start + 1000, start, 1000);
    CHECK_EQ(v0, 400);
    CHECK_EQ(vmid, 200);
    CHECK_EQ(vend, 0);
    if (auto* set = GetTutorialMissionHooks().setSliderValue)
        set(rt.sliderObj, 0, 400, vmid);
    CHECK_EQ(g_rec.lastSliderValue, 200);

    // Highlight-arrow bob: one full phase cycle.
    rt.arrowPhase = 2; rt.arrowTick = start;
    CHECK(TutorialArrowStep(rt, start + 10, 50.0f, 200.0f, 30.0f)
          == TutorialArrowResult::kStay);
    CHECK(TutorialArrowStep(rt, start + 50, 50.0f, 200.0f, 30.0f)
          == TutorialArrowResult::kTravel);
    CHECK_EQ(rt.arrowPhase, 3);

    // Chapter-completion click stops the narrated voice (dialogResult != -1,
    // click id == chapterId).
    TutorialVoiceCheck vc = TutorialCheckStateAndStopVoice(
        rt.panelHost, /*hostOpen=*/true, /*screenActive=*/true,
        /*dialogResult=*/1210, /*clickedId=*/rt.chapterId, rt.chapterId, /*menuState=*/0);
    CHECK(vc == TutorialVoiceCheck::kStopped);
    if (vc == TutorialVoiceCheck::kStopped) {
        if (auto* stop = GetTutorialMissionHooks().stopVoice) stop(rt.voiceHandle, 0);
        rt.voiceHandle = 0;
    }
    CHECK_EQ(g_rec.voiceStops, 1);
    CHECK_EQ(rt.voiceHandle, 0);

    // Close the panel and shut down.
    CHECK_EQ(TutorialCloseEventPanelResult(rt.panelHost, /*hostOpen=*/true), 0);
    TutorialShutdownReset(rt);
    CHECK_EQ((int)rt.phaseByte, 0);
    CHECK_EQ(rt.panelHost, 0);
    CHECK_EQ(rt.chapterId, -1);
    CHECK_EQ(rt.sliderForm, -1);

    SetTutorialMissionHooks(nullptr);   // restore inert default
}

// End-to-end of the mission reward-summary voiceover sequence: the four staged
// lines/body each render their text id and (when audio is on) play their voice
// sample, abortable by a panel click.
TEST(TutorialMissionE2E, RewardSummarySequence) {
    g_rec = Rec{};
    TutorialMissionHooks hooks = MakeHooks();
    hooks.audioIsInitialized = []() { return 1; };
    SetTutorialMissionHooks(&hooks);
    const auto& h = GetTutorialMissionHooks();

    // The matched descriptor (value field 0x500, voice index 7).
    const int descValue = 0x500;
    const int voiceIdx  = 7;

    // Stage 0..2: header lines.
    if (h.renderText) {
        h.renderText(kMissionRewardLine0TextId);
        h.renderText(kMissionRewardLine1TextId);
        h.renderText(kMissionRewardLine2TextId);
        // Body: the 0x17AB header then the descriptor body id.
        h.renderText(kMissionRewardBodyTextId);
        h.renderText((unsigned)MissionRewardBodyTextId(descValue));
    }
    CHECK_EQ(g_rec.rendered.size(), (size_t)5);
    if (g_rec.rendered.size() == 5) {
        CHECK_EQ(g_rec.rendered[0], kMissionRewardLine0TextId);
        CHECK_EQ(g_rec.rendered[3], kMissionRewardBodyTextId);
        CHECK_EQ(g_rec.rendered[4], (unsigned)(descValue + 2));
    }

    // Body voice sample name.
    char sample[64];
    MissionRewardVoiceSample(sample, sizeof sample, voiceIdx);
    CHECK(std::strcmp(sample, "_AUFTRAEGE_ERFOLG_HS_07") == 0);
    if (h.playVoice) h.playVoice(-7, 0, 0, sample);
    CHECK_EQ(g_rec.voicePlays, 1);

    // A panel click on the summary child id aborts the wait loop.
    CHECK(MissionRewardSkipRequested(1210, 99, 99));
    CHECK(!MissionRewardSkipRequested(-1, 99, 99));

    // With audio off, the wait runs the 250-tick timeout instead.
    CHECK_EQ(MissionRewardTimeoutDeadline(8000), 8250u);

    SetTutorialMissionHooks(nullptr);
}
