#pragma once
// guild::audio — CUTSCENE music override of gilde.exe.
//
// During a cutscene the world music director is suspended and a dedicated
// cutscene track is streamed in its place. Entering a cutscene snapshots the
// world music-enable flag, mutes the world director, then loads the cutscene
// track (or, when no track is named, fades the *current* music down to a quiet
// bed at 0.4). Leaving the cutscene restores the snapshotted flag and fades the
// music back up to full if the active stream actually changed.
//
// Recovered from:
//   VIBE_Music_PlayCutsceneTrack    @0x581b0c
//   VIBE_Music_RestoreAfterCutscene @0x581c04
//   VIBE_Music_SetTrackFade         @0x581c48
//
// The originals operate on a cluster of loose globals; they are modelled here as
// controller state (matching MusicPlayer / VoiceQueue in this module):
//   byte_642008  worldMusicEnabled  — world director's music-enable flag
//   dword_642010 savedMusicEnabled  — snapshot taken on cutscene entry
//   dword_642024 inCutscene         — 1 while a cutscene override is active
//   dword_642018 activeTrack        — handle of the currently-streaming track
//   dword_642020 cutsceneTrack      — handle of the track loaded for the cutscene
//   dword_63C8F8 musicSystemOn      — global music subsystem gate
//   flt_6422A8   musicVolumeSetting — user music volume (override skipped if 0)
//
// Track load/stop/fade route through MusicPlayer, which itself routes to
// shim::IAudioDevice (no Miles).
#include "audio/music.h"

namespace guild::audio {

// Fade target while a cutscene plays no dedicated track (dbl 0.40000001 @0x581b91).
constexpr float kCutsceneDuckVolume = 0.40000001f;
constexpr int   kCutsceneDuckFadeMs = 2000;   // VIBE_Audio_SetFadeVolume(.4, 2000)
constexpr float kCutsceneRestoreVolume = 1.0f;
constexpr int   kCutsceneRestoreFadeMs = 3000; // VIBE_Audio_SetFadeVolume(1.0, 3000)

class CutsceneMusic {
public:
    explicit CutsceneMusic(MusicPlayer* music) : music_(music) {}

    // Wire the world music-enable flag the original snapshots (byte_642008) and
    // the subsystem gates the original reads.
    void setWorldMusicEnabled(bool on) { worldMusicEnabled_ = on; }
    bool worldMusicEnabled() const { return worldMusicEnabled_; }
    void setMusicSystemOn(bool on) { musicSystemOn_ = on; }     // dword_63C8F8
    void setMusicVolumeSetting(float v) { musicVolumeSetting_ = v; } // flt_6422A8

    // VIBE_Music_PlayCutsceneTrack @0x581b0c — begin (or update) the cutscene
    // music override. `name`/`pcm` describe the track to stream; pass an empty
    // name (track == nullptr / pcm == nullptr) to request the "no track" path
    // that simply ducks the current music to 0.4. On the first call of a cutscene
    // the world music-enable flag is snapshotted and cleared.
    void playCutsceneTrack(const std::string& name, const void* pcm,
                           std::size_t bytes, int sampleRate);

    // Convenience: the "no dedicated track" entry point (a1 == nullptr branch).
    void playCutsceneTrack() { playCutsceneTrack(std::string(), nullptr, 0, 0); }

    // VIBE_Music_RestoreAfterCutscene @0x581c04 — end the override: restore the
    // snapshotted music-enable flag and, if the active stream changed during the
    // cutscene, fade the music back up to full.
    void restoreAfterCutscene();

    // VIBE_Music_SetTrackFade @0x581c48 — fade the active cutscene track (no-op
    // when no track is active).
    void setTrackFade(float modifier, int ms);

    bool inCutscene() const { return inCutscene_; }              // dword_642024
    MusicTrack* activeTrack() const { return activeTrack_; }     // dword_642018
    MusicTrack* cutsceneTrack() const { return cutsceneTrack_; } // dword_642020

private:
    MusicPlayer* music_;
    bool        worldMusicEnabled_ = false; // byte_642008
    bool        savedMusicEnabled_ = false; // dword_642010
    bool        inCutscene_ = false;        // dword_642024
    MusicTrack* activeTrack_ = nullptr;     // dword_642018
    MusicTrack* cutsceneTrack_ = nullptr;   // dword_642020
    bool        musicSystemOn_ = true;      // dword_63C8F8
    float       musicVolumeSetting_ = 1.0f; // flt_6422A8
};

} // namespace guild::audio
