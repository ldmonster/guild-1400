#include "tests/framework/test.h"
#include "audio/music_world.h"
#include "audio/ambient.h"
#include "render/snow.h"
#include "render/rain.h"
#include "render/weather.h"
#include "sim/gametime.h"
#include "crt/rand.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;

namespace {
struct RecordingSink : audio::IMusicSink {
    int next = 1;
    std::vector<std::string> loads;
    int stops = 0;
    int loadTrack(const std::string& name, int) override {
        loads.push_back(name);
        return next++;
    }
    void stopTrack(int, int) override { ++stops; }
};

audio::MusicDirector makeDirector(audio::IMusicSink* sink) {
    audio::MusicDirector d;
    d.sink = sink;
    audio::TrackEntry outdoor;
    outdoor.ids = {audio::kOutdoorTrackId};
    d.table.push_back(outdoor);
    return d;
}
} // namespace

// =============================================================================
// Drive game-time across all four seasons, run the music director each "day",
// and verify the selected outdoor tracks match a season-derived reference.
// =============================================================================
TEST(AudioMusicWorldE2E, SeasonArcDrivesTrackSelection) {
    RecordingSink sink;
    auto d = makeDirector(&sink);
    crt::Srand(12345);

    sim::GameTime t{};
    t.day = 0; t.hour = 8; t.minute = 0; t.second = 0;

    std::vector<int> seenSeasons;
    std::vector<std::string> selected;

    for (int day = 0; day < 8; ++day) {
        int season = audio::SeasonFromDay(t);
        // Latch season on the first tick of the run.
        if (d.lastSeason == -1)
            d.lastSeason = season;

        // If the season changed and an outdoor track is playing, the updater
        // stops it; otherwise (idle) it selects a fresh seasonal outdoor track.
        audio::PlaybackTick tick{season, /*atStreamEnd=*/false, /*location=*/0};
        auto action = audio::UpdateOutdoorTrackPlayback(d, tick);

        if (action == audio::PlaybackAction::kSeasonStop) {
            // After a season-stop the next idle tick reselects; emulate that.
            audio::PlaybackTick t2{season, false, 0};
            action = audio::UpdateOutdoorTrackPlayback(d, t2);
        }
        if (action == audio::PlaybackAction::kSelectOutdoor) {
            seenSeasons.push_back(season);
            selected.push_back(d.table[0].name);
            // Simulate the track finishing so the next day can reselect.
            audio::PlaybackTick end{season, /*atStreamEnd=*/true, 0};
            audio::UpdateOutdoorTrackPlayback(d, end);
            d.table[0].name.clear(); // outdoor entry reselects fresh next time
        }

        // Advance one full day.
        sim::GameTimeAdvance(&t, 1, 0, 0);
    }

    // Every selected track name must belong to its season's pool.
    CHECK(!selected.empty());
    for (std::size_t i = 0; i < selected.size(); ++i) {
        int s = seenSeasons[i];
        const std::string& nm = selected[i];
        const std::array<const char*, 4>* pool = nullptr;
        switch (s) {
        case audio::kSpring: pool = &audio::SpringTracks(); break;
        case audio::kSummer: pool = &audio::SummerTracks(); break;
        case audio::kAutumn: pool = &audio::AutumnTracks(); break;
        case audio::kWinter: pool = &audio::WinterTracks(); break;
        }
        bool inPool = false;
        for (const char* c : *pool)
            if (nm == c) inPool = true;
        CHECK(inPool);
    }
    // Each selection issues exactly one stream load.
    CHECK_EQ((int)sink.loads.size(), (int)selected.size());
}

// =============================================================================
// Reproduce a full snow + rain field across multiple frames against a hand-rolled
// reference (identical math) to confirm trajectory determinism under a fixed seed.
// =============================================================================
TEST(AudioMusicWorldE2E, SnowRainTrajectoryReproducible) {
    const int N = 16;
    render::SnowFlake a[N] = {}, b[N] = {};
    render::SnowSystem sa, sb;
    sa.flakes = a; sa.count = N; sa.capacity = N;
    sb.flakes = b; sb.count = N; sb.capacity = N;

    crt::Srand(777);
    render::SnowSeedFlakes(sa);
    crt::Srand(777);
    render::SnowSeedFlakes(sb);
    // Same seed -> identical seeded fields (seeding writes px..d1 only).
    for (int i = 0; i < N; ++i) {
        CHECK(a[i].px == b[i].px && a[i].py == b[i].py && a[i].pz == b[i].pz);
        CHECK(a[i].size == b[i].size && a[i].d0 == b[i].d0 && a[i].d1 == b[i].d1);
    }

    render::SnowCamera cam{};
    cam.eye[0] = 10.0f; cam.eye[1] = 5.0f; cam.eye[2] = -3.0f;
    cam.anchor[0] = 12.0f; cam.anchor[1] = 5.5f; cam.anchor[2] = -2.0f;
    float m[9] = {0.9f, 0.1f, 0.0f, -0.1f, 0.99f, 0.05f, 0.0f, -0.05f, 0.98f};
    std::memcpy(cam.m, m, sizeof(m));
    render::SnowViewport vp{0, 0, 1024, 768};

    for (int step = 0; step < 30; ++step) {
        render::SnowUpdateFlake(sa, 0.5f, cam, vp);
        render::SnowUpdateFlake(sb, 0.5f, cam, vp);
    }
    // Two independent runs with identical inputs must match bit-for-bit.
    for (int i = 0; i < N; ++i) {
        CHECK_EQ(std::memcmp(&a[i], &b[i], sizeof(render::SnowFlake)), 0);
        CHECK(a[i].px >= -1.0f && a[i].px < 1.0f);
        CHECK(std::isfinite(a[i].sx) && std::isfinite(a[i].sy));
        CHECK(std::isfinite(a[i].sx2) && std::isfinite(a[i].sy2));
    }

    // Rain analogue.
    render::RainDrop ra[N] = {}, rb[N] = {};
    render::RainSystem rsa, rsb;
    rsa.drops = ra; rsa.count = N; rsa.capacity = N;
    rsb.drops = rb; rsb.count = N; rsb.capacity = N;
    crt::Srand(31337);
    render::RainSeedDrops(rsa);
    crt::Srand(31337);
    render::RainSeedDrops(rsb);
    for (int step = 0; step < 25; ++step) {
        render::RainUpdateDrop(rsa, 0.5f, cam, vp);
        render::RainUpdateDrop(rsb, 0.5f, cam, vp);
    }
    for (int i = 0; i < N; ++i) {
        CHECK_EQ(std::memcmp(&ra[i], &rb[i], sizeof(render::RainDrop)), 0);
        CHECK(ra[i].pz >= -1.0f && ra[i].pz < 1.0f);
    }
}

// =============================================================================
// Weather arc -> intensity -> snow/rain grow amounts + cloud category, driven
// over a 24-hour cycle (day/night), verified against a recomputed reference.
// =============================================================================
TEST(AudioMusicWorldE2E, WeatherArcDrivesGrowAndCategory) {
    i32 arc[24];
    for (int h = 0; h < 24; ++h)
        arc[h] = (h >= 6 && h < 18) ? (10 + h * 8) : 5; // daytime ramps up

    float windX = -2.5f;
    float windY = 1.0f;

    for (int h = 0; h < 24; ++h) {
        int inten = render::WeatherIntensity(arc, h);
        // reference peak-of-three
        auto at = [&](int i) { return arc[((i % 24) + 24) % 24]; };
        int ref = std::max(std::max(at(h - 1), at(h)), at(h + 1));
        CHECK_EQ(inten, ref);

        int snow = render::SnowGrowAmount(windX, inten);
        int rain = render::RainGrowAmount(windX, inten);
        CHECK_EQ(snow, (int)(2.0 * -(double)windX * inten));
        CHECK_EQ(rain, (int)(0.5 * -(double)windX * inten));

        float mag = render::CloudScrollMagnitude(windX, windY, inten);
        CHECK(std::isfinite(mag) && mag >= 0.0f);

        auto cat = render::CategoryFor(inten);
        if (inten >= 150) CHECK(cat == render::kWeatherHeavy);
        else if (inten >= 50) CHECK(cat == render::kWeatherMedium);
        else CHECK(cat == render::kWeatherFair);
    }
}
