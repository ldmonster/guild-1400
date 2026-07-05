#pragma once
// guild::play — outro-movie playback CONTROL logic (cutscene_recon2 cluster).
//
// gilde.exe 0x534924 — VIBE_Movie_PlayOutro (__usercall, al = ..., edi = arg0).
//
// This reconstructs the *control flow* around the engine's MPEG outro playback:
//   * one-time lazy bind of the proprietary "moveahead.dll" player (mov_Init_,
//     mov_Play_, mov_Stop_, mov_Prepare_, mov_PrepareDD_, mov_Dispose_,
//     mov_SetVisible_, mov_GetEvent_, mov_Exit_) behind a guard flag (dword_63C8F0);
//   * a fade-to-BLACK pre-roll that spins the game frame loop until the fade
//     overlay reports "done" (flag bit 0x4) AND the music-fade timer (flt_62DA00)
//     has gone negative (VIBE_Audio_MixerUpdate @0x43a3e4 parks it at -1.0 when
//     the fade completes);
//   * save/restore of the global "movies enabled" byte (byte_642008) and the
//     master music volume (byte_1233552 * (1/127)) across the playback;
//   * input re-acquire / window-focus restore after the clip ends.
//
// RULE 5/6 BOUNDARY: the actual MPEG decode + presentation lives in moveahead.dll
// (proprietary codec). We do NOT reimplement it. The player vtable is modeled as a
// set of inert hooks (MoviePlayerHooks) that default to no-ops; a real backend can
// bind them later. The pure CONTROL logic (guard, fade-spin, volume save/restore,
// the outro.mpg path build) is reconstructed 1:1.

#include "guild/common/types.h"
#include <cstdint>
#include <functional>
#include <string>

namespace guild::play {

using guild::i32;
using guild::u8;
using guild::u32;

// flt_62DA00 == 0xBF800000 == -1.0f : the fade-COMPLETE sentinel. While a music
// fade is in progress the timer is >= 0.0 and the loop keeps spinning
// (@0x534a8e jbe back into the body); MixerUpdate sets -1.0 on completion,
// which is what lets the loop exit (0.0 > flt_62DA00).
inline constexpr float kMovieFadeTimerSentinel = -1.0f;
// flt_623640 == 0x3C010204 : the master-volume scale (≈ 1/127) applied to the
// stored 0..127 music-volume byte when restoring the music fade after the clip.
// (0.00787353515625f was WRONG — that encodes 0x3C010000; get_bytes @0x623640
// gives 04 02 01 3C == 0x1.020408p-7f == 0.0078740157186985...)
inline constexpr float kMovieMusicVolumeScale  = 0x1.020408p-7f;  // bit-exact 0x3C010204

inline constexpr int   kMovieFadeColorTime = 90;     // VIBE_Fade_Register fade-time arg (0x5A)
inline constexpr int   kMovieMusicFadeMs   = 1000;   // music fade duration (both pre/post)
inline constexpr u32   kMoviePrePlaySleepMs = 0x1F4; // 500ms after window pump, before Prepare
inline constexpr u32   kMoviePostPrepSleepMs = 0xC8; // 200ms after Prepare, before Play
inline constexpr int   kMoviePrepareEventArg = 138;  // dword_63C778(0,0,0,0,hInstance,138)

// The proprietary moveahead.dll entry-point table (mov_* exports). All inert by
// default — this is the rule-6 codec boundary; a real backend binds these.
struct MovieDllHooks {
    std::function<void()>                          init;          // mov_Init_
    std::function<void()>                          exit;          // mov_Exit_
    std::function<void(int handle)>                play;          // mov_Play_  (eax=handle)
    std::function<void(int handle)>                stop;          // mov_Stop_
    // mov_Prepare_(ecx=0,edx=0, 0, 0, hInstance, eventArg) -> playback handle
    std::function<int(void* hInstance, int eventArg)> prepare;
    std::function<void()>                          prepareDD;
    std::function<void()>                          dispose;
    std::function<void(int)>                       setVisible;
    std::function<int()>                           getEvent;
    bool bound = false;     // dword_63C8F0 : the one-time-init guard
    int  handle = 0;        // dword_63C764 : the active playback handle
};

// Engine-side control hooks the outro routine drives (fade overlay, music, input,
// window). Defaults are inert no-ops so the control logic is exercised headless.
struct MovieControlHooks {
    // VIBE_Music_SetTrackFade(volume, durationMs)
    std::function<void(float volume, int durationMs)> musicSetTrackFade;
    // VIBE_Fade_Register(0, ?, screenH>>16, screenW>>16, "BLACK", 90, 1) -> overlay*
    // Returns a pointer to a byte whose bit 0x4 is set once the fade has finished.
    std::function<const u8*()>            fadeRegister;
    std::function<void(const u8* ov)>     fadeUnregister;
    // Spins one frame of game logic (VIBE_GameLogic_RunFrameLoop). Returns the
    // current fade-overlay status byte and the fade-timer value the loop tests.
    std::function<void()>                 runFrameLoop;
    std::function<void()>                 presentFrame;        // VIBE_Render_PresentFrame
    std::function<void(bool focusGrab)>   pollKeyboard;        // dword_62D0E0 gate + poll
    std::function<void(bool acquire)>     acquireMouse;        // VIBE_Input_AcquireMouseDevice
    std::function<void()>                 pumpMessages;        // VIBE_Window_PumpMessages
    std::function<void()>                 setFocus;            // SetFocus(dword_63CC18)
    std::function<void(u32 ms)>           sleep;               // Sleep
    // The fade-overlay status byte and fade-timer value queried each loop turn.
    std::function<u8()>                   fadeStatusByte;      // *(byte*)overlay
    std::function<float()>                fadeTimer;           // flt_62DA00
    void* hInstance = nullptr;                                 // ds:hInstance for Prepare
};

// Global state mirrored by the routine (the bytes it saves/restores). Modeled as a
// struct so the save/restore behavior is testable; production points these at the
// real globals.
struct MovieGlobals {
    u8 moviesEnabled  = 1;   // byte_642008 : disabled (0) during the clip, restored after
    u8 musicVolume    = 127; // byte_1233552 : 0..127 master music volume
    u8 someFlag67225C = 0;   // byte_67225C : cleared after the clip
    int renderMode    = 1;   // dword_62D580 : ==1 -> PresentFrame before the clip
};

// gilde.exe 0x534924 — VIBE_Movie_PlayOutro.
// Plays "<movieDir>outro.mpg". Returns the saved (pre-clip) value of
// byte_642008 (the original returns it in al). When dll binding fails the original
// returns the (zero) library handle; we mirror that by returning 0 and not touching
// globals (see body). `movieDir` is aProjectMovie ("\\project\\movie\\").
//
// `width`/`height` are dword_69FFBC>>16 / (dword_69FFB8+2)>>16 — the fade-register
// screen extents. They only flow into fadeRegister, which is inert by default.
u8 Movie_PlayOutro(MovieDllHooks& dll, MovieControlHooks& ctl, MovieGlobals& g,
                   const std::string& movieDir, int width, int height,
                   bool dllAvailable = false);

} // namespace guild::play
