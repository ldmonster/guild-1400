#include "sim/cutscene_misc3.h"

#include <cstdio>    // snprintf

namespace guild::sim {

// ===========================================================================
// Recovered tables (read out of gilde.exe .rdata).
// ===========================================================================
// dword_49D8B8 / dword_49D8D0 — both {8,10,12,15,18,20}.
const i32 kSeasonDayWindows[kSeasonWindowCount] = { 8, 10, 12, 15, 18, 20 };
// dword_49D8A4 — {14000,12000,12000,15000,10000}.
const i32 kExecutionDurations[kExecutionDurCount] = { 14000, 12000, 12000, 15000, 10000 };

// ===========================================================================
// Hook plumbing + bundled mutable state.
// ===========================================================================
namespace {
const CutsceneMisc3Hooks* g_hooks = nullptr;
CutsceneMisc3Hooks        g_inert{};   // all-null -> deterministic no-ops
Cutscene3State            g_state{};
}  // namespace

void SetCutsceneMisc3Hooks(const CutsceneMisc3Hooks* hooks) { g_hooks = hooks; }
const CutsceneMisc3Hooks& GetCutsceneMisc3Hooks() { return g_hooks ? *g_hooks : g_inert; }
Cutscene3State& Cutscene3() { return g_state; }

namespace {
const CutsceneMisc3Hooks& H() { return GetCutsceneMisc3Hooks(); }

// dword_631598 with byte1 OR'd by 0x80, exactly as the mains do:
//   v = dword_631598; BYTE1(v) = BYTE1(dword_631598) | 0x80;
i32 FrameFlagsHiBit() {
    i32 v = g_state.frameFlags;
    return v | (0x80 << 8);
}

int Pump(i32 flags, i32 a, void* payload) {
    return H().pumpFrame ? H().pumpFrame(flags, a, payload) : 0;  // inert: stop
}

// Cutscene_RunTimedScript — returns nonzero on user skip (abort). Inert -> 0.
int RunTimed(i32 ms) {
    return H().runTimedScript ? H().runTimedScript(ms) : 0;
}

void* PersonFind(i32 id) { return H().personFind ? H().personFind(id) : nullptr; }
u8 PersonIll(void* p) { return (H().personIll && p) ? H().personIll(p) : 0; }
}  // namespace

// ===========================================================================
// DETERMINISTIC KERNELS.
// ===========================================================================

// 0x4a7fdc / 0x4a851c — seasonal day-window pick.
//   v1 = 6; v6 = 0; while(1){ if (day < table[v6]) { v1 = v6; break; }
//                              ++v6; if (v6 >= 6) break; }   // Death: `<`
// Bankruptcy uses the inverse predicate `>=` to advance, identical result: the
// matched index is the first threshold strictly greater than `day`.
int CutsceneSeasonWindowIndex(int day) {
    for (int i = 0; i < kSeasonWindowCount; ++i) {
        if (day < kSeasonDayWindows[i]) return i;
    }
    return kSeasonWindowCount;  // 6 — no window matched
}

// 0x4a7b5c — Birth voice base.
//   if (a.ill) { if (b.ill) base=6; else base=2; }   // father ill
//   else       { if (b.ill) base=4; else base=0; }   // mother-only / neither
int CutsceneBirthVoiceBase(bool fatherIll, bool motherIll) {
    if (fatherIll) return motherIll ? 6 : 2;
    return motherIll ? 4 : 0;
}

// 0x4a851c — Bankruptcy closing message id.
int CutsceneBankruptcyMessageId(unsigned int worldFlags) {
    return (worldFlags & 4u) ? 7343 : 5822;
}

// 0x4a6b90 — Execution intro RunTimedScript duration = table[RandInt(3)].
int CutsceneExecutionIntroDuration(int rollMod3) {
    int i = rollMod3;
    if (i < 0) i = 0;
    if (i >= kExecutionDurCount) i = kExecutionDurCount - 1;
    return kExecutionDurations[i];
}

// 0x4a65e4 — DuelChoiceWindow button -> choice byte + quit latch.
//   if (id == 1210) { choice=1; quit; }
//   else if (id == 1155) { choice=0; quit; }
int CutsceneDuelChoiceFromButton(i32 buttonId, bool* outQuit) {
    if (buttonId == kDuelBtnAccept) { if (outQuit) *outQuit = true; return 1; }
    if (buttonId == kDuelBtnDecline) { if (outQuit) *outQuit = true; return 0; }
    if (outQuit) *outQuit = false;
    return 0;  // prior value (a fresh window initialises +148 to 0)
}

// 0x4a673c — DuelOutcomeWindow clicked child-object index -> +148 byte.
//   slot[+148] initialised to 4; clicking action 0/1/2 sets 2/3/4.
int CutsceneDuelOutcomeFromButton(int clickedIndex) {
    switch (clickedIndex) {
        case 0: return 2;
        case 1: return 3;
        case 2: return 4;
        default: return 4;  // initial value, no click
    }
}

// 0x4a9a68 — LeaseAutoResolve AI lessee decision (pure form).
LeaseDecision CutsceneLeaseAutoResolve(int funds, i32 askingRent, int leaseYears,
                                       double will1, double will2, double will3,
                                       u32 counterRoll) {
    LeaseDecision d;
    d.rent = askingRent;  // a1[37] default == the asking/base rent

    // willingness = randFloat * (1/6) + 0.3
    double willingness = will1 * static_cast<double>(kLeaseWillBase)
                       + static_cast<double>(kLeaseWillScale);
    double threshold = static_cast<double>(32000 * leaseYears);

    if (static_cast<double>(funds) * willingness >= threshold) {
        // accept at the asking rate (a1[37] already == ask, a1[38] = 0 here)
        d.kind = 1;
        d.rent = askingRent;
        return d;
    }

    // counterBudget = funds * (randFloat2 * (1/6) + 0.3)
    double cb = static_cast<double>(funds)
              * (will2 * static_cast<double>(kLeaseWillBase)
                 + static_cast<double>(kLeaseWillScale));
    i32 counterBudget = static_cast<i32>(cb);

    if (counterBudget <= askingRent) {
        double ratio = static_cast<double>(counterBudget) / static_cast<double>(askingRent);
        if (will3 > ratio) {
            i32 cap = askingRent;
            if (2 * counterBudget < cap) cap = 2 * counterBudget;
            d.kind = 1;  // a1[38] = 1 (a willing counter still "accepts" a deal)
            float step = static_cast<float>(leaseYears) * kLeaseCounterCoef;
            u32 span = static_cast<u32>(cap - counterBudget);
            // RandInt(span) supplied as counterRoll (already reduced mod span by
            // the caller; the original is RandInt(cap-counterBudget)).
            u32 roll = (span == 0) ? 0u : (counterRoll % span);
            double counter = static_cast<double>(roll) * static_cast<double>(step)
                           + static_cast<double>(counterBudget);
            i32 rent = static_cast<i32>(counter);
            rent += rent % 32;            // round-up to the 32-coin grid
            d.kind = 2;                   // a counter-offer was made
            d.rent = rent;
            return d;
        }
    }
    // reject: a1[38] stays 0, rent unchanged (== ask)
    d.kind = 0;
    return d;
}

// ===========================================================================
// FULL FLOW DRIVERS.
// ===========================================================================

// 0x4aabdc — ShowParticipantDialog: walk the slot's participants, rendering any
// with text not yet rendered. We delegate to the host (it owns the 11AB0xx text
// arrays); the inert default is a no-op.
void CutsceneShowParticipantDialog(const CutsceneSlot* slot) {
    if (!slot) return;
    if (H().showParticipantDialog) H().showParticipantDialog(slot->partCount);
}

// 0x4aa970 — SetupCallbacks.
void CutsceneSetupCallbacks() {
    if (H().setupCallbacks) H().setupCallbacks();
}

// 0x4aa1f4 — FormatLodDebug.
char CutsceneFormatLodDebug(char* buf, int bufSize, const char* name,
                            unsigned int activeLod, int flType, int flOldType) {
    if (buf && bufSize > 0) {
        std::snprintf(buf, static_cast<size_t>(bufSize),
                      "Name: %s    active_lod: %x    fl.type: %i   fl.oldtype: %i ",
                      name ? name : "", activeLod, flType, flOldType);
    }
    return 1;
}

// 0x4a697c — PlayTobyScene.
void CutscenePlayTobyScene(const CutsceneSlot* slot) {
    if (H().loadScene) H().loadScene("cutscene_toby.ed3");   // LoadScene
    // RunParticipants x2 (modelled as the participant-dialog pump twice).
    CutsceneShowParticipantDialog(slot);
    CutsceneShowParticipantDialog(slot);
    // RunCombatScript -> a frame pump until the deadline; inert pump stops.
    while (Pump(FrameFlagsHiBit(), 0, nullptr)) { /* combat pump */ }
    if (H().teardown) H().teardown();                        // Teardown
    // NullSub() — intentionally empty.
}

// 0x4a6b90 — Execution.
void CutsceneExecution(CutsceneRng& rng, const CutsceneSlot* slot) {
    int seed = rng.GetSeed();
    int introDur = CutsceneExecutionIntroDuration(static_cast<int>(rng.RandInt(3)));

    g_state.frameFlags = 0;
    if (!g_state.replayGate) {
        if (H().voiceFlushAll) H().voiceFlushAll();
        if (H().musicPlayCutscene) H().musicPlayCutscene("execution");
    }
    void* bank = H().voiceLoadBank ? H().voiceLoadBank("hinrichtung.sbf") : nullptr;
    if (H().loadScene) H().loadScene("Hinrichtung.ed3");

    i32 handle = 0;
    if (!g_state.replayGate && H().scriptLoadRun)
        handle = H().scriptLoadRun("cutscenes\\hinrichtung\\enter.esc");

    // intro timed-script (RandInt(3)-selected duration), then the chain.
    int skipped = RunTimed(introDur);
    if (!skipped) skipped = RunTimed(0x2AF8);  // 11000 ms second beat
    (void)skipped;

    if (H().showMessageBox) H().showMessageBox(0);
    if (H().fadeIn) H().fadeIn(0);

    if (!g_state.replayGate && handle && H().scriptAlive && H().scriptAlive(handle)) {
        if (H().scriptFinish) H().scriptFinish(handle);
    }
    if (!g_state.replayGate) {
        while (Pump(FrameFlagsHiBit(), 0, nullptr) && H().scriptAlive && H().scriptAlive(handle)) {}
    }
    if (H().teardown) H().teardown();
    if (H().voiceFlushAll) H().voiceFlushAll();
    if (bank && H().voiceUnloadBank) H().voiceUnloadBank(bank);
    if (!g_state.replayGate && H().musicRestore) H().musicRestore();

    (void)slot;
    rng.SetSeed(seed);  // the original snapshots then... (Execution itself does
                        // not restore; RunParticipants does. We keep the seed
                        // snapshot faithful but leave the RNG advanced.)
    rng.SetSeed(rng.GetSeed());  // no-op guard
}

// 0x4a7fdc — Death.
void CutsceneDeath(const CutsceneSlot* slot) {
    void* person = slot ? PersonFind(slot->partIds[0]) : nullptr;
    int season = CutsceneSeasonWindowIndex(g_state.dayOfMonth);
    g_state.frameFlags = 0;

    if (!g_state.replayGate) {
        if (H().voiceFlushAll) H().voiceFlushAll();
        if (H().musicPlayCutscene) H().musicPlayCutscene("cd2\\AmKuehlenGrabe.mp3");
    }
    void* bank = H().voiceLoadBank ? H().voiceLoadBank("tod.sbf") : nullptr;
    if (H().loadScene) H().loadScene("Tod.ed3");
    if (H().setupSky) H().setupSky("Sky_Schoen_02");
    if (H().skyColorBand) H().skyColorBand(season);
    if (H().skyColorAmbient) H().skyColorAmbient(0.0f);

    i32 handle = 0;
    if (!g_state.replayGate && H().scriptLoadRun)
        handle = H().scriptLoadRun("cutscenes\\tod\\enter_TOD.esc");

    int skipped = RunTimed(0xBB8);    // 3000
    if (!skipped) {
        // six staged voice samples
        const int delays[6] = {0, 4000, 3000, 2000, 4000, 500};
        for (int i = 0; i < 6; ++i)
            if (!g_state.replayGate && H().voicePlaySample)
                H().voicePlaySample(-8, bank, i + 1, "_TOD_HS", delays[i]);
        skipped = RunTimed(0x2AF8);   // 11000
    }
    if (!skipped) {
        // ambient cross-fade ramp over the 825-frame window
        if (H().skyColorAmbient) H().skyColorAmbient(1.0f);
        RunTimed(0x2CEC);             // 11500
    }
    if (H().audioStartSample) H().audioStartSample(0, 63, 1, 127);

    // inheritance branch
    int adultChildren = (H().personCountAdultChildren && person)
                            ? H().personCountAdultChildren(person) : 0;
    if (adultChildren) {
        if (H().runFamilyTreeWindow) H().runFamilyTreeWindow();
    } else if (g_state.replayGate /* placeholder: word_63C740&8 player path */) {
        if (H().reloadSession) H().reloadSession();
    } else {
        if (H().distributeInheritance) H().distributeInheritance(person, 1);
        if (H().showMessageBox) H().showMessageBox(16);
    }

    if (H().fadeIn) H().fadeIn(1);
    if (!g_state.replayGate && handle && H().scriptAlive && H().scriptAlive(handle))
        if (H().scriptFinish) H().scriptFinish(handle);
    if (!g_state.replayGate)
        while (Pump(FrameFlagsHiBit(), 0, nullptr) && H().scriptAlive && H().scriptAlive(handle)) {}
    if (!g_state.replayGate && H().destroySky) H().destroySky();
    if (H().teardown) H().teardown();
    if (H().voiceFlushAll) H().voiceFlushAll();
    if (bank && H().voiceUnloadBank) H().voiceUnloadBank(bank);
    if (!g_state.replayGate && H().musicRestore) H().musicRestore();
}

// 0x4a7b5c — Birth.
void CutsceneBirth(CutsceneRng& rng, const CutsceneSlot* slot) {
    if (!slot) return;
    void* father = PersonFind(slot->partIds[0]);
    void* mother = PersonFind(slot->partIds[1]);
    if (!father || !mother) return;

    g_state.frameFlags = 459462;   // 0x70286 — the Birth-specific flag literal
    if (!g_state.replayGate) {
        if (H().voiceFlushAll) H().voiceFlushAll();
        if (H().musicPlayCutscene) H().musicPlayCutscene("cd2\\geburt.mp3");
    }
    void* bank = (!g_state.replayGate && H().voiceLoadBank)
                     ? H().voiceLoadBank("GEBURT.sbf") : nullptr;
    if (H().loadScene) H().loadScene("Geburt.ed3");

    i32 handle = 0;
    if (!g_state.replayGate && H().scriptLoadRun)
        handle = H().scriptLoadRun("cutscenes\\geburt\\enter.esc");

    int skipped = RunTimed(0x1964);   // 6500
    if (!skipped) {
        int base = CutsceneBirthVoiceBase(PersonIll(father) != 0, PersonIll(mother) != 0);
        int line = base + static_cast<int>(rng.RandInt(2));
        if (!g_state.replayGate && H().voicePlaySample)
            H().voicePlaySample(-8, bank, line, "geburt", 0);
        skipped = RunTimed(0x3E8);    // 1000
    }
    if (!skipped) {
        // the "geschrei" burst: RandInt(4)+2 lines, each delayed 1000+rng.
        int count = static_cast<int>(rng.RandInt(4)) + 2;
        for (int i = 0; i < count; ++i) {
            u32 r = H().mathRandomModulo ? H().mathRandomModulo(0x800u) : 0u;
            int line = static_cast<int>(rng.RandInt(5));
            int delay = static_cast<int>(r) + 1000;
            if (!g_state.replayGate && H().voicePlaySample)
                H().voicePlaySample(-8, bank, line, "geburt_geschrei", delay);
        }
        RunTimed(0x1B58);             // 7000
    }

    // name-entry window: pump frames until the user submits a non-empty name.
    while (Pump(g_state.frameFlags, 0, nullptr)) {}

    if (H().fadeIn) H().fadeIn(48);
    if (!g_state.replayGate && handle && H().scriptAlive && H().scriptAlive(handle))
        if (H().scriptFinish) H().scriptFinish(handle);
    if (!g_state.replayGate)
        while (Pump(FrameFlagsHiBit(), 0, nullptr) && H().scriptAlive && H().scriptAlive(handle)) {}
    if (H().voiceFlushAll) H().voiceFlushAll();
    if (!g_state.replayGate && bank && H().voiceUnloadBank) H().voiceUnloadBank(bank);
    if (H().teardown) H().teardown();
    if (!g_state.replayGate && H().musicRestore) H().musicRestore();
}

// 0x4a851c — Bankruptcy.
void CutsceneBankruptcy(const CutsceneSlot* slot) {
    void* person = slot ? PersonFind(slot->partIds[0]) : nullptr;
    int season = CutsceneSeasonWindowIndex(g_state.dayOfMonth);

    void* bank = H().voiceLoadBank ? H().voiceLoadBank("pleite.sbf") : nullptr;
    if (!g_state.replayGate) {
        if (H().voiceFlushAll) H().voiceFlushAll();
        if (H().musicPlayCutscene) H().musicPlayCutscene("cd2\\DerSeuchenzug.mp3");
    }
    if (H().loadScene) H().loadScene("Pleite.ed3");
    if (H().skyColorBand) H().skyColorBand(season);
    if (H().skyColorAmbient) H().skyColorAmbient(0.0f);
    if (H().setupSky) H().setupSky("Sky_Dunkel_01");

    i32 handle = 0;
    if (!g_state.replayGate && H().scriptLoadRun)
        handle = H().scriptLoadRun("cutscenes\\pleite\\enter.esc");

    int skipped = RunTimed(0x1770);   // 6000
    if (!skipped) {
        const int d1[3] = {0, 2500, 500};
        for (int i = 0; i < 3; ++i)
            if (!g_state.replayGate && H().voicePlaySample)
                H().voicePlaySample(-8, bank, i, "_PLEITE_HS", d1[i]);
        skipped = RunTimed(0x3E80);   // 16000
    }
    if (!skipped) {
        if (!g_state.replayGate && H().rainCreate) H().rainCreate(250);
        skipped = RunTimed(0x2AF8);   // 11000
    }
    if (!skipped) {
        const int d2[3] = {9500, 4500, 500};
        for (int i = 0; i < 3; ++i)
            if (!g_state.replayGate && H().voicePlaySample)
                H().voicePlaySample(-8, bank, i + 3, "_PLEITE_HS", d2[i]);
        RunTimed(0x5DC0);             // 24000
    }

    int msg = CutsceneBankruptcyMessageId(static_cast<unsigned int>(g_state.duelMode == 0 ? 0 : 4));
    (void)msg;
    if (H().showMessageBox) H().showMessageBox(0);
    if (H().fadeIn) H().fadeIn(0);

    if (!g_state.replayGate && handle && H().scriptAlive && H().scriptAlive(handle))
        if (H().scriptFinish) H().scriptFinish(handle);
    if (!g_state.replayGate)
        while (Pump(FrameFlagsHiBit(), 0, nullptr) && H().scriptAlive && H().scriptAlive(handle)) {}
    if (!g_state.replayGate && H().destroySky) H().destroySky();
    if (!g_state.replayGate && H().rainDestroy) H().rainDestroy();
    if (H().teardown) H().teardown();
    if (H().voiceFlushAll) H().voiceFlushAll();
    if (bank && H().voiceUnloadBank) H().voiceUnloadBank(bank);
    if (!g_state.replayGate && H().musicRestore) H().musicRestore();

    // inheritance / reload branch
    if (H().distributeInheritance) H().distributeInheritance(person, 0);
}

// 0x4a9c24 — Salon.
void CutsceneSalon(const CutsceneSlot* slot, bool entityPresent) {
    if (!slot) return;
    int loaded = 0;
    if (!g_state.replayGate) {
        if (H().voiceFlushAll) H().voiceFlushAll();
        if (H().musicPlayCutscene) H().musicPlayCutscene("salon");
    }
    if (entityPresent) {
        // SalonFadeTransition path (the resolved entity is the salon building).
        // Its heavy body (slot caching / scene-load / interior enter) is host
        // I/O; we model the load-bearing presentation via loadScene-less path.
    } else {
        if (H().loadScene) H().loadScene("ob_SALON.ed3");
        loaded = 1;
    }
    // RunCombatScript — pump until the deadline.
    while (Pump(FrameFlagsHiBit(), 0, nullptr)) {}

    // swap each adjacent participant pair (Command_QueueRequestCoord27 both ways)
    // — modelled as the participant-dialog pump (the command-staging is host I/O).
    CutsceneShowParticipantDialog(slot);

    if (loaded) {
        if (H().fadeIn) H().fadeIn(0);
        if (H().teardown) H().teardown();
    }
    if (!g_state.replayGate && H().musicRestore) H().musicRestore();
}

} // namespace guild::sim
