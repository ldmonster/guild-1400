// Unit (golden-vector) tests for the NEW favour-comment selection added to
// audio/voice_comment: VIBE_Voice_PlayCraftFavorComment (0x582448) and
// VIBE_Voice_PlayBuildingFavorComment (0x58269c). All edges are mocked so the
// pure selection logic (RNG gate, favour average, tier bucketing incl. the
// avg==75 quirk, side tie-break) is checked exactly.
#include "test.h"

#include "audio/voice_comment.h"

#include <string>
#include <vector>

using namespace guild;
using guild::audio::CraftUnit;

namespace {

// Recording sink (also serves bank loads).
struct FavorSink : audio::IVoiceCommentSink {
    bool played = false;
    u16  speaker = 0;
    int  bank = 0;
    int  channel = 0;
    std::string name;
    int  nextBank = 100;

    bool playPositionalSample(u16 id, int b, const std::string& n) override {
        // index-less overload -> channel defaults handled by the base; record -1.
        played = true; speaker = id; bank = b; channel = -1; name = n; return true;
    }
    bool playPositionalSample(u16 id, int b, int ch, const std::string& n) override {
        played = true; speaker = id; bank = b; channel = ch; name = n; return true;
    }
    void unloadSampleBank(int) override {}
    int  loadCommentBank(const std::string&) override { return nextBank++; }
};

// Callback context: favour table + RNG scripted rolls.
struct Ctx {
    std::vector<double> favorById;
    double floatRoll = 0.0;
    std::vector<int> modRolls; size_t modIdx = 0;
};
double FavorCb(u16 id, void* c) {
    auto* x = static_cast<Ctx*>(c);
    return (id < x->favorById.size()) ? x->favorById[id] : 0.0;
}
double FloatCb(void* c) { return static_cast<Ctx*>(c)->floatRoll; }
int ModCb(u16, void* c) {
    auto* x = static_cast<Ctx*>(c);
    return x->modIdx < x->modRolls.size() ? x->modRolls[x->modIdx++] : 0;
}

bool eq(const std::string& a, const char* b) { return a == b; }

} // namespace

// --- Bucketing incl. the avg==75 quirk ------------------------------------
TEST(AudioVoiceFavor, BucketQuirk) {
    CHECK_EQ(audio::BucketCraftFavorRaw(0), 0);
    CHECK_EQ(audio::BucketCraftFavorRaw(49), 0);
    CHECK_EQ(audio::BucketCraftFavorRaw(50), 1);
    CHECK_EQ(audio::BucketCraftFavorRaw(74), 1);
    CHECK_EQ(audio::BucketCraftFavorRaw(75), 75);  // intentional: left unchanged
    CHECK_EQ(audio::BucketCraftFavorRaw(76), 2);
    CHECK_EQ(audio::BucketCraftFavorRaw(100), 2);
}

// --- Craft: 50% skip gate -------------------------------------------------
TEST(AudioVoiceFavor, CraftSkipGate) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.6;  // > 0.5 -> silent
    audio::WorkerCommentPlayer p(&sink);
    CraftUnit u{true, true, true, 1};
    int bank = p.playCraftFavorComment(&u, 1, FavorCb, FloatCb, ModCb, &ctx);
    CHECK(!sink.played);
    CHECK_EQ(bank, 0);  // commandBank untouched (no load before gate)
}

// --- Craft: on-demand command-bank load when gate passes -------------------
TEST(AudioVoiceFavor, CraftLoadsBankOnDemand) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.4;
    audio::WorkerCommentPlayer p(&sink);
    // No units -> the (no-id) side path runs (v2 = -5, MITTLERE) and plays.
    int bank = p.playCraftFavorComment(nullptr, 0, FavorCb, FloatCb, ModCb, &ctx);
    CHECK_EQ(bank, 100);   // first fake handle
    CHECK_EQ(p.commandBank(), 100);
}

// --- Craft: no units -> speaker -5, MITTLERE -------------------------------
TEST(AudioVoiceFavor, CraftNoUnitsMediumMinus5) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.0;
    audio::WorkerCommentPlayer p(&sink);
    p.setCommandBank(500);
    int bank = p.playCraftFavorComment(nullptr, 0, FavorCb, FloatCb, ModCb, &ctx);
    CHECK_EQ(bank, 500);
    CHECK(sink.played);
    CHECK_EQ(sink.speaker, (u16)0xFFFB);  // (u16)-5
    CHECK_EQ(sink.channel, -1);
    CHECK(eq(sink.name, "MITTLERE_GUNST"));
}

// --- Craft: side-B majority, good favour -> GUTE on side-B id ---------------
TEST(AudioVoiceFavor, CraftSideBGood) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.0; ctx.favorById = {0, 90, 90};
    audio::WorkerCommentPlayer p(&sink); p.setCommandBank(7);
    CraftUnit us[2] = { {true,true,false,1}, {true,true,false,2} };
    p.playCraftFavorComment(us, 2, FavorCb, FloatCb, ModCb, &ctx);
    CHECK(sink.played);
    CHECK_EQ(sink.speaker, (u16)2);            // last side-B id
    CHECK(eq(sink.name, "GUTE_GUNST"));
}

// --- Craft: side-A majority, bad favour -> SCHLECHTE on side-A id -----------
TEST(AudioVoiceFavor, CraftSideABad) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.0; ctx.favorById = {0, 0, 10, 10};
    audio::WorkerCommentPlayer p(&sink); p.setCommandBank(7);
    CraftUnit us[2] = { {true,true,true,2}, {true,true,true,3} };
    p.playCraftFavorComment(us, 2, FavorCb, FloatCb, ModCb, &ctx);
    CHECK(sink.played);
    CHECK_EQ(sink.speaker, (u16)3);            // last side-A id
    CHECK(eq(sink.name, "SCHLECHTE_GUNST"));
}

// --- Craft: avg==75 quirk speaks GUTE, not MITTLERE ------------------------
TEST(AudioVoiceFavor, CraftAvg75IsGood) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.0; ctx.favorById = {0, 75};
    audio::WorkerCommentPlayer p(&sink); p.setCommandBank(7);
    CraftUnit u{true, true, true, 1};
    p.playCraftFavorComment(&u, 1, FavorCb, FloatCb, ModCb, &ctx);
    CHECK(sink.played);
    CHECK(eq(sink.name, "GUTE_GUNST"));
}

// --- Craft: side-A medium favour -> MITTLERE -------------------------------
TEST(AudioVoiceFavor, CraftSideAMedium) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.0; ctx.favorById = {0, 60, 60, 60};
    audio::WorkerCommentPlayer p(&sink); p.setCommandBank(7);
    CraftUnit us[3] = { {true,true,true,1}, {true,true,true,2}, {true,true,true,3} };
    p.playCraftFavorComment(us, 3, FavorCb, FloatCb, ModCb, &ctx);
    CHECK(sink.played);
    CHECK(eq(sink.name, "MITTLERE_GUNST"));
}

// --- Craft: inactive units are skipped (present but !active) ---------------
TEST(AudioVoiceFavor, CraftSkipsInactive) {
    FavorSink sink; Ctx ctx; ctx.floatRoll = 0.0; ctx.favorById = {0, 90};
    audio::WorkerCommentPlayer p(&sink); p.setCommandBank(7);
    CraftUnit us[2] = { {true,false,true,9}, {true,true,true,1} };  // [0] inactive
    p.playCraftFavorComment(us, 2, FavorCb, FloatCb, ModCb, &ctx);
    CHECK_EQ(sink.speaker, (u16)1);            // only the active side-A unit counts
    CHECK(eq(sink.name, "GUTE_GUNST"));
}

// --- Building: favour/33 buckets + slot-empty channel forcing --------------
TEST(AudioVoiceFavor, BuildingBuckets) {
    {   // favour 10 -> /33==0 -> bad; slot occupied -> RandomModulo(3) channel
        FavorSink sink; Ctx ctx; ctx.modRolls = {2}; ctx.favorById = {0, 10};
        audio::WorkerCommentPlayer p(&sink); p.setGreetingBank(9);
        int ch = p.playBuildingFavorComment(1, /*slotEmpty=*/false, FavorCb, ModCb, &ctx);
        CHECK_EQ(ch, 2);
        CHECK_EQ(sink.bank, 9);
        CHECK(eq(sink.name, "_SCHLECHTE_GUNST"));
    }
    {   // favour 40 -> /33==1 -> medium; slot empty -> channel forced to 3
        FavorSink sink; Ctx ctx; ctx.modRolls = {1}; ctx.favorById = {0, 40};
        audio::WorkerCommentPlayer p(&sink); p.setGreetingBank(9);
        int ch = p.playBuildingFavorComment(1, /*slotEmpty=*/true, FavorCb, ModCb, &ctx);
        CHECK_EQ(ch, 3);
        CHECK(eq(sink.name, "_MITTLERE_GUNST"));
    }
    {   // favour 99 -> /33==3 -> good
        FavorSink sink; Ctx ctx; ctx.modRolls = {0}; ctx.favorById = {0, 99};
        audio::WorkerCommentPlayer p(&sink); p.setGreetingBank(9);
        int ch = p.playBuildingFavorComment(1, false, FavorCb, ModCb, &ctx);
        CHECK_EQ(ch, 0);
        CHECK(eq(sink.name, "_GUTE_GUNST"));
    }
    {   // favour 66 -> /33==2 -> good (boundary)
        FavorSink sink; Ctx ctx; ctx.modRolls = {0}; ctx.favorById = {0, 66};
        audio::WorkerCommentPlayer p(&sink); p.setGreetingBank(9);
        p.playBuildingFavorComment(1, false, FavorCb, ModCb, &ctx);
        CHECK(eq(sink.name, "_GUTE_GUNST"));
    }
}
