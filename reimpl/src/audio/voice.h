#pragma once
// guild::audio — voice-slot facade for the SoundBank (sb_*) layer of gilde.exe.
//
// The original wraps Miles Sound System (mss32.dll _AIL_*). VIBE_Sound_LibInit
// @0x445d90 allocates an array of 48-byte voice slots (one per hardware voice;
// the engine inits 48 voices / 2ch / 44100Hz), each backed by an MSS sample
// handle. Playback, volume, pan and loop control all route through those slots.
//
// Here the slot bookkeeping is preserved 1:1; the actual device side (alloc a
// hardware voice, push PCM, set volume/pan) routes through shim::IAudioDevice
// instead of _AIL_*.
#include "guild/common/types.h"
#include "shim/IAudioDevice.h"
#include <vector>

namespace guild::audio {

// Voice-slot status flag bits stored at VoiceSlot::flags (+0x14).
// Recovered from VIBE_Sound_SetLoopFlag @0x446330, VIBE_Audio_StopVoice
// @0x447508 and VIBE_Sound_AllocVoiceChannel @0x446730.
enum VoiceFlags : u8 {
    kVoiceFlag_FadeStop = 0x02, // pending fade-out stop  (StopVoice fade==1)
    kVoiceFlag_LoopHold = 0x04, // explicit loop-count override active (PlayVoiceSample)
    kVoiceFlag_Released = 0x08, // slot logically ended / free to reclaim
    kVoiceFlag_Looping  = 0x10, // loop active — slot must not be recycled
};

// MSS "playing" status code returned by AIL_sample_status (SMP_PLAYING == 4).
// Used by VIBE_Audio_VoiceIsPlaying @0x4471b0 and the allocator/queue.
constexpr int kSampleStatusPlaying = 4;

// The original's voice recycler/queue ask the device whether a voice is still
// playing (AIL_sample_status == 4). shim::IAudioDevice is a fixed shared
// interface that does not expose that query, so the facade reaches it through
// this optional extension: a backend that can report playback status derives
// from it in addition to shim::IAudioDevice. When the injected device does not
// implement it, voices are treated as never-playing (free to recycle).
class IAudioStatusDevice {
public:
    virtual ~IAudioStatusDevice() = default;
    // Mirrors VIBE_Audio_GetSampleStatus @0x44a438 / AIL_sample_status.
    virtual int sampleStatus(shim::VoiceHandle) = 0;
};

// Per-voice slot, 48 bytes in gilde.exe (VIBE_Sound_LibInit allocs 48*nVoices).
// Field offsets recovered from VIBE_Sound_PlaySample @0x4461d0,
// VIBE_Sound_AllocVoiceChannel @0x446730, VIBE_Audio_SetVoiceVolume @0x44754c
// and VIBE_Audio_SetVoicePan @0x447574.
//
// Note the original's accessor naming is crossed: VIBE_Audio_SetVoiceVolume
// writes +0x28 while VIBE_Audio_SetVoicePan writes +0x24, yet PlaySample seeds
// +0x24 = 127 (a volume) and +0x28 = 63 (a pan-center). We keep the byte
// offsets exact and document the role each offset actually plays.
struct VoiceSlot {
    shim::VoiceHandle handle = -1; // +0x00  device voice handle (MSS sample handle)
    const void*       sample = nullptr; // +0x04  resolved bank sample record (sb)
    u32               priority = 0; // +0x08  start tick; lowest is recycled first
    u32               loopCount = 0; // +0x0C  loop count / "1" = play-once seed
    u32               field10 = 0; // +0x10
    u8                flags = 0; // +0x14  VoiceFlags
    u8                pad15[3] = {0, 0, 0}; // +0x15
    u32               field18 = 0; // +0x18
    u32               field1C = 0; // +0x1C
    u32               field20 = 0; // +0x20
    int               volume = 127; // +0x24  0..127  (seeded 127 in PlaySample)
    int               pan = 63; // +0x28  0..127, 63/64 center (seeded 63 in PlaySample)
    u32               field2C = 0; // +0x2C
};
static_assert(sizeof(int) == 4, "expects 32-bit int");

// The voice pool: the array of 48-byte slots plus the device.
// Mirrors the globals dword_62EA08 (slot array) / dword_62EA0C (count) /
// dword_62E8FC (digital-output handle, non-null once initialized).
class VoicePool {
public:
    explicit VoicePool(shim::IAudioDevice* device)
        : device_(device), status_(dynamic_cast<IAudioStatusDevice*>(device)) {}

    // VIBE_Sound_LibInit @0x445d90 — allocate `voices` zeroed 48-byte slots and
    // bind a device voice to each. The engine calls with 48 voices/2ch/44100Hz.
    // Returns false if any device voice could not be allocated (original returns
    // -1 in that case but still keeps the pool).
    bool init(int voices, int channels, int sampleRate);

    // VIBE_Sound_Shutdown @0x439ccc — free device voices and drop the pool.
    void shutdown();

    bool initialized() const { return initialized_; }
    int  voiceCount() const { return static_cast<int>(slots_.size()); }
    VoiceSlot* slotAt(int i) { return (i >= 0 && i < voiceCount()) ? &slots_[i] : nullptr; }

    // VIBE_Sound_AllocVoiceChannel @0x446730 — pick a voice slot to (re)use.
    // Pass 1: first slot that is neither playing (device status != playing) nor
    // looping (flag 0x10). If none free, pass 2 over the rest steals the slot
    // with the lowest priority (+0x08) that is also not playing/looping.
    // Returns null when every voice is busy (the original returns 0 → invalid).
    VoiceSlot* allocVoiceChannel();

    // VIBE_Sound_SetLoopFlag @0x446330 — set/clear the loop bit (0x10).
    static void setLoopFlag(VoiceSlot* v, bool looping);

    // VIBE_Audio_SetVoiceVolume @0x44754c — clamp+store volume (writes +0x28 in
    // the original). a2 < 0x80 gate => values 0..127 only; out-of-range ignored.
    void setVoiceVolume(VoiceSlot* v, int vol);

    // VIBE_Audio_SetVoicePan @0x447574 — clamp+store pan (writes +0x24 in the
    // original). Same 0..127 gate.
    void setVoicePan(VoiceSlot* v, int pan);

    // VIBE_Audio_VoiceIsPlaying @0x4471b0 — true iff the device reports the slot's
    // voice as still playing (status == 4). Original returns -1/1; we return bool.
    bool voiceIsPlaying(const VoiceSlot* v) const;

    // VIBE_Audio_StopVoice @0x447508 — stop (fade==false) marks Released and stops
    // the device voice; fade==true sets the pending-fade flag instead.
    void stopVoice(VoiceSlot* v, bool fade);

    // Push a sample's PCM to a slot's device voice and (re)apply volume/pan/loop.
    // Models the device-facing half of VIBE_Audio_PlayVoiceSample @0x44737c:
    // SetSampleVolume/SetSamplePan/SetSampleLoopCount then StartSample.
    void startVoice(VoiceSlot* v, const void* pcm, std::size_t bytes,
                    int sampleRate, int loops);

    shim::IAudioDevice* device() const { return device_; }

private:
    shim::IAudioDevice* device_;
    IAudioStatusDevice* status_; // null if device can't report status
    std::vector<VoiceSlot> slots_;
    bool initialized_ = false;
};

} // namespace guild::audio
