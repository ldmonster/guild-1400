#pragma once
// guild::play — rule-6 video backing for VIBE_Movie_PlayOutro.
//
// The original outro playback went through moveahead.dll's mov_* exports
// (MovieDllHooks). Per the user's rule-6 decision the proprietary codec is
// replaced by pl_mpeg via the shim::IVideoDecoder interface. This bridge binds an
// IVideoDecoder to the MovieDllHooks the reconstructed Movie_PlayOutro drives:
//   * prepare(hInstance,eventArg) -> open "<movieDir>outro.mpg", return a handle;
//   * play(handle) -> the blocking playback pump: decode each video frame to the
//     injected frameSink and each audio block to audioSink, pacing with sleep();
//   * dispose()/stop()/exit() -> close the decoder.
//
// Presentation and audio output are injected callbacks so the bridge is headless-
// testable; the real host wires frameSink -> IGraphicsDevice (upload+blit) and
// audioSink -> IAudioDevice, and sleep -> the platform clock. If no decoder is
// available (CreateVideoDecoder() == nullptr in the portable build) the hooks are
// inert no-ops and Movie_PlayOutro degrades to a silent skip — exactly the prior
// "movie=STUB" behavior.
#include "play/cutscene_recon2_movie.h"
#include "shim/IVideo.h"

#include <functional>
#include <string>

namespace guild::play {

struct VideoMovieBacking {
    // Decoder to drive (not owned; caller manages lifetime). May be nullptr ->
    // the produced hooks are inert (silent skip).
    shim::IVideoDecoder* decoder = nullptr;
    std::string movieDir;                       // aProjectMovie ("\\project\\movie\\")
    std::string clipName = "outro.mpg";

    // Injected output sinks (default no-ops => headless / silent).
    std::function<void(const shim::VideoFrame&)> frameSink;   // present one RGB frame
    std::function<void(const float*, int count)> audioSink;   // queue interleaved PCM
    std::function<void(double seconds)>          sleep;       // frame pacing
    // Optional per-frame stop check (return true to abort playback early, e.g. a
    // key press). Default: never abort.
    std::function<bool()>                        shouldAbort;

    // Audio scratch size (interleaved stereo floats decoded per video frame step).
    int audioChunk = 4096;
};

// Produce a MovieDllHooks table bound to `backing`. `dllAvailable` for
// Movie_PlayOutro should be passed true when backing.decoder != nullptr.
MovieDllHooks MakeVideoMovieHooks(VideoMovieBacking& backing);

} // namespace guild::play
