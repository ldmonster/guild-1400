// End-to-end flow for the cutscene-misc substrate: allocate a duel slot,
// step it through a duel-outcome roll sequence, drive the progress bar across
// its lifetime, and tear the slot down via the pending-script finish path.
#include "test.h"
#include "sim/cutscene.h"
#include "sim/cutscene_misc.h"

#include <vector>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct E2EMock {
    std::vector<void*> finished;
    std::vector<int>   barValues;
    int                voiceFlush = 0, musicPlay = 0, musicRestore = 0;
};
E2EMock* g_e2e = nullptr;

void e2e_scriptFinish(void* s)              { g_e2e->finished.push_back(s); }
void e2e_voiceFlush()                       { g_e2e->voiceFlush++; }
void e2e_musicPlay()                        { g_e2e->musicPlay++; }
void e2e_musicRestore()                     { g_e2e->musicRestore++; }
void e2e_setValue(int, int, int, int v)     { g_e2e->barValues.push_back(v); }
void e2e_setEnabled(int, int)               {}

} // namespace

TEST(CutsceneMisc, E2EDuelSlotStepFinishFlow) {
    E2EMock mock; g_e2e = &mock;
    CutsceneMiscHooks h{};
    h.scriptFinish      = e2e_scriptFinish;
    h.voiceFlushAll     = e2e_voiceFlush;
    h.musicPlayCutscene = e2e_musicPlay;
    h.musicRestore      = e2e_musicRestore;
    h.objectSetValue    = e2e_setValue;
    h.objectSetEnabled  = e2e_setEnabled;
    SetCutsceneMiscHooks(&h);

    // --- 1. allocate a duel slot from a template ----------------------------
    CutsceneTable table;
    CutsceneSlot tmpl{};
    std::memset(&tmpl, 0, sizeof(tmpl));
    tmpl.id   = -1;     // template carries no id; AllocSlot stamps the new id
    tmpl.type = 4;      // duel
    tmpl.partCount = 2;
    tmpl.partIds[0] = 1001;
    tmpl.partIds[1] = 1002;
    CutsceneSlot* slot = table.AllocSlot(tmpl, /*newId=*/77);
    CHECK(slot != nullptr);
    CHECK_EQ(slot->id, 77);
    CHECK_EQ(slot->type, (u8)4);
    CHECK_EQ(slot->priority, (u8)8);    // alloc default

    // --- 2. enter cutscene: pause game audio --------------------------------
    CutscenePauseGame();
    CHECK_EQ(mock.voiceFlush, 1);
    CHECK_EQ(mock.musicPlay, 1);

    // --- 3. drive the progress bar over the cutscene window ------------------
    SetCutsceneMiscGameTick(5000);
    CutsceneUpdateProgressBar(/*widget=*/3, /*span=*/4000, /*reset=*/true);
    SetCutsceneMiscGameTick(6000);   // elapsed 1000 -> 75
    SetCutsceneMiscGameTick(7000);   // elapsed 2000 -> 50
    int v50;
    {
        i32 r = CutsceneUpdateProgressBar(3, 4000, false);  // at 7000
        v50 = r;
    }
    SetCutsceneMiscGameTick(9000);   // elapsed 4000 -> 0
    i32 v0 = CutsceneUpdateProgressBar(3, 4000, false);
    CHECK_EQ(v50, 50);
    CHECK_EQ(v0, 0);

    // --- 4. step the duel outcome roll into the slot's choice byte ----------
    // The duel main reads each participant's choice byte at record+148; here we
    // roll a deterministic AI-choice tier into a scratch record using the slot's
    // own seed. Choice mode -> 0/1; outcome mode -> 2/3/4.
    CutsceneRng rng;
    rng.SetSeed(0x3039);   // golden seed
    u8 rec[256]; std::memset(rec, 0, sizeof(rec));
    CutsceneMisc().duelMode = 0;
    i32 roll = CutsceneRollDuelOutcomeTier(rng, rec);
    CHECK_EQ(roll, 9);
    CHECK_EQ(rec[148], (u8)1);

    rng.SetSeed(0x3039);
    CutsceneMisc().duelMode = 1;
    roll = CutsceneRollDuelOutcomeTier(rng, rec);
    CHECK_EQ(roll, 69);
    CHECK_EQ(rec[148], (u8)4);
    CutsceneMisc().duelMode = 0;

    // --- 5. finish: queue a pending script record, run the finish scan ------
    std::vector<u8> scripts(kScriptRecordCount * kScriptRecordStride, 0);
    for (int i = 0; i < kScriptRecordCount; ++i)
        *reinterpret_cast<i32*>(scripts.data() + i * kScriptRecordStride
                                + kScriptOffHandle) = -1;
    // record 4 = the duel's pending cutscene script.
    u8* r4 = scripts.data() + 4 * kScriptRecordStride;
    *reinterpret_cast<i32*>(r4 + kScriptOffHandle) = 500;
    r4[kScriptOffFlags] = 0x01;          // active
    *reinterpret_cast<i32*>(r4 + kScriptOffKind) = -2;   // pending
    CutsceneFinishPendingScripts(scripts.data());
    CHECK_EQ((int)mock.finished.size(), 1);
    CHECK(mock.finished[0] == r4);

    // --- 6. exit cutscene: resume audio and free the slot -------------------
    CutsceneResumeGame();
    CHECK_EQ(mock.musicRestore, 1);
    CHECK(table.RemoveById(77));
    CHECK(table.FindById(77) == nullptr);

    SetCutsceneMiscHooks(nullptr);
}
