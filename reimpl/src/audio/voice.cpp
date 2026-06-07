#include "audio/voice.h"

namespace guild::audio {

bool VoicePool::init(int voices, int channels, int sampleRate) {
    // VIBE_Sound_LibInit @0x445d90: VIBE_Audio_OpenDigitalOutput then alloc
    // 48*voices bytes (one VoiceSlot each) and bind a sample handle per slot.
    (void)channels;
    if (initialized_)
        return false; // original: dword_62E8FC already set => return -1
    if (!device_->init(voices, channels, sampleRate))
        return false;
    slots_.assign(static_cast<std::size_t>(voices > 0 ? voices : 0), VoiceSlot{});
    bool ok = true;
    for (auto& s : slots_) {
        // VIBE_Audio_AllocateSampleHandle @0x449e70 — one device voice per slot.
        s.handle = device_->allocVoice();
        if (s.handle < 0)
            ok = false; // original records -1 but keeps going
    }
    initialized_ = true;
    return ok;
}

void VoicePool::shutdown() {
    // VIBE_Sound_Shutdown @0x439ccc — release the per-slot device voices.
    for (auto& s : slots_) {
        if (s.handle >= 0)
            device_->freeVoice(s.handle);
        s = VoiceSlot{};
    }
    slots_.clear();
    initialized_ = false;
}

VoiceSlot* VoicePool::allocVoiceChannel() {
    // VIBE_Sound_AllocVoiceChannel @0x446730.
    if (!initialized_ || slots_.empty())
        return nullptr;

    const int n = voiceCount();

    // Pass 1: first slot that is free (not playing and not looping).
    int i = 0;
    for (; i < n; ++i) {
        VoiceSlot& s = slots_[i];
        bool playing = voiceIsPlaying(&s);
        bool looping = (s.flags & kVoiceFlag_Looping) != 0;
        if (!playing && !looping)
            break;
    }
    if (i >= n)
        return nullptr; // every voice busy => 0 (invalid)

    VoiceSlot* chosen = &slots_[i];

    // Pass 2: among the remaining slots, steal the lowest-priority (+0x08) one
    // that is itself free. Original: if (chosen->priority > cand->priority ...)
    // chosen = cand. So the smallest priority value wins.
    for (++i; i < n; ++i) {
        VoiceSlot& cand = slots_[i];
        bool playing = voiceIsPlaying(&cand);
        bool looping = (cand.flags & kVoiceFlag_Looping) != 0;
        if (chosen->priority > cand.priority && !playing && !looping)
            chosen = &cand;
    }

    // VIBE_Audio_InitSample(); slot.flags = 0;
    chosen->flags = 0;
    return chosen;
}

void VoicePool::setLoopFlag(VoiceSlot* v, bool looping) {
    if (!v)
        return;
    if (looping)
        v->flags |= kVoiceFlag_Looping;
    else
        v->flags &= static_cast<u8>(~kVoiceFlag_Looping);
}

void VoicePool::setVoiceVolume(VoiceSlot* v, int vol) {
    // VIBE_Audio_SetVoiceVolume @0x44754c: gate (unsigned)vol < 0x80.
    if (!initialized_ || !v)
        return;
    if (static_cast<unsigned>(vol) >= 0x80u)
        return; // out of 0..127 — ignored, matches a2 < 0x80 test
    if (vol == v->pan) // original writes offset +0x28 (the 'pan' field byte slot)
        return;
    v->pan = vol;
    device_->setVolume(v->handle, vol);
}

void VoicePool::setVoicePan(VoiceSlot* v, int pan) {
    // VIBE_Audio_SetVoicePan @0x447574: gate (unsigned)pan < 0x80.
    if (!initialized_ || !v)
        return;
    if (static_cast<unsigned>(pan) >= 0x80u)
        return;
    if (pan == v->volume) // original writes offset +0x24 (the 'volume' field byte slot)
        return;
    v->volume = pan;
    device_->setPan(v->handle, pan);
}

bool VoicePool::voiceIsPlaying(const VoiceSlot* v) const {
    // VIBE_Audio_VoiceIsPlaying @0x4471b0: a1 && *a1 && status==4.
    if (!v || v->handle < 0 || !status_)
        return false;
    return status_->sampleStatus(v->handle) == kSampleStatusPlaying;
}

void VoicePool::stopVoice(VoiceSlot* v, bool fade) {
    // VIBE_Audio_StopVoice @0x447508.
    if (!initialized_ || !v)
        return;
    if (fade) {
        v->flags |= kVoiceFlag_FadeStop;
    } else {
        device_->stop(v->handle);
        v->flags |= kVoiceFlag_Released;
    }
}

void VoicePool::startVoice(VoiceSlot* v, const void* pcm, std::size_t bytes,
                           int sampleRate, int loops) {
    // Device-facing half of VIBE_Audio_PlayVoiceSample @0x44737c.
    if (!initialized_ || !v)
        return;
    device_->setVolume(v->handle, v->pan);   // +0x28 holds the volume value
    device_->setPan(v->handle, v->volume);   // +0x24 holds the pan value
    v->flags &= static_cast<u8>(~kVoiceFlag_Released); // &= ~8 before StartSample
    device_->playSample(v->handle, pcm, bytes, sampleRate, loops);
}

} // namespace guild::audio
