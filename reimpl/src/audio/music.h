#pragma once
// guild::audio — streaming music / ambient track facade of gilde.exe.
//
// Tracks are streamed (not voiced) through a small slot table keyed by name. A
// track slot caches the stream handle, its active flag, target volume and a
// global fade modifier; the engine fades the digital master volume by that
// modifier each mixer tick.
//
// Recovered from:
//   VIBE_Audio_LoadTrack          @0x439ed0  (find-or-recycle slot by name)
//   VIBE_Audio_StartTrack         @0x439f8c  (open stream, set vol, start/pause)
//   VIBE_Audio_StopTrack          @0x43a2fc  (fade-out or hard stop)
//   VIBE_Audio_IsTrackPlaying     @0x43a37c  (stream status == 4)
//   VIBE_Audio_SetMusicVolume     @0x439e90
//   VIBE_Audio_ApplyMasterVolume  @0x439ddc  (master * fade modifier, clamped)
//   VIBE_Audio_SetFadeVolume      @0x43a7b8  (immediate or timed fade)
#include "guild/common/types.h"
#include "shim/IAudioDevice.h"
#include <string>
#include <vector>

namespace guild::audio {

// Default per-track volume seeded by StartTrack (a1+0x118 = 127).
constexpr int kDefaultTrackVolume = 127;

// A streaming track slot. Name at +0x00, stream handle at +0x100, active flag
// at +0x104, target volume at +0x118 (StartTrack). Stride 296 bytes.
struct MusicTrack {
    std::string name;                 // +0x00
    shim::VoiceHandle stream = -1;    // +0x100  (0/-1 == no stream)
    bool active = false;              // +0x104
    int volume = kDefaultTrackVolume; // +0x118
    bool loop = false;
    const void* pcm = nullptr;
    std::size_t pcmBytes = 0;
    int sampleRate = 44100;
};

class MusicPlayer {
public:
    explicit MusicPlayer(shim::IAudioDevice* device) : device_(device) {}

    // VIBE_Audio_FindActiveTrackSlot @0x43a864 — active slot whose name matches.
    MusicTrack* findActiveTrack(const std::string& name);

    // VIBE_Audio_LoadTrack @0x439ed0 + StartTrack @0x439f8c — find-or-create the
    // named track slot, open its stream, set volume and begin playback. `loop`
    // maps to StartTrack's loop-count branch (v26 ? loops=0 : loops=1).
    MusicTrack* loadTrack(const std::string& name, const void* pcm,
                          std::size_t bytes, int sampleRate, bool loop);

    // VIBE_Audio_StopTrack @0x43a2fc — fade==true requests a 2s fade-out; else a
    // hard stop (pause + close stream, clear active flag).
    void stopTrack(MusicTrack* t, bool fade);

    // VIBE_Audio_IsTrackPlaying @0x43a37c — stream active flag (status == 4).
    bool isTrackPlaying(const MusicTrack* t) const { return t && t->active; }

    // VIBE_Audio_SetMusicVolume @0x439e90 — set the digital master (music) volume.
    void setMusicVolume(int vol);

    // VIBE_Audio_SetFadeVolume @0x43a7b8 — set the fade modifier in [0,1]. The
    // `out-of-range` guard (a1 >= 0 && a1 <= 1.0) is preserved. (Timed fades are
    // modeled as immediate here; the original interpolates over `ms`.)
    void setFadeVolume(float modifier, int ms);

    // VIBE_Audio_ApplyMasterVolume @0x439ddc — push master*fadeModifier (clamped
    // modifier to [0,1]) to the device master volume.
    void applyMasterVolume(int masterVolume);

    float fadeModifier() const { return fadeModifier_; }
    int   masterVolume() const { return masterVolume_; }
    std::vector<MusicTrack>& tracks() { return tracks_; }

private:
    shim::IAudioDevice* device_;
    std::vector<MusicTrack> tracks_;
    float fadeModifier_ = 1.0f; // flt_62D9F8
    int   masterVolume_ = 127;  // dword_62DA0C (last master applied)
};

} // namespace guild::audio
