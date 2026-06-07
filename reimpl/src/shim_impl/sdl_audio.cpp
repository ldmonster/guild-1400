// OPTIONAL SDL2 audio backend — compiled to nothing unless GUILD_HAVE_SDL2 is set.
// See sdl_audio.h for the enable/build instructions and the assumed PCM format.
#ifdef GUILD_HAVE_SDL2

#include "shim_impl/sdl_audio.h"

#include <algorithm>
#include <cstring>

namespace guild::shim {

namespace {

// Clamp an int sample to the signed-16 range.
inline std::int16_t clamp16(int v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return static_cast<std::int16_t>(v);
}

} // namespace

// ----------------------------- pure mixer core -----------------------------

void SdlAudioDevice::MixInto(MixVoice* voices, int voiceCount, int master,
                             std::int16_t* out, std::size_t frames, int channels) {
    // Start from silence.
    std::memset(out, 0, frames * static_cast<std::size_t>(channels) * sizeof(std::int16_t));
    if (master < 0) master = 0;
    if (master > 127) master = 127;

    for (int vi = 0; vi < voiceCount; ++vi) {
        MixVoice& v = voices[vi];
        if (!v.active || !v.data || v.frames == 0)
            continue;

        int vol = v.volume;
        if (vol < 0) vol = 0;
        if (vol > 127) vol = 127;

        // Per-channel pan gain. 64 = center; mono ignores pan entirely.
        // Linear constant-sum pan: left = (127-pan)/127, right = pan/127, each
        // doubled so center (pan=64) is ~unity for both channels.
        int panL = 127, panR = 127;
        if (channels == 2) {
            int p = v.pan;
            if (p < 0) p = 0;
            if (p > 127) p = 127;
            // Scale so center maps to 127 on both channels.
            panL = (127 - p) * 2;
            panR = p * 2;
            if (panL > 127) panL = 127;
            if (panR > 127) panR = 127;
        }

        for (std::size_t f = 0; f < frames; ++f) {
            if (v.pos >= v.frames) {
                if (v.loop) {
                    v.pos = 0;
                } else {
                    v.active = false;
                    break;
                }
            }
            const std::int16_t* src = v.data + v.pos * static_cast<std::size_t>(channels);
            std::int16_t* dst = out + f * static_cast<std::size_t>(channels);
            for (int c = 0; c < channels; ++c) {
                int pan = (channels == 2) ? (c == 0 ? panL : panR) : 127;
                // sample * vol/127 * master/127 * pan/127
                long s = static_cast<long>(src[c]);
                s = s * vol / 127;
                s = s * master / 127;
                s = s * pan / 127;
                dst[c] = clamp16(static_cast<int>(dst[c]) + static_cast<int>(s));
            }
            ++v.pos;
            if (v.pos >= v.frames) {
                if (v.loop)
                    v.pos = 0;
                else {
                    v.active = false;
                    break;
                }
            }
        }
    }
}

// ----------------------------- resample -------------------------------------

std::vector<std::int16_t> SdlAudioDevice::Resample(const std::int16_t* in,
                                                   std::size_t inFrames, int channels,
                                                   int inRate, int outRate) {
    std::vector<std::int16_t> out;
    if (!in || inFrames == 0 || channels <= 0 || inRate <= 0 || outRate <= 0)
        return out;
    if (inRate == outRate) {
        out.assign(in, in + inFrames * static_cast<std::size_t>(channels));
        return out;
    }
    // outFrames = round(inFrames * outRate / inRate)
    std::size_t outFrames = static_cast<std::size_t>(
        (static_cast<unsigned long long>(inFrames) * static_cast<unsigned long long>(outRate)
         + inRate / 2) / inRate);
    if (outFrames == 0)
        outFrames = 1;
    out.resize(outFrames * static_cast<std::size_t>(channels));

    const double step = static_cast<double>(inRate) / static_cast<double>(outRate);
    for (std::size_t of = 0; of < outFrames; ++of) {
        double srcPos = of * step;
        std::size_t i0 = static_cast<std::size_t>(srcPos);
        double frac = srcPos - static_cast<double>(i0);
        std::size_t i1 = i0 + 1;
        if (i1 >= inFrames) i1 = inFrames - 1;
        for (int c = 0; c < channels; ++c) {
            double a = in[i0 * channels + c];
            double b = in[i1 * channels + c];
            double v = a + (b - a) * frac;
            out[of * channels + c] = clamp16(static_cast<int>(v >= 0 ? v + 0.5 : v - 0.5));
        }
    }
    return out;
}

// ----------------------------- locking --------------------------------------

void SdlAudioDevice::lock() {
    if (dev_)
        SDL_LockAudioDevice(dev_);
}

void SdlAudioDevice::unlock() {
    if (dev_)
        SDL_UnlockAudioDevice(dev_);
}

// ----------------------------- snapshot/commit ------------------------------

int SdlAudioDevice::snapshot(std::vector<MixVoice>& out) {
    out.clear();
    out.reserve(voices_.size());
    for (auto& v : voices_) {
        MixVoice mv;
        mv.data = v.pcm.empty() ? nullptr : v.pcm.data();
        mv.frames = channels_ > 0 ? v.pcm.size() / static_cast<std::size_t>(channels_) : 0;
        mv.pos = v.pos;
        mv.volume = v.volume;
        mv.pan = v.pan;
        mv.loop = v.loop;
        mv.active = v.active;
        out.push_back(mv);
    }
    return static_cast<int>(out.size());
}

void SdlAudioDevice::commit(const std::vector<MixVoice>& snap) {
    const std::size_t n = std::min(snap.size(), voices_.size());
    for (std::size_t i = 0; i < n; ++i) {
        voices_[i].pos = snap[i].pos;
        voices_[i].active = snap[i].active;
    }
}

// ----------------------------- callback -------------------------------------

void SDLCALL SdlAudioDevice::audioCallback(void* userdata, Uint8* stream, int len) {
    auto* self = static_cast<SdlAudioDevice*>(userdata);
    std::int16_t* out = reinterpret_cast<std::int16_t*>(stream);
    const int ch = self->channels_ > 0 ? self->channels_ : 1;
    std::size_t frames = static_cast<std::size_t>(len) / (sizeof(std::int16_t) * static_cast<std::size_t>(ch));

    // The callback runs on SDL's audio thread; SDL holds the device lock for the
    // duration of this call, so accesses to voices_ here are already serialized
    // against our lock()/unlock() public mutators.
    std::vector<MixVoice> snap;
    self->snapshot(snap);
    MixInto(snap.data(), static_cast<int>(snap.size()), self->master_, out, frames, ch);
    self->commit(snap);
}

// ----------------------------- IAudioDevice ---------------------------------

bool SdlAudioDevice::init(int voices, int channels, int sampleRate) {
    if (voices <= 0 || (channels != 1 && channels != 2) || sampleRate <= 0)
        return false;
    if (inited_)
        shutdown();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;

    SDL_AudioSpec want, have;
    SDL_zero(want);
    SDL_zero(have);
    want.freq = sampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = static_cast<Uint8>(channels);
    want.samples = 1024;
    want.callback = &SdlAudioDevice::audioCallback;
    want.userdata = this;

    // Disallow format/channel/rate changes so the callback always sees S16SYS at
    // exactly the requested rate/channels (we do our own resampling on input).
    dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (dev_ == 0) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    rate_ = have.freq;
    channels_ = have.channels;
    master_ = 127;
    voices_.assign(static_cast<std::size_t>(voices), Voice{});
    inited_ = true;

    SDL_PauseAudioDevice(dev_, 0); // start playback
    return true;
}

void SdlAudioDevice::shutdown() {
    if (dev_) {
        SDL_PauseAudioDevice(dev_, 1);
        SDL_CloseAudioDevice(dev_);
        dev_ = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    voices_.clear();
    inited_ = false;
    rate_ = 0;
    channels_ = 0;
}

VoiceHandle SdlAudioDevice::allocVoice() {
    if (!inited_)
        return -1;
    lock();
    VoiceHandle h = -1;
    for (std::size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].allocated) {
            voices_[i] = Voice{};
            voices_[i].allocated = true;
            h = static_cast<VoiceHandle>(i);
            break;
        }
    }
    unlock();
    return h;
}

void SdlAudioDevice::freeVoice(VoiceHandle v) {
    if (v < 0 || static_cast<std::size_t>(v) >= voices_.size())
        return;
    lock();
    voices_[static_cast<std::size_t>(v)] = Voice{};
    unlock();
}

void SdlAudioDevice::playSample(VoiceHandle v, const void* pcm, std::size_t bytes,
                                int sampleRate, int loops) {
    if (v < 0 || static_cast<std::size_t>(v) >= voices_.size() || !pcm || bytes == 0)
        return;
    const std::int16_t* in = static_cast<const std::int16_t*>(pcm);
    std::size_t samples = bytes / sizeof(std::int16_t);
    std::size_t inFrames = samples / static_cast<std::size_t>(channels_);
    if (inFrames == 0)
        return;

    // Resample to the device rate if needed (input assumed S16, device channels).
    std::vector<std::int16_t> resampled;
    if (sampleRate > 0 && sampleRate != rate_)
        resampled = Resample(in, inFrames, channels_, sampleRate, rate_);
    else
        resampled.assign(in, in + inFrames * static_cast<std::size_t>(channels_));

    lock();
    Voice& voice = voices_[static_cast<std::size_t>(v)];
    if (!voice.allocated)
        voice.allocated = true;
    voice.pcm = std::move(resampled);
    voice.pos = 0;
    // loops: 0 = play once, -1 (or any negative / a loop flag) = repeat forever.
    voice.loop = (loops != 0);
    voice.active = true;
    unlock();
}

void SdlAudioDevice::stop(VoiceHandle v) {
    if (v < 0 || static_cast<std::size_t>(v) >= voices_.size())
        return;
    lock();
    voices_[static_cast<std::size_t>(v)].active = false;
    voices_[static_cast<std::size_t>(v)].pos = 0;
    unlock();
}

void SdlAudioDevice::setVolume(VoiceHandle v, int vol) {
    if (v < 0 || static_cast<std::size_t>(v) >= voices_.size())
        return;
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    lock();
    voices_[static_cast<std::size_t>(v)].volume = vol;
    unlock();
}

void SdlAudioDevice::setPan(VoiceHandle v, int pan) {
    if (v < 0 || static_cast<std::size_t>(v) >= voices_.size())
        return;
    if (pan < 0) pan = 0;
    if (pan > 127) pan = 127;
    lock();
    voices_[static_cast<std::size_t>(v)].pan = pan;
    unlock();
}

void SdlAudioDevice::setMasterVolume(int vol) {
    if (vol < 0) vol = 0;
    if (vol > 127) vol = 127;
    lock();
    master_ = vol;
    unlock();
}

int SdlAudioDevice::activeVoices() {
    int n = 0;
    lock();
    for (auto& v : voices_)
        if (v.active) ++n;
    unlock();
    return n;
}

std::vector<std::int16_t> SdlAudioDevice::pullMix(std::size_t frames) {
    const int ch = channels_ > 0 ? channels_ : 1;
    std::vector<std::int16_t> out(frames * static_cast<std::size_t>(ch), 0);
    if (frames == 0)
        return out;
    lock();
    std::vector<MixVoice> snap;
    snapshot(snap);
    MixInto(snap.data(), static_cast<int>(snap.size()), master_, out.data(), frames, ch);
    commit(snap);
    unlock();
    return out;
}

} // namespace guild::shim

#endif // GUILD_HAVE_SDL2
