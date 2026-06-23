// pl_mpeg-backed IVideoDecoder (rule-6: replaces moveahead.dll's MPEG codec).
//
// Compiled into the guild library unconditionally (CMake globs src/**), but the
// pl_mpeg-backed body only lights up when GUILD_HAVE_PLMPEG is defined (the
// GUILD_BACKEND presets add third_party/ to the include path and define the macro
// + PL_MPEG_IMPLEMENTATION here). In the portable/headless build CreateVideoDecoder
// returns nullptr so nothing depends on a third-party lib.
#include "shim/IVideo.h"

#ifdef GUILD_HAVE_PLMPEG

// pl_mpeg.h uses FILE/malloc/memcpy but does not include the C stdlib headers
// itself — pull them in first.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

#include <vector>

namespace guild::shim {
namespace {

class PlmpegVideoDecoder final : public IVideoDecoder {
public:
    ~PlmpegVideoDecoder() override { close(); }

    bool open(const char* path) override {
        close();
        plm_ = plm_create_with_filename(path);
        if (!plm_) return false;
        // Decode video; keep audio only if a stream exists.
        plm_set_video_enabled(plm_, TRUE);
        const bool audio = plm_get_num_audio_streams(plm_) > 0;
        plm_set_audio_enabled(plm_, audio ? TRUE : FALSE);
        if (audio) plm_set_audio_stream(plm_, 0);
        w_  = plm_get_width(plm_);
        h_  = plm_get_height(plm_);
        rgb_.assign(static_cast<std::size_t>(w_) * h_ * 3, 0);
        return true;
    }

    void close() override {
        if (plm_) { plm_destroy(plm_); plm_ = nullptr; }
        w_ = h_ = 0;
        ended_ = false;
        rgb_.clear();
    }

    int    width()         const override { return w_; }
    int    height()        const override { return h_; }
    double framerate()     const override { return plm_ ? plm_get_framerate(plm_) : 0.0; }
    double duration()      const override { return plm_ ? plm_get_duration(plm_)  : 0.0; }
    bool   hasAudio()      const override { return plm_ && plm_get_num_audio_streams(plm_) > 0; }
    int    audioRate()     const override { return plm_ ? plm_get_samplerate(plm_) : 0; }
    int    audioChannels() const override { return 2; } // pl_mpeg outputs interleaved stereo

    bool decodeVideo(VideoFrame& out) override {
        if (!plm_) return false;
        plm_frame_t* fr = plm_decode_video(plm_);
        if (!fr) { ended_ = true; return false; }
        if (rgb_.size() < static_cast<std::size_t>(w_) * h_ * 3)
            rgb_.assign(static_cast<std::size_t>(w_) * h_ * 3, 0);
        plm_frame_to_rgb(fr, rgb_.data(), w_ * 3);
        out.width  = w_;
        out.height = h_;
        out.time   = fr->time;
        out.rgb    = rgb_.data();
        return true;
    }

    int decodeAudio(float* dst, int maxSamples) override {
        if (!plm_ || !dst || maxSamples <= 0) return 0;
        plm_samples_t* s = plm_decode_audio(plm_);
        if (!s) return 0;
        // pl_mpeg yields interleaved stereo floats; s->count is per-channel frames.
        int floats = static_cast<int>(s->count) * 2;
        if (floats > maxSamples) floats = maxSamples;
        for (int i = 0; i < floats; ++i) dst[i] = s->interleaved[i];
        return floats;
    }

    bool eof() const override { return ended_ || (plm_ && plm_has_ended(plm_)); }

private:
    plm_t* plm_ = nullptr;
    int w_ = 0, h_ = 0;
    bool ended_ = false;
    std::vector<std::uint8_t> rgb_;
};

} // namespace

IVideoDecoder* CreateVideoDecoder() { return new PlmpegVideoDecoder(); }

} // namespace guild::shim

#else // !GUILD_HAVE_PLMPEG — portable/headless build: no video backend.

namespace guild::shim {
IVideoDecoder* CreateVideoDecoder() { return nullptr; }
} // namespace guild::shim

#endif
