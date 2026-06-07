#pragma once
// gilde.exe — per-frame AUDIO tick + app-side SFX triggers (guild::app).
//
// This module reconstructs the AUDIO portion of the per-frame game loop: the
// block of VIBE_GameLogic_RunFrameLoop (@0x4c09a0) that, gated by the audio
// subsystem-enable flags, drives the reconstructed audio cores each frame.
// It also reconstructs the app-side market-ambience SFX trigger pair that the
// scene/options loops fire (VIBE_Ambient_StartMarketLoop/StopMarketLoop).
//
// The originals are app/sim-side drivers that read loose engine globals and call
// into the (already reconstructed) audio cores. Here those globals are modelled
// as AudioTickState fields and the cores are the real guild::audio siblings.
//
// Reconstructed from VIBE_GameLogic_RunFrameLoop @0x4c09a0:
//   if (dword_63C900) {                          // snd_3dEnabled  (0x4c0d39)
//       VIBE_Ambient_UpdateWildlifeSounds();     //                (0x4c0d3b)
//       VIBE_VoiceQueue_ProcessNext();           // speech queue   (0x4c0d40)
//   }
//   if (dword_63C8F8 && (mask & 0x40000))        // snd_musicOn + day-cycle bit
//       VIBE_Music_UpdateOutdoorTrackPlayback(); //                (0x4c0d58)
//   ...
//   if (dword_63C900) {                          //                (0x4c0ddd)
//       VIBE_Sound3d_UpdateAll();                // 3D positional  (0x4c0ddf)
//       if (dword_63C904) VIBE_Sound3d_UpdateListener(); // terrain loops (0x4c0ded)
//       VIBE_Sound_UpdateVoices();               // mixer voices   (0x4c0df2)
//   }
//
//   VIBE_Ambient_StartMarketLoop @0x582858, VIBE_Ambient_StopMarketLoop @0x5828bc.
//
// The terrain-coupled VIBE_Sound3d_UpdateListener (@0x425208) reads the live
// heightmap/floor octree (VIBE_Floor_PickTileAtPoint / Heightmap_LookupTile-
// Attribute) which is out of scope for a headless audio tick; its hook is a
// documented stub here (it only adds extra terrain "echo" voices on top of the
// reconstructed UpdateAll/UpdateVoices path).
#include "guild/common/types.h"
#include "app/gamelogic.h"          // mask:: bits (kDayCycleMusic)
#include "audio/sound.h"            // SoundSystem (voices / 3D pool / queue)
#include "audio/music_world.h"      // MusicDirector + UpdateOutdoorTrackPlayback
#include "audio/sound3d.h"          // Vec3 (listener pose)

namespace guild::app {

// The audio subsystem-enable gate flags the original tests at the top of each
// audio block (dword_63C900/dword_63C904/dword_63C8F8). They are set true once
// the corresponding subsystem has been brought up (sound lib / weather loops /
// music thread).
struct AudioEnable {
    bool sound3dOn = false;  // dword_63C900 @0x63C900 — digital/3D sound enabled
    bool weatherOn = false;  // dword_63C904 @0x63C904 — weather ambient-loops enabled
    bool musicOn   = false;  // dword_63C8F8 @0x63C8F8 — streaming music enabled
};

// What the audio tick decided this frame (so callers/tests can assert it).
struct AudioTickResult {
    bool wildlifeRan = false;     // the wildlife/ambient updater ran
    bool voiceQueueRan = false;   // the speech-queue head was serviced
    audio::PlaybackAction music = audio::PlaybackAction::kNone; // music transition
    bool sound3dRan = false;      // the 3D positional pool update ran
    bool listenerRan = false;     // the terrain-loop listener update ran (stub)
    bool voicesRan = false;       // the mixer voice update ran
};

// The per-frame audio listener pose (camera) the 3D pool updates against. In the
// original these come from the live camera transform (dword_13FCD1C); the app
// supplies them so the 3D pool can be driven headless.
struct AudioListener {
    audio::Vec3 pos;       // listener world position
    audio::Vec3 forward;   // listener forward vector
};

// gilde.exe 0x4c09a0 (audio block) — drive the audio cores for one frame.
//   `featureMask` is the same v54 the frame loop received; the music block is
//   additionally gated by (v54 & 0x40000) (mask::kDayCycleMusic). `tickCounter`
//   is the original 13*dword_62EB38 voice-queue/wildlife time base (the caller
//   passes the per-frame tick; this fn applies the *13 the queue expects).
//   `season`/`atStreamEnd`/`locationId` feed the outdoor-music state machine.
// Returns the transitions taken (for assertion).
AudioTickResult AudioTick(audio::SoundSystem& sound, audio::MusicDirector& music,
                          const AudioEnable& enable, std::uint32_t featureMask,
                          int tickCounter, const AudioListener& listener,
                          int season, bool atStreamEnd, int locationId);

// gilde.exe 0x582858 — VIBE_Ambient_StartMarketLoop. Start the looping market
// ambience iff it is not already playing (handle == 0). Resolves the sample,
// attaches a looping 3D entry (range 2200) and stores its handle. Returns the
// (new or existing) market-loop entry, or null if it could not start.
//   Original: if (!dword_6420F4) { v = PlaySample(...); h = Sound3d_AttachToEntity(
//             v, 127, 2200.0); dword_6420F4 = h; if (h) { SetLooping(h,1); ... } }
audio::Sound3dEntry* StartMarketLoop(audio::SoundSystem& sound,
                                     audio::Sound3dEntry*& marketHandle,
                                     const audio::Vec3& pos,
                                     const AudioListener& listener,
                                     const std::string& sampleName);

// gilde.exe 0x5828bc — VIBE_Ambient_StopMarketLoop. Stop + detach the market
// loop if playing. Returns true iff it stopped one.
//   Original: if (dword_6420F4) { Sound3d_SetLooping(h,0); Sound3d_StopEntry(h,1);
//             Sound3d_DetachEntry(h); dword_6420F4 = 0; }
bool StopMarketLoop(audio::SoundSystem& sound, audio::Sound3dEntry*& marketHandle);

} // namespace guild::app
