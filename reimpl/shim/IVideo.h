#pragma once
// Host video-decode boundary. The original used the proprietary "moveahead.dll"
// MPEG player (mov_* exports) for cutscene/outro playback. Per the user's rule-6
// decision (2026-06-10) that codec is replaced by pl_mpeg (an MIT MPEG-1 / MP2 /
// MPEG-PS decoder), wired behind this interface. guild::play::Movie_PlayOutro's
// MovieDllHooks bind to an IVideoDecoder via the video_movie_backing bridge.
//
// The decoder is pure (no GPU/audio): it yields tightly-packed RGB frames and
// interleaved PCM. Presentation (upload+blit) is the caller's job via
// IGraphicsDevice; audio playback via IAudioDevice. This keeps decode headless-
// testable and the platform backends swappable.
#include <cstdint>
#include <cstddef>

namespace guild::shim {

// One decoded video frame. `rgb` points at width*height*3 bytes (R,G,B, row-major,
// no padding), owned by the decoder and valid until the next decodeVideo() call.
struct VideoFrame {
    int width  = 0;
    int height = 0;
    double time = 0.0;          // presentation timestamp (seconds)
    const std::uint8_t* rgb = nullptr;
};

class IVideoDecoder {
public:
    virtual ~IVideoDecoder() = default;

    // Open a container file (e.g. an MPEG-1 .mpg). Returns false on failure.
    virtual bool open(const char* path) = 0;
    virtual void close() = 0;

    virtual int    width()      const = 0;   // 0 until a stream is known
    virtual int    height()     const = 0;
    virtual double framerate()  const = 0;   // frames/sec (0 if unknown)
    virtual double duration()   const = 0;   // seconds (0 if unknown)
    virtual bool   hasAudio()   const = 0;
    virtual int    audioRate()  const = 0;   // sample rate (Hz)
    virtual int    audioChannels() const = 0;

    // Decode the next video frame into `out` (decoder-owned RGB). Returns false at
    // end-of-stream (or error). Advances the video clock.
    virtual bool decodeVideo(VideoFrame& out) = 0;

    // Decode up to maxSamples interleaved float samples into `out`; returns the
    // number actually written (0 at audio EOF / when no audio). Non-blocking.
    virtual int decodeAudio(float* out, int maxSamples) = 0;

    virtual bool eof() const = 0;
};

// Create the compiled-in video decoder, or nullptr when no video backend is built
// (the portable/headless build). Caller owns the returned object (delete to free).
IVideoDecoder* CreateVideoDecoder();

} // namespace guild::shim
