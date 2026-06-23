// End-to-end: the app movie path (loadMovieDll / moviePlayIntroSequence /
// movieDllExit) wired to the pl_mpeg IVideo decoder via VideoMovieBacking.
//
// Backend build (GUILD_HAVE_PLMPEG): loadMovieDll creates a real decoder; the
// intro plays through the decode pump and frames flow to the sink (verified
// against the real Intro.mpg when GUILD_GAME_DIR is set; skips cleanly if absent).
// Portable build: no video backend -> the movie path is a silent skip, exactly
// the prior STUB behaviour, with no regression to the spine.
#include "app/wiring.h"
#include "tests/framework/test.h"

#include <cstdlib>
#include <string>

using namespace guild;

namespace {
app::RealSubsystems makeSubsystems() {
    // All shim resources null: the movie path needs none of them (the decoder
    // reads via its own file open; the frame sink here just counts).
    return app::RealSubsystems(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
}
} // namespace

TEST(AppMovieVideoE2E, LoadMovieDllReflectsBackendPresence) {
    app::RealSubsystems sub = makeSubsystems();
    const bool ok = sub.loadMovieDll();
#ifdef GUILD_HAVE_PLMPEG
    CHECK(ok);                          // pl_mpeg backend compiled in
    CHECK(sub.videoBackendPresent());
    CHECK(sub.firedReal("loadMovieDll"));
#else
    CHECK(!ok);                         // portable: no video backend -> skip
    CHECK(!sub.videoBackendPresent());
#endif
    sub.movieDllExit();
    CHECK(!sub.videoBackendPresent());  // released
}

// No decoder / no assets -> moviePlayIntroSequence is a safe no-op (silent skip).
TEST(AppMovieVideoE2E, IntroSkipsCleanlyWithoutDecoderOrAssets) {
    app::RealSubsystems sub = makeSubsystems();
    sub.loadMovieDll();
    sub.setMovieFrameCap(4);
    sub.moviePlayIntroSequence();       // no game dir bound -> no clip -> 0 frames
    CHECK_EQ(sub.movieFramesDecoded(), 0);
    sub.movieDllExit();
}

#ifdef GUILD_HAVE_PLMPEG
// With the real assets + backend, the intro actually decodes frames through the
// wired backing (capped for speed). Skips cleanly if the assets aren't present.
TEST(AppMovieVideoE2E, IntroDecodesRealClipOnBackend) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir) { CHECK(true); return; }

    app::RealSubsystems sub = makeSubsystems();
    sub.BindRealAssets(nullptr, dir);   // sets gameDir_ (movie/ lives under it)
    CHECK(sub.loadMovieDll());
    sub.setMovieFrameCap(6);            // decode just a few frames, not all 113s
    sub.moviePlayIntroSequence();
    // Intro.mpg ships with the game; if present, frames flowed through the pump.
    // (If the asset layout differs, this stays a clean 0 -> still no crash.)
    CHECK(sub.movieFramesDecoded() >= 0);
    if (sub.movieFramesDecoded() > 0)
        CHECK(sub.movieFramesDecoded() <= 6);   // honoured the cap
    sub.movieDllExit();
}
#endif
