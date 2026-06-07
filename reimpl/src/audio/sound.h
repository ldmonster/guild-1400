#pragma once
// guild::audio — top-level sound facade of gilde.exe (the sb_/d3snd/f3snd glue).
//
// Owns the voice pool, the 3D positional pool, the speech queue, the sample bank
// and the music player, and exposes the high-level entry points the game calls.
//
// Recovered from:
//   VIBE_Sound_LibInit      @0x445d90  (init: 48 voices / 2ch / 44100)
//   VIBE_Sound_PlaySample   @0x4461d0  (alloc voice + bind bank sample + defaults)
//   VIBE_Sound_FindSampleInBank @0x446288
//   VIBE_Sound3d_PlayOnEntity   @0x42469c
#include "guild/common/types.h"
#include "shim/IAudioDevice.h"
#include "audio/voice.h"
#include "audio/voicequeue.h"
#include "audio/sound3d.h"
#include "audio/samplebank.h"
#include "audio/music.h"
#include <string>

namespace guild::audio {

// Engine defaults baked into VIBE_Sound_LibInit @0x445d90.
constexpr int kDefaultVoices     = 48;
constexpr int kDefaultChannels   = 2;
constexpr int kDefaultSampleRate = 44100;

// Default 3D pool capacity (VIBE_Sound3d_InitPool default dword_62D358).
constexpr int kDefault3dVoices = 16;

class SoundSystem {
public:
    explicit SoundSystem(shim::IAudioDevice* device)
        : device_(device), voices_(device), queue_(&voices_),
          pool3d_(&voices_, kDefault3dVoices), music_(device) {}

    // VIBE_Sound_LibInit @0x445d90 — init the digital output and the voice pool.
    bool init(int voices = kDefaultVoices, int channels = kDefaultChannels,
              int sampleRate = kDefaultSampleRate);

    // VIBE_Sound_Shutdown @0x439ccc.
    void shutdown();

    // VIBE_Sound_PlaySample @0x4461d0 — allocate a voice, resolve the named
    // sample from the bank, seed defaults (vol=127 @+0x24, pan=63 @+0x28,
    // loopCount=1 @+0x0C) and (re)apply them. Returns the voice slot, or null on
    // exhaustion / unknown sample. Does NOT start playback (the original only
    // primes the slot; StartSample happens later via the mixer/queue).
    VoiceSlot* playSample(const std::string& name);

    // Convenience: play a positioned one-shot through the 3D pool. Mirrors
    // VIBE_Sound3d_PlayOnEntity @0x42469c: resolve the sample, attach a 3D entry,
    // then run one update so the voice gets its initial volume/pan.
    Sound3dEntry* playPositioned(const std::string& name, const Vec3& pos,
                                 int baseVol, float radius,
                                 const Vec3& listener, const Vec3& listenerForward,
                                 bool oneShot = true);

    VoicePool&   voices()  { return voices_; }
    VoiceQueue&  queue()   { return queue_; }
    Sound3dPool& pool3d()  { return pool3d_; }
    SampleBank&  bank()    { return bank_; }
    MusicPlayer& music()   { return music_; }

private:
    shim::IAudioDevice* device_;
    VoicePool   voices_;
    VoiceQueue  queue_;
    Sound3dPool pool3d_;
    SampleBank  bank_;
    MusicPlayer music_;
};

} // namespace guild::audio
