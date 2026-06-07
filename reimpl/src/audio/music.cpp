#include "audio/music.h"

namespace guild::audio {

MusicTrack* MusicPlayer::findActiveTrack(const std::string& name) {
    // VIBE_Audio_FindActiveTrackSlot @0x43a864: StrCmp over the slot table,
    // bounded to 10 slots in the original.
    for (auto& t : tracks_) {
        if (t.active && t.name == name)
            return &t;
    }
    return nullptr;
}

MusicTrack* MusicPlayer::loadTrack(const std::string& name, const void* pcm,
                                   std::size_t bytes, int sampleRate, bool loop) {
    // VIBE_Audio_LoadTrack @0x439ed0: reuse an active slot of the same name,
    // else take a free slot and copy the name in.
    MusicTrack* t = findActiveTrack(name);
    if (!t) {
        for (auto& slot : tracks_) {
            if (!slot.active) {
                t = &slot;
                break;
            }
        }
        if (!t) {
            tracks_.push_back(MusicTrack{});
            t = &tracks_.back();
        }
        *t = MusicTrack{};
        t->name = name;
    }

    // VIBE_Audio_StartTrack @0x439f8c (device-facing core): if not already
    // active, open the stream, seed volume (=127), start, then mark active.
    if (t->active)
        return t; // already playing this track

    t->pcm = pcm;
    t->pcmBytes = bytes;
    t->sampleRate = sampleRate;
    t->loop = loop;
    t->volume = kDefaultTrackVolume; // a1+0x118 = 127

    // VIBE_Audio_OpenStream + StartStream — model the MSS stream with a device
    // voice. loop branch: v26 ? loops=0 : loops=1 (a non-zero arg => play once).
    t->stream = device_->allocVoice();
    if (t->stream < 0) {
        t->active = false;
        return nullptr; // StartTrack returns -1 / LoadTrack returns 0
    }
    // StartTrack @0x439f8c: loop-count branch v26 ? 0 : 1. A looping track uses
    // loop-count 0 (infinite in MSS); a one-shot uses 1.
    int loops = loop ? 0 : 1;
    device_->setVolume(t->stream, 0); // SetStreamVolume(...,0) before start
    device_->playSample(t->stream, pcm, bytes, sampleRate, loops);
    device_->setVolume(t->stream, t->volume); // SetStreamVolume(..., +0x118)
    t->active = true; // a1+0x104 = 1
    return t;
}

void MusicPlayer::stopTrack(MusicTrack* t, bool fade) {
    // VIBE_Audio_StopTrack @0x43a2fc.
    if (!t || !t->active) // !*(a1+0x104) => nothing to do
        return;
    if (fade) {
        // VIBE_Audio_FadeOutTrack(a1, 2): a timed fade. Modeled as an immediate
        // mute here (the device has no streaming fade primitive).
        device_->setVolume(t->stream, 0);
        return;
    }
    device_->stop(t->stream);     // PauseStream + CloseStream
    device_->freeVoice(t->stream);
    t->active = false;            // a1+0x104 = 0
    t->stream = -1;               // a1+0x100 = 0
}

void MusicPlayer::setMusicVolume(int vol) {
    // VIBE_Audio_SetMusicVolume @0x439e90 — digital master (music device) volume.
    device_->setMasterVolume(vol);
    masterVolume_ = vol; // dword_62DA10 (kept for parity)
}

void MusicPlayer::setFadeVolume(float modifier, int ms) {
    // VIBE_Audio_SetFadeVolume @0x43a7b8: guard 0.0 <= a1 <= 1.0.
    (void)ms; // timed fades modeled as immediate
    if (!(modifier >= 0.0f) || modifier > 1.0f)
        return;
    fadeModifier_ = modifier; // flt_62D9F8 = a1 (immediate path, a2==0)
    applyMasterVolume(masterVolume_); // VIBE_Audio_ApplyMasterVolume(dword_62DA0C)
}

void MusicPlayer::applyMasterVolume(int masterVolume) {
    // VIBE_Audio_ApplyMasterVolume @0x439ddc: clamp modifier to [0,1], then push
    // master*modifier to the device master volume (truncating to int).
    if (fadeModifier_ < 0.0f)
        fadeModifier_ = 0.0f;
    else if (fadeModifier_ > 1.0f)
        fadeModifier_ = 1.0f;
    double scaled = static_cast<double>(masterVolume) * fadeModifier_;
    int out = static_cast<int>(scaled); // VIBE_Coord_ConvertX truncation
    device_->setMasterVolume(out);
    masterVolume_ = masterVolume; // dword_62DA0C = v2
}

} // namespace guild::audio
