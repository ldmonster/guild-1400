#include "audio/sound.h"

namespace guild::audio {

bool SoundSystem::init(int voices, int channels, int sampleRate) {
    // VIBE_Sound_LibInit @0x445d90.
    return voices_.init(voices, channels, sampleRate);
}

void SoundSystem::shutdown() {
    // VIBE_Sound_Shutdown @0x439ccc.
    pool3d_.stopAll();
    queue_.flushAll();
    voices_.shutdown();
}

VoiceSlot* SoundSystem::playSample(const std::string& name) {
    // VIBE_Sound_PlaySample @0x4461d0.
    if (!voices_.initialized())
        return nullptr;
    VoiceSlot* v = voices_.allocVoiceChannel(); // VIBE_Sound_AllocVoiceChannel
    if (!v)
        return nullptr;
    // VIBE_Sound_FindSampleInBank @0x446288.
    SampleRecord* s = bank_.findSampleByName(name);
    v->sample = s;          // v9[1] = sample (+0x04)
    if (!s) {
        // Original: returns (v9 ^ v7) i.e. a non-handle sentinel; the slot stays
        // allocated but unbound. We surface that as the slot with sample==null.
        return v;
    }
    v->field2C = 0;         // v9[11] = 0 (+0x2C)
    v->loopCount = 1;       // v9[3]  = 1 (+0x0C)
    v->volume = 127;        // v9[9]  = 127 (+0x24)  (carries the volume value)
    v->pan = 63;            // v9[10] = 63  (+0x28)  (carries the pan value)
    // VIBE_Audio_SetSampleVolume(*v9) / SetSamplePan(*v9) / SetSampleLoopCount(*v9):
    // each pushes the value held in the slot. SetSampleVolume -> AIL_set_sample_volume
    // pushes +0x24 (=127); SetSamplePan -> AIL_set_sample_pan pushes +0x28 (=63).
    // (The AIL_set_sample_* calls are the Miles boundary; routed through the SDL shim.)
    device_->setVolume(v->handle, v->volume);  // +0x24 (=127)  -> AIL_set_sample_volume
    device_->setPan(v->handle, v->pan);        // +0x28 (=63)   -> AIL_set_sample_pan
    // SetSampleLoopCount pushes +0x0C (=1); the shim carries loop count via playSample/
    // startVoice, and PlaySample only primes the slot (no StartSample here), so there is
    // no separate device loop-count call at this site.
    return v;
}

Sound3dEntry* SoundSystem::playPositioned(const std::string& name, const Vec3& pos,
                                          int baseVol, float radius,
                                          const Vec3& listener,
                                          const Vec3& listenerForward,
                                          bool oneShot) {
    // VIBE_Sound3d_PlayOnEntity @0x42469c -> AttachToEntity -> first update.
    SampleRecord* s = bank_.findSampleByName(name);
    const void* pcm = nullptr;
    std::size_t bytes = 0;
    int rate = kDefaultSampleRate;
    if (s) {
        pcm = s->pcm.empty() ? reinterpret_cast<const void*>(s) : s->pcm.data();
        bytes = s->pcm.size();
        rate = s->sampleRate;
    }
    Sound3dEntry* e = pool3d_.attach(pos, baseVol, radius, pcm, bytes, rate, oneShot);
    if (!e)
        return nullptr;
    pool3d_.updateAll(listener, listenerForward);
    return e;
}

} // namespace guild::audio
