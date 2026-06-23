// Golden tests for the rule-6 video backing that binds Movie_PlayOutro's
// MovieDllHooks to a shim::IVideoDecoder (guild::play::MakeVideoMovieHooks).
// Uses a fake decoder so the full prepare->play->dispose pump is exercised
// headlessly (no pl_mpeg / no display needed).
#include "tests/framework/test.h"

#include "play/video_movie_backing.h"
#include "play/cutscene_recon2_movie.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::play;

namespace {

// A deterministic fake decoder: yields `total` synthetic RGB frames then EOF.
class FakeDecoder final : public shim::IVideoDecoder {
public:
    explicit FakeDecoder(int total) : total_(total) {}
    bool open(const char* path) override { opened_ = path ? path : ""; pos_ = 0; return openOk_; }
    void close() override { ++closes_; }
    int    width()  const override { return 4; }
    int    height() const override { return 2; }
    double framerate() const override { return 25.0; }
    double duration()  const override { return total_ / 25.0; }
    bool   hasAudio()  const override { return audio_; }
    int    audioRate() const override { return 44100; }
    int    audioChannels() const override { return 2; }
    bool decodeVideo(shim::VideoFrame& out) override {
        if (pos_ >= total_) return false;
        rgb_.assign(4 * 2 * 3, static_cast<std::uint8_t>(pos_)); // frame index as pixel value
        out.width = 4; out.height = 2; out.time = pos_ / 25.0; out.rgb = rgb_.data();
        ++pos_;
        return true;
    }
    int decodeAudio(float* dst, int maxSamples) override {
        if (!audio_ || maxSamples <= 0) return 0;
        int n = maxSamples < 8 ? maxSamples : 8;
        for (int i = 0; i < n; ++i) dst[i] = 0.5f;
        return n;
    }
    bool eof() const override { return pos_ >= total_; }

    std::string opened_;
    bool openOk_ = true;
    bool audio_  = false;
    int  closes_ = 0;
private:
    int total_, pos_ = 0;
    std::vector<std::uint8_t> rgb_;
};

// Minimal control hooks: fade reports done immediately so the pre-roll loop exits.
MovieControlHooks MakeCtl() {
    MovieControlHooks ctl;
    ctl.fadeStatusByte = []() -> u8 { return 0x4; };  // done bit set
    ctl.fadeTimer      = []() -> float { return 0.0f; }; // 0.0 <= timer -> exit
    return ctl;
}

} // namespace

// Full pipe: Movie_PlayOutro -> prepare(open) -> play(pump all frames) -> the
// frameSink receives every frame in order; the decoder is opened on the outro path
// and closed at the end.
TEST(VideoMovieBacking, PlaysAllFramesInOrder) {
    FakeDecoder dec(5);
    VideoMovieBacking backing;
    backing.decoder = &dec;
    backing.movieDir = "\\project\\movie\\";

    std::vector<int> seen;             // first-pixel value of each presented frame
    backing.frameSink = [&](const shim::VideoFrame& f) { seen.push_back(f.rgb[0]); };
    int slept = 0;
    backing.sleep = [&](double) { ++slept; };

    MovieDllHooks dll = MakeVideoMovieHooks(backing);
    MovieControlHooks ctl = MakeCtl();
    MovieGlobals g;

    u8 ret = Movie_PlayOutro(dll, ctl, g, backing.movieDir, 640, 480, /*dllAvailable*/ true);

    CHECK_EQ(dec.opened_, std::string("\\project\\movie\\outro.mpg"));
    CHECK_EQ((int)seen.size(), 5);
    for (int i = 0; i < 5; ++i) CHECK_EQ(seen[i], i);   // frames in order 0..4
    CHECK_EQ(slept, 5);                                  // paced once per frame
    CHECK(dec.closes_ >= 1);                             // disposed/closed
    CHECK_EQ((int)ret, 1);                               // saved moviesEnabled (default 1)
    CHECK_EQ((int)g.moviesEnabled, 1);                   // restored after the clip
}

// Open failure (handle 0) -> play() is a silent no-op, no frames presented.
TEST(VideoMovieBacking, OpenFailureSkipsSilently) {
    FakeDecoder dec(3);
    dec.openOk_ = false;
    VideoMovieBacking backing;
    backing.decoder = &dec;
    backing.movieDir = "\\m\\";
    int frames = 0;
    backing.frameSink = [&](const shim::VideoFrame&) { ++frames; };

    MovieDllHooks dll = MakeVideoMovieHooks(backing);
    MovieControlHooks ctl = MakeCtl();
    MovieGlobals g;
    Movie_PlayOutro(dll, ctl, g, backing.movieDir, 640, 480, true);

    CHECK_EQ(frames, 0);
}

// No decoder at all (portable build path) -> inert hooks, silent skip, globals
// preserved.
TEST(VideoMovieBacking, NoDecoderInertSkip) {
    VideoMovieBacking backing;            // decoder == nullptr
    backing.movieDir = "\\m\\";
    int frames = 0;
    backing.frameSink = [&](const shim::VideoFrame&) { ++frames; };

    MovieDllHooks dll = MakeVideoMovieHooks(backing);
    MovieControlHooks ctl = MakeCtl();
    MovieGlobals g;
    u8 ret = Movie_PlayOutro(dll, ctl, g, backing.movieDir, 320, 240, true);

    CHECK_EQ(frames, 0);
    CHECK_EQ((int)ret, 1);
    CHECK_EQ((int)g.moviesEnabled, 1);
}

// Abort callback stops playback early.
TEST(VideoMovieBacking, AbortStopsEarly) {
    FakeDecoder dec(10);
    VideoMovieBacking backing;
    backing.decoder = &dec;
    backing.movieDir = "\\m\\";
    int frames = 0;
    backing.frameSink = [&](const shim::VideoFrame&) { ++frames; };
    backing.shouldAbort = [&]() { return frames >= 3; };  // abort after 3

    MovieDllHooks dll = MakeVideoMovieHooks(backing);
    MovieControlHooks ctl = MakeCtl();
    MovieGlobals g;
    Movie_PlayOutro(dll, ctl, g, backing.movieDir, 640, 480, true);

    CHECK_EQ(frames, 3);
}

// Audio path: when the decoder reports audio, audioSink receives PCM per frame.
TEST(VideoMovieBacking, AudioDeliveredWhenPresent) {
    FakeDecoder dec(2);
    dec.audio_ = true;
    VideoMovieBacking backing;
    backing.decoder = &dec;
    backing.movieDir = "\\m\\";
    backing.frameSink = [&](const shim::VideoFrame&) {};
    int audioBlocks = 0, audioSamples = 0;
    backing.audioSink = [&](const float*, int n) { ++audioBlocks; audioSamples += n; };

    MovieDllHooks dll = MakeVideoMovieHooks(backing);
    MovieControlHooks ctl = MakeCtl();
    MovieGlobals g;
    Movie_PlayOutro(dll, ctl, g, backing.movieDir, 640, 480, true);

    CHECK_EQ(audioBlocks, 2);     // one block per frame
    CHECK_EQ(audioSamples, 16);   // 8 samples * 2 frames
}
