// Integration test: the audio favour-comment selection driven by the REAL ai
// favourability sibling (guild::ai::ComputePersonFavorability, no stub) and the
// REAL util LCG. Proves the cross-module wiring: the spoken "Gunst" tier matches
// the genuine favourability score the binary's scorer produces.
#include "test.h"

#include "audio/voice_comment.h"
#include "ai/favorability.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"
#include "crt/rand.h"

#include <string>
#include <vector>

using namespace guild;
using guild::audio::CraftUnit;

namespace {

// Deterministic favourability data source for the real scorer.
struct FavData : ai::FavorabilityEnv {
    int relationByte = 0;
    ai::FavPersonFields Person(int) override {
        ai::FavPersonFields f; f.relationByteSelf = relationByte; return f;
    }
    ai::OfficeDefinition Office(u8) override { return {}; }
    int WorkstationWorkers(int) override { return 0; }
    int QueryByGoodType(int) override { return 0; }
    int QueryBeginWorkers(int, int, bool& g) override { g = false; return 0; }
    int GesetzState() override { return 0; }
    int InventorySlot(int, int) override { return 0; }
};

struct Ctx {
    FavData fav;
    int localPlayer = 0;
};

// The favour callback bound to the REAL scorer (applyLaw=1 as the binary passes).
double RealFavor(u16 npcId, void* c) {
    auto* x = static_cast<Ctx*>(c);
    return ai::ComputePersonFavorability(x->localPlayer, npcId, true, x->fav);
}
double RealFloat(void*) { return util::RandomFloatScaled(); }
int    RealMod(u16 n, void*) { return util::RandomModulo(n); }

struct RecSink : audio::IVoiceCommentSink {
    bool played = false; u16 spk = 0; int ch = 0; std::string name;
    bool playPositionalSample(u16 id, int, const std::string& n) override {
        played = true; spk = id; ch = -1; name = n; return true;
    }
    bool playPositionalSample(u16 id, int, int c, const std::string& n) override {
        played = true; spk = id; ch = c; name = n; return true;
    }
    void unloadSampleBank(int) override {}
    int  loadCommentBank(const std::string&) override { return 77; }
};

// Oracle: which craft tag the translated bucketing would speak for a favour value.
const char* ExpectedCraftTag(double favor) {
    int raw = audio::BucketCraftFavorRaw(static_cast<int>(favor));
    if (!raw) return "SCHLECHTE_GUNST";
    if (raw == 1) return "MITTLERE_GUNST";
    return "GUTE_GUNST";
}

} // namespace

// self == other: the REAL scorer returns 100.0 -> Good (single side-A worker).
TEST(AudioVoiceFavorItest, RealSelfIsGood) {
    Ctx ctx; ctx.localPlayer = 5;
    RecSink sink; audio::WorkerCommentPlayer p(&sink);
    crt::Srand(2);  // first float ~0.028 -> gate passes
    CraftUnit u{true, true, true, 5};  // worker == local player
    p.playCraftFavorComment(&u, 1, RealFavor, RealFloat, RealMod, &ctx);
    CHECK(sink.played);
    CHECK_EQ(sink.spk, (u16)5);
    CHECK(sink.name == "GUTE_GUNST");
}

// The spoken tier is CONSISTENT with the real scorer's value for a neutral NPC.
TEST(AudioVoiceFavorItest, RealTierConsistent) {
    Ctx ctx; ctx.localPlayer = 0; ctx.fav.relationByte = 128;
    RecSink sink; audio::WorkerCommentPlayer p(&sink);
    crt::Srand(2);
    double favor = ai::ComputePersonFavorability(0, 7, true, ctx.fav);
    const char* expect = ExpectedCraftTag(favor);
    CraftUnit u{true, true, true, 7};
    p.playCraftFavorComment(&u, 1, RealFavor, RealFloat, RealMod, &ctx);
    CHECK(sink.played);
    CHECK_EQ(sink.spk, (u16)7);
    CHECK(sink.name == expect);
}

// Building comment via the real scorer: self==owner -> favour 100 -> _GUTE_GUNST.
TEST(AudioVoiceFavorItest, RealBuildingSelfGood) {
    Ctx ctx; ctx.localPlayer = 5;
    RecSink sink; audio::WorkerCommentPlayer p(&sink); p.setGreetingBank(77);
    crt::Srand(2);
    int ch = p.playBuildingFavorComment(5, /*slotEmpty=*/false, RealFavor, RealMod, &ctx);
    CHECK(ch >= 0 && ch <= 2);
    CHECK(sink.name == "_GUTE_GUNST");
}

// The favour comment's RNG gate is the same LCG the rest of the game uses:
// reseeding reproduces the exact spoken/skipped outcome (determinism contract).
TEST(AudioVoiceFavorItest, GateUsesRealLcgDeterministic) {
    Ctx ctx; ctx.localPlayer = 5;

    crt::Srand(2);
    RecSink s1; audio::WorkerCommentPlayer p1(&s1);
    CraftUnit u{true, true, true, 5};
    p1.playCraftFavorComment(&u, 1, RealFavor, RealFloat, RealMod, &ctx);

    crt::Srand(2);
    RecSink s2; audio::WorkerCommentPlayer p2(&s2);
    p2.playCraftFavorComment(&u, 1, RealFavor, RealFloat, RealMod, &ctx);

    CHECK_EQ(s1.played, s2.played);
    CHECK(s1.name == s2.name);
    CHECK_EQ(s1.spk, s2.spk);
}
