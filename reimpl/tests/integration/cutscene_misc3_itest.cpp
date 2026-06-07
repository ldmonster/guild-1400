// Integration test: the Birth cutscene's "geschrei" voice-burst delay is driven
// by the REAL Math_RandomModulo sibling (guild::util::RandomModulo, no stub),
// which itself consumes the REAL CRT LCG (guild::crt::RandNext/Srand). This is
// exactly the live wiring: VIBE_Cutscene_Birth (0x4a7b5c) calls
// VIBE_Math_RandomModulo (0x58b89c) for each burst line's delay seed. We forward
// the module's mathRandomModulo hook into the genuine reconstructed function and
// assert the cross-module delay the binary would produce.
#include "test.h"

#include "sim/cutscene_misc3.h"
#include "sim/cutscene.h"
#include "util/math_random.h"
#include "crt/rand.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
// The REAL sibling, bound exactly as the binary wires it.
u32 realMathRandomModulo(u32 range) {
    return static_cast<u32>(util::RandomModulo(static_cast<u16>(range)));
}

struct Capture {
    std::vector<int> burstDelays;
    int sceneLoads = 0;
};
Capture* g_cap = nullptr;

void capVoice(int /*ch*/, void* /*bank*/, int /*idx*/, const char* tag, int delay) {
    if (g_cap && tag && std::string(tag) == "geburt_geschrei")
        g_cap->burstDelays.push_back(delay);
}
void capScene(const char*) { if (g_cap) g_cap->sceneLoads++; }

// Person record: +9 ill byte. We size to the real 536-byte record.
struct PersonRec { u8 bytes[600] = {0}; };
PersonRec g_father, g_mother;
void* findPerson(i32 id) {
    if (id == 100) return &g_father;
    if (id == 200) return &g_mother;
    return nullptr;
}
u8 illByte(void* p) { return p ? static_cast<PersonRec*>(p)->bytes[9] : 0; }
}  // namespace

TEST(CutsceneMisc3Itest, BirthBurstDelayFromRealCrtLcg) {
    Capture cap; g_cap = &cap;
    Cutscene3() = Cutscene3State{};

    CutsceneMisc3Hooks h{};
    h.mathRandomModulo = realMathRandomModulo;   // REAL util::RandomModulo
    h.voicePlaySample = capVoice;
    h.loadScene = capScene;
    h.personFind = findPerson;
    h.personIll = illByte;
    SetCutsceneMisc3Hooks(&h);

    // Seed the REAL CRT LCG deterministically (as a fresh session would).
    crt::Srand(42);

    // Seed the cutscene RNG so the burst count (RandInt(4)+2) is fixed.
    CutsceneRng rng; rng.SetSeed(7);

    CutsceneSlot slot{}; slot.partCount = 2;
    slot.partIds[0] = 100; slot.partIds[1] = 200;

    CutsceneBirth(rng, &slot);

    SetCutsceneMisc3Hooks(nullptr);
    g_cap = nullptr;

    // At least one geschrei line played; every delay = 1000 + RandomModulo(0x800).
    CHECK(!cap.burstDelays.empty());
    CHECK_EQ(cap.sceneLoads, 1);

    // Recompute the expected delays by replaying the same REAL sibling sequence.
    crt::Srand(42);
    bool allMatch = true;
    for (int d : cap.burstDelays) {
        int expect = 1000 + util::RandomModulo(0x800);
        if (d != expect) allMatch = false;
        // delay is in [1000, 1000+2047]
        if (d < 1000 || d > 1000 + 0x7FF) allMatch = false;
    }
    CHECK(allMatch);
}

// A second cross-module assertion: the burst LINE COUNT comes from the cutscene
// RNG (RandInt(4)+2 in [2,5]); independent of the CRT LCG. Proves the two RNGs
// stay distinct (the binary uses a SEPARATE cutscene LCG).
TEST(CutsceneMisc3Itest, BirstCountFromCutsceneRng) {
    Capture cap; g_cap = &cap;
    Cutscene3() = Cutscene3State{};

    CutsceneMisc3Hooks h{};
    h.mathRandomModulo = realMathRandomModulo;
    h.voicePlaySample = capVoice;
    h.personFind = findPerson;
    h.personIll = illByte;
    SetCutsceneMisc3Hooks(&h);

    crt::Srand(1);
    CutsceneRng rng; rng.SetSeed(99);
    // Precompute the expected count from the same cutscene LCG state.
    CutsceneRng probe; probe.SetSeed(99);
    probe.RandInt(2);  // the gendered birth line consumes one RandInt(2) first
    int expectCount = static_cast<int>(probe.RandInt(4)) + 2;

    CutsceneSlot slot{}; slot.partCount = 2;
    slot.partIds[0] = 100; slot.partIds[1] = 200;
    CutsceneBirth(rng, &slot);

    SetCutsceneMisc3Hooks(nullptr);
    g_cap = nullptr;

    CHECK(expectCount >= 2 && expectCount <= 5);
    CHECK_EQ(static_cast<int>(cap.burstDelays.size()), expectCount);
}
