#include "audio/music_cutscene.h"

namespace guild::audio {

// gilde.exe 0x581b0c — VIBE_Music_PlayCutsceneTrack
//   void __usercall(char *name@<eax>)
void CutsceneMusic::playCutsceneTrack(const std::string& name, const void* pcm,
                                      std::size_t bytes, int sampleRate) {
    const bool hasTrack = !name.empty() && pcm != nullptr;

    // First entry of the cutscene: snapshot the world music-enable flag, mark
    // the cutscene active and clear the world flag so the director goes quiet.
    //   if ( !dword_642024 ) { dword_642010 = byte_642008; dword_642024 = 1;
    //                          byte_642008 = 0; }
    if (!inCutscene_) {
        savedMusicEnabled_ = worldMusicEnabled_;
        inCutscene_ = true;
        worldMusicEnabled_ = false;
    }

    // Only drive the music device when the subsystem is on and the user's music
    // volume setting is non-zero.
    //   if ( dword_63C8F8 && (LODWORD(flt_6422A8) & 0x7FFFFFFF) != 0 )
    if (!musicSystemOn_ || musicVolumeSetting_ == 0.0f)
        return;

    if (hasTrack) {
        // if ( dword_642018 ) VIBE_Audio_StopTrack(dword_642018, 1, name);
        if (activeTrack_)
            music_->stopTrack(activeTrack_, /*fade=*/true);
        // dword_642020 = VIBE_Audio_LoadTrack(name); dword_642018 = dword_642020;
        cutsceneTrack_ = music_->loadTrack(name, pcm, bytes, sampleRate, /*loop=*/false);
        activeTrack_ = cutsceneTrack_;
    } else if (activeTrack_) {
        // else if ( dword_642018 ) VIBE_Audio_SetFadeVolume(0.40000001, 2000);
        music_->setFadeVolume(kCutsceneDuckVolume, kCutsceneDuckFadeMs);
    }
}

// gilde.exe 0x581c04 — VIBE_Music_RestoreAfterCutscene
//   void()
void CutsceneMusic::restoreAfterCutscene() {
    // byte_642008 = dword_642010; dword_642024 = 0;
    worldMusicEnabled_ = savedMusicEnabled_;
    inCutscene_ = false;

    // if ( dword_63C8F8 && dword_642018 != dword_642020 )
    //   if ( dword_642018 ) VIBE_Audio_SetFadeVolume(1.0, 3000);
    if (musicSystemOn_ && activeTrack_ != cutsceneTrack_) {
        if (activeTrack_)
            music_->setFadeVolume(kCutsceneRestoreVolume, kCutsceneRestoreFadeMs);
    }
}

// gilde.exe 0x581c48 — VIBE_Music_SetTrackFade
//   void __stdcall(float a1, int a2)
void CutsceneMusic::setTrackFade(float modifier, int ms) {
    // if ( dword_642018 ) VIBE_Audio_SetFadeVolume(a1, a2);
    if (activeTrack_)
        music_->setFadeVolume(modifier, ms);
}

} // namespace guild::audio
