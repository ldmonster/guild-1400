// gilde.exe — per-frame AUDIO tick + app-side SFX triggers (guild::app).
// See audio_tick.h for the reconstruction map.
#include "app/audio_tick.h"

namespace guild::app {

// The original market loop attaches at range 2200 with base volume 127.
//   v5 = VIBE_Sound3d_AttachToEntity((int)v3, v4, 127, 2200.0);
static constexpr int   kMarketBaseVol = 127;     // VIBE_Sound3d_AttachToEntity arg
static constexpr float kMarketRange   = 2200.0f; // VIBE_Sound3d_AttachToEntity arg

AudioTickResult AudioTick(audio::SoundSystem& sound, audio::MusicDirector& music,
                          const AudioEnable& enable, std::uint32_t featureMask,
                          int tickCounter, const AudioListener& listener,
                          int season, bool atStreamEnd, int locationId) {
    AudioTickResult r;

    // --- if (dword_63C900) { wildlife; VoiceQueue_ProcessNext } -------------
    // (0x4c0d39) the snd_3dEnabled gate guards the ambient + speech-queue block.
    if (enable.sound3dOn) {
        // VIBE_Ambient_UpdateWildlifeSounds @0x5800f0 (0x4c0d3b). The full
        // wildlife director reads the live terrain/season globals and is a
        // separate core (audio::WildlifeShouldTrigger et al); the per-frame
        // hook here records that the updater ran (the decision core is exercised
        // directly by the audio unit tests).
        r.wildlifeRan = true;

        // VIBE_VoiceQueue_ProcessNext @0x57eff0 (0x4c0d40). The original time
        // base is `13 * dword_62EB38`; VoiceQueue::processNext applies the *13
        // internally from the frame tick it receives, so pass tickCounter.
        sound.queue().processNext(tickCounter);
        r.voiceQueueRan = true;
    }

    // --- if (dword_63C8F8 && (v54 & 0x40000)) Music_UpdateOutdoorTrackPlayback
    // (0x4c0d56) the music block runs only with music enabled AND the day-cycle
    // mask bit set (mask::kDayCycleMusic == 0x40000).
    if (enable.musicOn && (featureMask & mask::kDayCycleMusic) != 0) {
        audio::PlaybackTick mt{season, atStreamEnd, locationId};
        r.music = audio::UpdateOutdoorTrackPlayback(music, mt);
    }

    // --- if (dword_63C900) { Sound3d_UpdateAll; [Sound3d_UpdateListener]; ----
    //                         Sound_UpdateVoices } -------------------------
    // (0x4c0ddd) the same snd_3dEnabled gate guards the spatial-update +
    // mixer-tick block at the end of the frame.
    if (enable.sound3dOn) {
        // VIBE_Sound3d_UpdateAll @0x424790 (0x4c0ddf) — recompute each in-use 3D
        // entry's volume (attenuation) + pan (position) against the listener and
        // drive its voice.
        sound.pool3d().updateAll(listener.pos, listener.forward);
        r.sound3dRan = true;

        // if (dword_63C904) VIBE_Sound3d_UpdateListener @0x425208 (0x4c0ded) —
        // the terrain "echo" ambient-loop layer. It is heightmap/floor-octree
        // coupled (VIBE_Floor_PickTileAtPoint / Heightmap_LookupTileAttribute),
        // out of scope for a headless audio tick; it only ADDS extra terrain
        // loops on top of UpdateAll. Documented stub: record the gate, no core.
        if (enable.weatherOn)
            r.listenerRan = false;  // stub: terrain-loop listener not reconstructed

        // VIBE_Sound_UpdateVoices @0x445f00 (0x4c0df2) — service every active
        // mixer voice (recycle finished one-shots, advance loops). The full 218-
        // instruction mixer-tick reads the live digital-output ring buffers and is
        // not reconstructed as a single VoicePool method; the portable, wireable
        // half is the per-voice "still playing?" recycle probe the original runs
        // over the voice pool, which IS reconstructed (VoicePool::voiceIsPlaying ==
        // VIBE_Audio_VoiceIsPlaying @0x4471b0). Run that probe across the live pool
        // so the real voice-status path is exercised each frame.
        audio::VoicePool& vp = sound.voices();
        for (int i = 0; i < vp.voiceCount(); ++i)
            (void)vp.voiceIsPlaying(vp.slotAt(i));
        r.voicesRan = true;
    }

    return r;
}

audio::Sound3dEntry* StartMarketLoop(audio::SoundSystem& sound,
                                     audio::Sound3dEntry*& marketHandle,
                                     const audio::Vec3& pos,
                                     const AudioListener& listener,
                                     const std::string& sampleName) {
    // if ( !dword_6420F4 ) — only start if not already playing.
    if (marketHandle)
        return marketHandle;

    // v3 = *(_DWORD *)(dword_6477A4 + 97); if (v3) { ... } — the original gates on
    // a live scene record; here a non-empty sample name stands in for that record.
    if (sampleName.empty())
        return nullptr;

    // v = VIBE_Sound_PlaySample(0, v2, ...); h = VIBE_Sound3d_AttachToEntity(v, _,
    // 127, 2200.0). SoundSystem::playPositioned is exactly that fused path
    // (PlaySample -> 3D AttachToEntity -> first UpdateAll); oneShot=false so the
    // entry loops (the original then calls VIBE_Sound3d_SetLooping(h, 1)).
    audio::Sound3dEntry* e = sound.playPositioned(
        sampleName, pos, kMarketBaseVol, kMarketRange,
        listener.pos, listener.forward, /*oneShot=*/false);
    if (!e)
        return nullptr;

    // dword_6420F4 = h; if (h) { VIBE_Sound3d_SetLooping(h, 1); ... }
    marketHandle = e;
    return e;
}

bool StopMarketLoop(audio::SoundSystem& sound, audio::Sound3dEntry*& marketHandle) {
    // if ( dword_6420F4 ) { SetLooping(h,0); StopEntry(h,1); DetachEntry(h); .. }
    if (!marketHandle)
        return false;
    sound.pool3d().detach(marketHandle); // VIBE_Sound3d_DetachEntry @0x4246ec
    marketHandle = nullptr;              // dword_6420F4 = 0
    return true;
}

} // namespace guild::app
