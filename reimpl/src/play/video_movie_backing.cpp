// guild::play — pl_mpeg/IVideo backing for Movie_PlayOutro. See header.
#include "play/video_movie_backing.h"

#include <vector>

namespace guild::play {

MovieDllHooks MakeVideoMovieHooks(VideoMovieBacking& backing) {
    MovieDllHooks h;
    VideoMovieBacking* b = &backing;

    // mov_Init_ / mov_Exit_ — no decoder-level work; lifecycle is prepare/dispose.
    h.init = []() {};
    h.exit = [b]() { if (b->decoder) b->decoder->close(); };

    // mov_Prepare_(hInstance, eventArg) -> playback handle. Opens the clip.
    h.prepare = [b](void* /*hInstance*/, int /*eventArg*/) -> int {
        if (!b->decoder) return 0;
        const std::string path = b->movieDir + b->clipName;
        return b->decoder->open(path.c_str()) ? 1 : 0;
    };

    h.prepareDD  = []() {};
    h.setVisible = [](int) {};
    h.stop       = [b](int) { if (b->decoder) b->decoder->close(); };
    h.dispose    = [b]() { if (b->decoder) b->decoder->close(); };

    // mov_Play_(handle) — the blocking playback pump. Decodes the whole clip,
    // delivering each video frame to frameSink (with audio to audioSink), paced by
    // sleep(). A handle of 0 (open failed / no decoder) is a silent no-op.
    h.play = [b](int handle) {
        if (handle == 0 || !b->decoder) return;
        shim::IVideoDecoder* dec = b->decoder;
        const double fps = dec->framerate();
        const double frameDelay = (fps > 0.0) ? (1.0 / fps) : 0.0;
        std::vector<float> audio(static_cast<std::size_t>(b->audioChunk > 0 ? b->audioChunk : 4096));

        shim::VideoFrame frame;
        while (dec->decodeVideo(frame)) {
            if (b->frameSink) b->frameSink(frame);
            if (dec->hasAudio() && b->audioSink) {
                int n = dec->decodeAudio(audio.data(), static_cast<int>(audio.size()));
                if (n > 0) b->audioSink(audio.data(), n);
            }
            if (b->sleep && frameDelay > 0.0) b->sleep(frameDelay);
            if (b->shouldAbort && b->shouldAbort()) break;
        }
        dec->close();
    };

    // mov_GetEvent_ — not driven by Movie_PlayOutro (play() blocks), but the
    // proprietary player polled it for completion; report "ended" (0).
    h.getEvent = []() -> int { return 0; };

    return h;
}

} // namespace guild::play
