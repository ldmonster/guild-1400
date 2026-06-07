#pragma once
// guild::audio — self-contained audio "leaf" math/bookkeeping of gilde.exe.
//
// These are the deterministic, side-effect-light helpers around the Miles
// digital-driver layer: slot/handle/driver index lookups over the 16-output
// table (dword_62EA1C[16] = DigitalAudio::outputs_), the global preference
// (max-sample-handles) get/set, the digital master-volume clamp, the
// volume-settings curve (master/music/sfx mix), the music-track fade-out arm /
// name-prefix copy, and the ambient-voice tracking list. The raw Miles MSS
// calls route through shim::IAudioDevice (already wired in DigitalAudio); here
// only the integer/float bookkeeping is reconstructed 1:1.
//
// Recovered from:
//   VIBE_Audio_FindFreeStreamSlot        @0x44ab2c
//   VIBE_Audio_LookupStreamHandleIndex   @0x44abd8
//   VIBE_Audio_LookupSampleDriverIndex   @0x44ac20
//   VIBE_Audio_LookupStreamDriverIndex   @0x44ac88
//   VIBE_Audio_FindDriverIndex           @0x44aa94
//   VIBE_Audio_GetGlobalPreference       @0x449dd4
//   VIBE_Audio_SetGlobalPreference       @0x449e20
//   VIBE_Audio_CountAllocatedVoices      @0x449528
//   VIBE_Audio_ClampDigitalMasterVolume  @0x4498b4 (clamp portion of SetDigitalMasterVolume)
//   VIBE_Audio_ApplyVolumeSettings       @0x56c148
//   VIBE_Audio_FadeOutTrack              @0x43a910
//   VIBE_Audio_SetTrackNamePrefix        @0x43a95c
//   VIBE_Audio_PlayAmbientVoice          @0x505ba8 (list-tracking portion)
//   VIBE_Audio_StopAmbientVoices         @0x505da8
#include "audio/digital_output.h"
#include "guild/common/types.h"
#include <array>
#include <functional>
#include <string>

namespace guild::audio {

// ---------------------------------------------------------------------------
// Digital-driver slot / handle / driver-index lookups (operate on the same
// 16-output table modelled by DigitalAudio). The original indexes parallel
// arrays dword_62EA5C[i] (sample slots) / dword_62EA9C[i] (stream slots) keyed
// by the output pointer dword_62EA1C[i]; here those live inside each output.
// ---------------------------------------------------------------------------

// gilde.exe 0x44ab2c — VIBE_Audio_FindFreeStreamSlot (eax = find(out@eax)).
// First free (zero) stream slot of `outIndex`, or -1 (full / closed / capacity).
int FindFreeStreamSlot(const DigitalAudio& d, int outIndex);

// gilde.exe 0x44abd8 — VIBE_Audio_LookupStreamHandleIndex (eax = lookup(h@eax)).
// Per-output slot index of stream `handle` (first match), or -1.
int LookupStreamHandleIndex(const DigitalAudio& d, shim::VoiceHandle handle);

// gilde.exe 0x44ac20 — VIBE_Audio_LookupSampleDriverIndex (eax = lookup(h@eax)).
// Output (driver) index owning sample `handle`, or -1.
int LookupSampleDriverIndex(const DigitalAudio& d, shim::VoiceHandle handle);

// gilde.exe 0x44ac88 — VIBE_Audio_LookupStreamDriverIndex (eax = lookup(h@eax)).
// Output (driver) index owning stream `handle`, or -1.
int LookupStreamDriverIndex(const DigitalAudio& d, shim::VoiceHandle handle);

// gilde.exe 0x44aa94 — VIBE_Audio_FindDriverIndex (eax = find(out@eax)).
// Index of the output slot at `outIndex` if it is open, scanning the 16-table
// exactly as the original (which matches by pointer identity). Returns the
// index, or -1 if `outIndex` is not a valid open slot.
int FindDriverIndex(const DigitalAudio& d, int outIndex);

// gilde.exe 0x449528 — VIBE_Audio_CountAllocatedVoices.
// Sum of allocatedSampleCount across all open outputs (the original counts the
// per-voice 0x10 "allocated" flag over the global voice array; the open-output
// allocated counts are the faithful equivalent of that running total).
int CountAllocatedVoices(const DigitalAudio& d);

// gilde.exe 0x449dd4 — VIBE_Audio_GetGlobalPreference (eax = get(&out@eax)).
// Reads the global max-sample-handles preference (dword_62EAE0) into *out.
// Returns 0 on success, -1 if no driver / (no open outputs && pref==0).
int GetGlobalPreference(const DigitalAudio& d, int globalPref, int* out);

// gilde.exe 0x449e20 — VIBE_Audio_SetGlobalPreference (eax = set(val@eax)).
// Sets the global preference. Refused (-1) when >1 output is open, or no driver.
// Returns the (possibly unchanged) preference via *globalPref; result 0 / -1.
int SetGlobalPreference(const DigitalAudio& d, int* globalPref, int value);

// gilde.exe 0x4498b4 — clamp portion of VIBE_Audio_SetDigitalMasterVolume.
// The original rejects volumes >= 0x80 (128). Returns the clamped/validated
// volume in [0,127], or -1 if out of range. (No device call here.)
int ClampDigitalMasterVolume(int volume);

// ---------------------------------------------------------------------------
// Volume-settings curve (VIBE_Audio_ApplyVolumeSettings @0x56c148).
//
// The original reads five 0..255 byte sliders and two float scales, then
// derives the master / music / sfx / ambient / a fifth scale value. We compute
// the same products; the caller pushes master/music to the device.
//   master = sfxScale  = round(soundByte * scale0)               -> SetMasterVolume
//   sfx    = round(sfxByte  * sfxScale)                           -> ApplyMasterVolume
//   music  = round(musicByte* sfxScale)                          -> SetMusicVolume
//   ambient= ambByte * scale0   (flt_64200C)
//   fifth  = amb2Byte * scale1  (flt_6422A8)
// scale0 = flt_62522C, scale1 = flt_625230.
// ---------------------------------------------------------------------------
struct VolumeSettingsIn {
    u8 soundByte = 0;  // byte_1233550
    u8 musicByte = 0;  // byte_1233551
    u8 sfxByte   = 0;  // byte_1233552
    u8 ambByte   = 0;  // byte_1233553
    u8 amb2Byte  = 0;  // byte_1233554
    float scale0 = 1.0f; // flt_62522C
    float scale1 = 1.0f; // flt_625230
};
struct VolumeSettingsOut {
    int   masterVolume = 0;  // (int)(soundByte * scale0)  -> SetMasterVolume
    int   sfxVolume    = 0;  // (int)(sfxByte   * sfxScale)-> ApplyMasterVolume
    int   musicVolume  = 0;  // (int)(musicByte * sfxScale)-> SetMusicVolume
    float ambientScale = 0;  // ambByte  * scale0  (flt_64200C)
    float ambient2Scale= 0;  // amb2Byte * scale1  (flt_6422A8)
};
VolumeSettingsOut ApplyVolumeSettings(const VolumeSettingsIn& in);

// ---------------------------------------------------------------------------
// Music-track fade-out arming and name-prefix copy.
// ---------------------------------------------------------------------------

// A streaming-track fade record (the 296-byte music-track slot, fade fields).
// The updater drives fadeMode through SetStreamVolume; here we just arm it.
struct TrackFadeState {
    bool   streamActive = false; // *(+260) — 1 once the stream is playing
    int    streamHandle = 0;     // *(+256)
    int    msPosition   = 0;     // *(+268) — cursor (filled by GetStreamMsPosition)
    int    fadeStartMs  = 0;     // *(+272)
    u8     fadeMode     = 0;     // *(+276) — 1=fade-in, 2=fade-out
    int    curVolume    = 0;     // *(+284)
    int    fadeBaseVol  = 0;     // *(+288)
};

// gilde.exe 0x43a910 — VIBE_Audio_FadeOutTrack (eax = arm(track@eax, mode@dl)).
// Arms a fade on an active track: latches the current ms position as the fade
// start and copies the current volume as the fade base. `currentMs` is the
// value GetStreamMsPosition would write (the original calls it inline). No-op
// when the track is null/inactive or mode==0.
void FadeOutTrack(TrackFadeState* t, u8 mode, int currentMs);

// gilde.exe 0x43a95c — VIBE_Audio_SetTrackNamePrefix (al = copy(src@eax)).
// Copies a NUL-terminated prefix string into the global track-name prefix
// buffer (unk_7649B8) two chars at a time (original's word-copy loop). Returns
// the terminating NUL byte (always 0), matching the original's `al`.
char SetTrackNamePrefix(std::string& dst, const char* src);

// ---------------------------------------------------------------------------
// Ambient-voice tracking list (dword_122DCA0[16] + count dword_6344A0).
// PlaySeasonalAmbience/PlayAmbientVoice append started handles; StopAmbientVoices
// stops them all and resets the count. We model just the list bookkeeping; the
// actual StartVoiceSample/StopVoice routes through the owning voice layer.
// ---------------------------------------------------------------------------
constexpr int kMaxAmbientVoices = 16; // dword_6344A0 capped at 16

struct AmbientVoiceList {
    std::array<shim::VoiceHandle, kMaxAmbientVoices> handles{}; // dword_122DCA0
    int count = 0;                                              // dword_6344A0
};

// gilde.exe 0x505ba8 — VIBE_Audio_PlayAmbientVoice (list-append portion).
// Appends `handle` if there is room (count < 16) and bumps the count; returns
// the new count (or the prior count when full), mirroring the original's
// `if (count < 16) { arr[count] = h; return ++count; } return result`.
int AppendAmbientVoice(AmbientVoiceList& list, shim::VoiceHandle handle);

// gilde.exe 0x505da8 — VIBE_Audio_StopAmbientVoices (loop + reset portion).
// Calls `stop(handle)` for each tracked voice, clears the slots, and resets the
// count to 0. Returns the number of voices stopped.
int StopAmbientVoices(AmbientVoiceList& list,
                      const std::function<void(shim::VoiceHandle)>& stop);

} // namespace guild::audio
