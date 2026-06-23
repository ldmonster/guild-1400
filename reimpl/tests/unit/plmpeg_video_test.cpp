// Contract tests for the IVideo decoder factory (guild::shim::CreateVideoDecoder).
// Backend-aware via the propagated GUILD_HAVE_PLMPEG macro:
//   * portable/headless build -> factory returns nullptr (no third-party dep);
//   * pl_mpeg backend build    -> factory returns a usable decoder; a bogus path
//     fails gracefully; and (if a real outro.mpg asset is present) a few frames
//     decode with sane dimensions.
#include "tests/framework/test.h"

#include "shim/IVideo.h"

#include <cstdlib>
#include <string>

using namespace guild;

TEST(PlmpegVideo, FactoryContract) {
    shim::IVideoDecoder* d = shim::CreateVideoDecoder();
#ifdef GUILD_HAVE_PLMPEG
    CHECK(d != nullptr);                       // backend present
    CHECK(!d->open("/no/such/file.mpg"));      // graceful failure, no crash
    CHECK_EQ(d->width(), 0);                   // nothing opened
    delete d;
#else
    CHECK(d == nullptr);                       // headless: no video backend
#endif
}

#ifdef GUILD_HAVE_PLMPEG
// Optional: decode a few frames from the real outro clip when the game assets are
// available (GUILD_GAME_DIR). Skips cleanly otherwise, per the asset-test policy.
TEST(PlmpegVideo, DecodesRealOutroIfPresent) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir) { CHECK(true); return; }         // no assets -> skip
    // Try the known on-disk layouts (the extracted assets use "movie/Outro.mpg";
    // the in-engine VFS path is "\\project\\movie\\outro.mpg"). Filesystem casing
    // varies, so probe a few candidates.
    const char* candidates[] = {
        "/movie/Outro.mpg", "/movie/outro.mpg",
        "/project/movie/outro.mpg", "/project/movie/Outro.mpg",
        "/movie/Intro.mpg",
    };
    shim::IVideoDecoder* d = shim::CreateVideoDecoder();
    CHECK(d != nullptr);
    bool opened = false;
    for (const char* c : candidates) {
        if (d->open((std::string(dir) + c).c_str())) { opened = true; break; }
    }
    if (!opened) { delete d; CHECK(true); return; } // clip absent -> skip
    CHECK(d->width()  > 0);
    CHECK(d->height() > 0);
    shim::VideoFrame f;
    int decoded = 0;
    for (int i = 0; i < 3 && d->decodeVideo(f); ++i) {
        CHECK(f.rgb != nullptr);
        CHECK_EQ(f.width, d->width());
        CHECK_EQ(f.height, d->height());
        ++decoded;
    }
    CHECK(decoded >= 1);
    delete d;
}
#endif
