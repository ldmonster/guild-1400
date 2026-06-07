#include "tests/framework/test.h"
#include "audio/music_world.h"
#include "audio/ambient.h"
#include "render/snow.h"
#include "render/rain.h"
#include "render/weather.h"
#include "util/math_random.h"
#include "crt/rand.h"
#include <cmath>
#include <cstring>

using namespace guild;

namespace {
bool feq(float a, float b, float eps = 1e-6f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Build a small director: one outdoor entry (id 9876) + two location entries.
audio::MusicDirector makeDirector(audio::IMusicSink* sink) {
    audio::MusicDirector d;
    d.sink = sink;
    audio::TrackEntry outdoor;
    outdoor.ids = {audio::kOutdoorTrackId};
    d.table.push_back(outdoor); // index 0
    audio::TrackEntry tavern;
    tavern.ids = {10, 11};
    tavern.name = "msx\\tavern.mp3";
    d.table.push_back(tavern); // index 1
    audio::TrackEntry market;
    market.ids = {20};
    market.name = "msx\\market.mp3";
    d.table.push_back(market); // index 2
    return d;
}

struct MockSink : audio::IMusicSink {
    int nextHandle = 1000;
    std::string lastLoaded;
    int loadCount = 0;
    int stopCount = 0;
    int loadTrack(const std::string& name, int) override {
        lastLoaded = name;
        ++loadCount;
        return nextHandle++;
    }
    void stopTrack(int, int) override { ++stopCount; }
};
} // namespace

// --- Season derivation (calendar-driven) -----------------------------------
TEST(AudioMusicWorld, SeasonFromDay) {
    sim::GameTime t{};
    t.day = 0;  CHECK_EQ(audio::SeasonFromDay(t), audio::kSpring);
    t.day = 1;  CHECK_EQ(audio::SeasonFromDay(t), audio::kSummer);
    t.day = 2;  CHECK_EQ(audio::SeasonFromDay(t), audio::kAutumn);
    t.day = 3;  CHECK_EQ(audio::SeasonFromDay(t), audio::kWinter);
    t.day = 4;  CHECK_EQ(audio::SeasonFromDay(t), audio::kSpring);
    t.day = 47; CHECK_EQ(audio::SeasonFromDay(t), 47 % 4);
}

// --- FindTrackById / FindActiveTrackSlot ------------------------------------
TEST(AudioMusicWorld, FindTrack) {
    MockSink sink;
    auto d = makeDirector(&sink);
    CHECK_EQ(audio::FindTrackById(d, audio::kOutdoorTrackId), 0);
    CHECK_EQ(audio::FindTrackById(d, 11), 1);
    CHECK_EQ(audio::FindTrackById(d, 20), 2);
    CHECK_EQ(audio::FindTrackById(d, 9999), -1);
    CHECK_EQ(audio::FindActiveTrackSlot(d), -1);
    d.table[1].active = true;
    CHECK_EQ(audio::FindActiveTrackSlot(d), 1);
}

// --- Season -> track selection (golden, seeded RNG) -------------------------
// crt seed=1 => RandomModulo(117)%3 sequence = [2,1,0,1,1,2,0,0].
// Spring: variant!=0 -> ImFruehling ; variant 0 -> Burgfraeulein.
TEST(AudioMusicWorld, SelectOutdoorSpringGolden) {
    MockSink sink;
    auto d = makeDirector(&sink);
    crt::Srand(1);
    int slot = audio::SelectOutdoorSeasonTrack(d, audio::kSpring);
    CHECK_EQ(slot, 0);
    CHECK(d.table[0].active);
    // First variant rolled is 2 (non-zero) -> ImFruehling.
    CHECK(d.table[0].name == "cd1\\ImFruehling.mp3");
    CHECK(sink.lastLoaded == "cd1\\ImFruehling.mp3");
    CHECK(d.currentTrackHandle != 0);
}

TEST(AudioMusicWorld, SelectOutdoorWinterVariants) {
    // Winter pool: 0->AufDenStrassen, 1->InkalterNovembernacht, 2->ImmerKalt.
    MockSink sink;
    auto d = makeDirector(&sink);
    crt::Srand(1); // first variant = 2 -> ImmerKalt
    audio::SelectOutdoorSeasonTrack(d, audio::kWinter);
    CHECK(d.table[0].name == "cd1\\ImmerKalt.mp3");
}

TEST(AudioMusicWorld, SelectOutdoorNoEntry) {
    MockSink sink;
    audio::MusicDirector d;
    d.sink = &sink;
    CHECK_EQ(audio::SelectOutdoorSeasonTrack(d, audio::kSpring), -1);
}

// --- Track-update state transitions -----------------------------------------
TEST(AudioMusicWorld, UpdateInitialSeasonLatch) {
    MockSink sink;
    auto d = makeDirector(&sink);
    // First tick latches season, no track action.
    audio::PlaybackTick tick{audio::kSpring, false, 0};
    CHECK(audio::UpdateOutdoorTrackPlayback(d, tick) == audio::PlaybackAction::kNone);
    CHECK_EQ(d.lastSeason, audio::kSpring);
}

TEST(AudioMusicWorld, UpdateStartsOutdoorWhenIdle) {
    MockSink sink;
    auto d = makeDirector(&sink);
    crt::Srand(1);
    d.lastSeason = audio::kSpring;          // already latched
    audio::PlaybackTick tick{audio::kSpring, false, 0}; // no location -> outdoor
    auto a = audio::UpdateOutdoorTrackPlayback(d, tick);
    CHECK(a == audio::PlaybackAction::kSelectOutdoor);
    CHECK(d.currentTrackHandle != 0);
    CHECK(d.table[0].active);
}

TEST(AudioMusicWorld, UpdateStartsLocationWhenIdleAndInside) {
    MockSink sink;
    auto d = makeDirector(&sink);
    d.lastSeason = audio::kSpring;
    audio::PlaybackTick tick{audio::kSpring, false, 11}; // location id 11 -> entry 1
    auto a = audio::UpdateOutdoorTrackPlayback(d, tick);
    CHECK(a == audio::PlaybackAction::kResumeLocation);
    CHECK(d.table[1].active);
    CHECK(sink.lastLoaded == "msx\\tavern.mp3");
}

TEST(AudioMusicWorld, UpdateTrackEndedEntersPause) {
    MockSink sink;
    auto d = makeDirector(&sink);
    d.lastSeason = audio::kSpring;
    d.currentTrackHandle = 5000;
    d.table[0].active = true;
    audio::PlaybackTick tick{audio::kSpring, /*atStreamEnd=*/true, 0};
    auto a = audio::UpdateOutdoorTrackPlayback(d, tick);
    CHECK(a == audio::PlaybackAction::kTrackEnded);
    CHECK_EQ(d.currentTrackHandle, 0);
    CHECK(!d.table[0].active);
    CHECK(d.lastTrackName == d.table[0].name); // remembered for repeat-avoid
}

TEST(AudioMusicWorld, UpdateSeasonChangeStopsOutdoor) {
    MockSink sink;
    auto d = makeDirector(&sink);
    d.lastSeason = audio::kSummer;   // was summer
    d.currentTrackHandle = 7000;
    d.table[0].active = true;        // outdoor active
    d.table[0].name = "cd1\\ZurSommerzeit.mp3";
    audio::PlaybackTick tick{audio::kAutumn, false, 0}; // season changed
    auto a = audio::UpdateOutdoorTrackPlayback(d, tick);
    CHECK(a == audio::PlaybackAction::kSeasonStop);
    CHECK_EQ(d.currentTrackHandle, 0);
    CHECK_EQ(d.lastSeason, audio::kAutumn);
    CHECK(!d.table[0].active);
    CHECK_EQ(sink.stopCount, 1);
}

// --- Wildlife / ambient selection by time & season --------------------------
TEST(AudioAmbient, WildlifeCooldownGate) {
    audio::WildlifeCategory cat;
    for (int s = 0; s < 4; ++s) {
        cat.baseDelay[s] = 100;
        cat.threshold[s] = 1.0f; // gate always > 1.0 when cooldown elapsed
        cat.lastTrigger[s] = 0;
    }
    crt::Srand(1);
    // tick0 at now=200: jitter=838 -> delay=938; now-last=200 <= 938 -> no fire,
    // but the cooldown compare fails so the gate RandNext is NOT drawn.
    CHECK(!audio::WildlifeShouldTrigger(cat, 0, 200, 0, 1000));
    // now huge so cooldown elapses: jitter=113 -> delay=213; 5000-0 > 213 -> fire
    // (threshold 1.0 + positive gate > 1.0).
    CHECK(audio::WildlifeShouldTrigger(cat, 0, 5000, 0, 1000));
    CHECK_EQ(cat.lastTrigger[0], 5000);
}

TEST(AudioAmbient, WildlifeSuppressedAboveCap) {
    audio::WildlifeCategory cat;
    cat.baseDelay[1] = 0;
    cat.threshold[1] = 2.0f;
    cat.lastTrigger[1] = 0;
    crt::Srand(1);
    // modifier 400 >= suppressAbove 350 -> never fires even though cooldown ok.
    CHECK(!audio::WildlifeShouldTrigger(cat, 1, 100000, 400, 350));
}

TEST(AudioAmbient, WildlifeResetTimers) {
    audio::WildlifeCategory cat;
    audio::WildlifeResetTimers(cat, 1234);
    for (int s = 0; s < 4; ++s)
        CHECK_EQ(cat.lastTrigger[s], 1234);
}

TEST(AudioAmbient, MarketLoopStateGuard) {
    audio::MarketLoop m;
    CHECK(audio::MarketStartLoop(m, 42));   // starts
    CHECK_EQ(m.handle, 42);
    CHECK(!audio::MarketStartLoop(m, 99));  // already running -> no-op
    CHECK_EQ(m.handle, 42);
    CHECK(audio::MarketStopLoop(m));        // stops
    CHECK_EQ(m.handle, 0);
    CHECK(!audio::MarketStopLoop(m));       // nothing to stop
    CHECK(!audio::MarketStartLoop(m, 0));   // null handle -> no start
}

TEST(AudioAmbient, PopulationVolumeClamp) {
    CHECK_EQ(audio::AmbientPopulationVolume(30.0f), 30); // <= 64 -> kept
    CHECK_EQ(audio::AmbientPopulationVolume(64.0f), 64); // == clamp -> kept
    CHECK_EQ(audio::AmbientPopulationVolume(200.0f), 64);// > clamp -> floor 64
}

// --- Snow flake seeding (golden, seeded RNG) --------------------------------
TEST(RenderSnow, SeedFlakesGolden) {
    render::SnowFlake buf[3];
    render::SnowSystem sys;
    sys.flakes = buf;
    sys.count = 3;
    sys.capacity = 3;
    crt::Srand(1);
    render::SnowSeedFlakes(sys);
    CHECK(feq(buf[0].px, 0.02774132415652275f));
    CHECK(feq(buf[0].py, -0.6485488414764404f));
    CHECK(feq(buf[0].pz, -0.382732629776001f));
    CHECK(feq(buf[0].size, 1.017265796661377f));
    CHECK(feq(buf[0].d0, 0.034476302564144135f));
    CHECK(feq(buf[0].d1, 0.026717277243733406f));
    CHECK(feq(buf[2].px, -0.4455397129058838f));
    CHECK(feq(buf[2].size, 1.0176931619644165f));
}

// --- Snow flake update advances position & keeps it bounded -----------------
TEST(RenderSnow, UpdateAdvancesAndWraps) {
    render::SnowFlake buf[4];
    render::SnowSystem sys;
    sys.flakes = buf;
    sys.count = 4;
    sys.capacity = 4;
    crt::Srand(1);
    render::SnowSeedFlakes(sys);

    // Identity camera basis, anchor==eye (no drift/anchor offset), centered vp.
    render::SnowCamera cam{};
    cam.eye[0] = cam.eye[1] = cam.eye[2] = 0.0f;
    cam.anchor[0] = cam.anchor[1] = cam.anchor[2] = 0.0f;
    float idm[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(cam.m, idm, sizeof(idm));
    render::SnowViewport vp{0, 0, 640, 480};

    // Snapshot, advance, and verify positions stay wrapped into [-1,1).
    float pyBefore[4];
    for (int i = 0; i < 4; ++i) pyBefore[i] = buf[i].py;
    for (int step = 0; step < 20; ++step)
        render::SnowUpdateFlake(sys, 1.0f, cam, vp);
    for (int i = 0; i < 4; ++i) {
        CHECK(buf[i].px >= -1.0f && buf[i].px < 1.0f);
        CHECK(buf[i].py >= -1.0f && buf[i].py < 1.0f);
        CHECK(buf[i].pz >= -1.0f && buf[i].pz < 1.0f);
        // screen projection finite and roughly within an expanded viewport box.
        CHECK(std::isfinite(buf[i].sx) && std::isfinite(buf[i].sy));
    }
    // Gravity is downward (-Y of world through identity basis): py should move.
    bool moved = false;
    for (int i = 0; i < 4; ++i)
        if (!feq(buf[i].py, pyBefore[i])) moved = true;
    CHECK(moved);
}

// --- Snow accumulation masked copy + mip level ------------------------------
TEST(RenderSnow, AccumulateMaskedCopy) {
    // 2x2 tile: copy src->dst only where mask < threshold(=128).
    u8 dst[4]  = {0, 0, 0, 0};
    u8 src[4]  = {10, 20, 30, 40};
    u8 mask[4] = {0, 200, 100, 255};
    render::SnowAccumulateMaskedCopy8(dst, src, mask, 2, 128);
    CHECK_EQ(dst[0], 10); // mask 0   < 128 -> copy
    CHECK_EQ(dst[1], 0);  // mask 200 >=128 -> keep
    CHECK_EQ(dst[2], 30); // mask 100 < 128 -> copy
    CHECK_EQ(dst[3], 0);  // mask 255 >=128 -> keep

    u16 d16[4] = {0,0,0,0}; u16 s16[4] = {1,2,3,4};
    render::SnowAccumulateMaskedCopy16(d16, s16, mask, 2, 128);
    CHECK_EQ(d16[0], 1); CHECK_EQ(d16[1], 0); CHECK_EQ(d16[2], 3); CHECK_EQ(d16[3], 0);

    CHECK_EQ(render::SnowMipLevel(256, 1), 8); // log2(256)
    CHECK_EQ(render::SnowMipLevel(256, 4), 6); // log2(64)
    CHECK_EQ(render::SnowMipLevel(1, 1), 0);
}

// --- Rain drop seeding + update ---------------------------------------------
TEST(RenderRain, SeedDropsGolden) {
    render::RainDrop buf[3];
    render::RainSystem sys;
    sys.drops = buf;
    sys.count = 3;
    sys.capacity = 3;
    crt::Srand(1);
    render::RainSeedDrops(sys);
    CHECK(feq(buf[0].px, 0.02774132415652275f));
    CHECK(feq(buf[0].py, -0.6485488414764404f));
    CHECK(feq(buf[0].size, 0.38363292813301086f));
    CHECK(feq(buf[0].d0, 0.29738152027130127f));
    CHECK(feq(buf[2].px, -0.4455397129058838f));
}

TEST(RenderRain, UpdateAdvancesAndWraps) {
    render::RainDrop buf[4];
    render::RainSystem sys;
    sys.drops = buf;
    sys.count = 4;
    sys.capacity = 4;
    crt::Srand(1);
    render::RainSeedDrops(sys);

    render::SnowCamera cam{};
    float idm[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::memcpy(cam.m, idm, sizeof(idm));
    render::SnowViewport vp{0, 0, 800, 600};

    for (int step = 0; step < 15; ++step)
        render::RainUpdateDrop(sys, 1.0f, cam, vp);
    for (int i = 0; i < 4; ++i) {
        CHECK(buf[i].px >= -1.0f && buf[i].px < 1.0f);
        CHECK(buf[i].py >= -1.0f && buf[i].py < 1.0f);
        CHECK(buf[i].pz >= -1.0f && buf[i].pz < 1.0f);
        CHECK(std::isfinite(buf[i].sx) && std::isfinite(buf[i].sx2));
    }
}

// --- Weather intensity + cloud-layer selection ------------------------------
TEST(RenderWeather, IntensityPeakOfThree) {
    i32 arc[24];
    for (int i = 0; i < 24; ++i) arc[i] = 0;
    arc[5] = 40; arc[6] = 100; arc[7] = 30;
    CHECK_EQ(render::WeatherIntensity(arc, 6), 100); // max(40,100,30)
    CHECK_EQ(render::WeatherIntensity(arc, 5), 100); // max(arc4,arc5,arc6)=100
    CHECK_EQ(render::WeatherIntensity(arc, 0), 0);
    // wrap: hour 0 looks at 23,0,1
    arc[23] = 70;
    CHECK_EQ(render::WeatherIntensity(arc, 0), 70);
}

TEST(RenderWeather, Category) {
    CHECK(render::CategoryFor(10) == render::kWeatherFair);
    CHECK(render::CategoryFor(50) == render::kWeatherMedium);
    CHECK(render::CategoryFor(149) == render::kWeatherMedium);
    CHECK(render::CategoryFor(150) == render::kWeatherHeavy);
}

TEST(RenderWeather, CloudVariantCounts) {
    CHECK_EQ(render::CloudVariantCount(render::kWeatherFair), 4);
    CHECK_EQ(render::CloudVariantCount(render::kWeatherMedium), 3);
    CHECK_EQ(render::CloudVariantCount(render::kWeatherHeavy), 2);
}

TEST(RenderWeather, CloudLayerSelectRerollsOnMatch) {
    // Force a deterministic roll then confirm the advance-on-match behavior.
    crt::Srand(1);
    int first = render::SelectCloudLayerIndex(render::kWeatherFair, -1); // no current
    CHECK(first >= 0 && first < 4);
    // If we pass current == the value that will be rolled, it must advance.
    crt::Srand(1);
    int roll = guild::util::RandomModulo(4); // peek the same roll
    crt::Srand(1);
    int sel = render::SelectCloudLayerIndex(render::kWeatherFair, roll);
    CHECK_EQ(sel, roll + 1);
}

TEST(RenderWeather, GrowAmounts) {
    // trunc(2.0 * -windX * intensity); windX negative -> positive grow.
    CHECK_EQ(render::SnowGrowAmount(-1.0f, 100), 200);
    CHECK_EQ(render::RainGrowAmount(-1.0f, 100), 50);
    CHECK_EQ(render::SnowGrowAmount(0.0f, 100), 0);
}
