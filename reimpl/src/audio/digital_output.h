#pragma once
// guild::audio — low-level digital-output / sample-handle layer of gilde.exe.
//
// The original wraps Miles Sound System (mss32.dll). VIBE_Sound_LibInit opens a
// digital output (VIBE_Audio_OpenDigitalOutput @0x449910 = AIL_waveOutOpen),
// builds a WAVEFORMAT-like descriptor, allocates a per-output array of sample
// handles, and binds one handle per voice (VIBE_Audio_AllocateSampleHandle
// @0x449e70 = AIL_allocate_sample_handle). Up to 16 digital outputs are tracked
// (dword_62EA1C[16]). Streams are opened with VIBE_Audio_OpenStream @0x44a544 /
// VIBE_Audio_StartStream @0x44a74c (AIL_open_stream / AIL_start_stream).
//
// Here the bookkeeping (slot arrays, the 28-byte descriptor, handle alloc /
// recycle, index lookup) is preserved 1:1; the actual Miles calls route through
// shim::IAudioDevice (allocVoice / playSample / ...).
//
// DIGITAL OUTPUT descriptor — 0x1C (28) bytes, built by OpenDigitalOutput:
//   +0x00  ptr/handle  driver handle (AIL_waveOutOpen out param; +0x04 region)
//   +0x04  u16  formatTag  = 1 (PCM); +0x02 word also set to 1
//   +0x06  u16  channels   (a2)
//   +0x08  u32  sampleRate (v12)
//   +0x0C  u32  bytesPerSec = channels * (bits>>3) * sampleRate
//   +0x10  u16  blockAlign  = channels * (bits>>3)
//   +0x12  u16  bitsPerSample (a3)
//   +0x14  u32  allocatedSampleCount (=0 after open)
//   +0x18  u32  maxSampleHandles (dword_62EAE0; AIL_get_preference(1))
#include "guild/common/types.h"
#include "shim/IAudioDevice.h"
#include <cstddef>
#include <vector>

namespace guild::audio {

// Up to 16 digital outputs (dword_62EA1C[16]).
constexpr int kMaxDigitalOutputs = 16;

// Default max sample handles per output (AIL_get_preference(1); Miles' DIG_*).
constexpr int kDefaultMaxSampleHandles = 48;

// Descriptor fields kept byte-exact (see header). channels/rate/bits are the
// open() args; the derived fields are computed exactly as the original.
struct DigitalOutput {
    bool open = false;
    u16 formatTag = 0;        // +0x04
    u16 channels  = 0;        // +0x06
    u32 sampleRate = 0;       // +0x08
    u32 bytesPerSec = 0;      // +0x0C
    u16 blockAlign  = 0;      // +0x10
    u16 bitsPerSample = 0;    // +0x12
    u32 allocatedSampleCount = 0; // +0x14
    u32 maxSampleHandles = 0;     // +0x18  (capacity of the slot arrays)
    // dword_62EA5C[i] sample-handle slot array / dword_62EA9C[i] stream slot array.
    std::vector<shim::VoiceHandle> sampleSlots; // 0 = free slot
    std::vector<shim::VoiceHandle> streamSlots; // 0 = free slot
};

// The digital-audio driver: the 16-output array plus the shim device that does
// the real work. Mirrors VIBE_Sound_* / VIBE_Audio_* low-level globals.
class DigitalAudio {
public:
    explicit DigitalAudio(shim::IAudioDevice* device) : device_(device) {}

    // Driver presence flag (dword_62EADC). The original sets it once the Miles
    // digital driver is installed; OpenDigitalOutput fails (-1) if it is 0.
    void setDriverInstalled(bool on) { driverInstalled_ = on; }
    bool driverInstalled() const { return driverInstalled_; }

    // VIBE_Audio_OpenDigitalOutput @0x449910 — find a free output slot (of 16),
    // build the WAVEFORMAT descriptor (channels=a2, bits=a3, rate=v12), open the
    // device, and allocate the per-output sample/stream slot arrays
    // (maxSampleHandles entries, all zeroed). Returns the slot index, or -1 on
    // failure (no driver / no free slot / device open failed).
    int openDigitalOutput(int channels, int sampleRate, int bitsPerSample,
                          int maxSampleHandles = kDefaultMaxSampleHandles);

    // VIBE_Audio_AllocateSampleHandle @0x449e70 — for the output at `outIndex`,
    // find a free sample slot (VIBE_Audio_FindFreeSampleSlot @0x44aac8), ask the
    // device for a voice, store it in the slot, bump allocatedSampleCount, and
    // return the handle. Returns -1 on failure (bad index / full / device fail).
    shim::VoiceHandle allocateSampleHandle(int outIndex);

    // VIBE_Audio_FindFreeSampleSlot @0x44aac8 — index of the first zero slot in
    // the output's sample array, or -1 when allocatedSampleCount >= capacity.
    int findFreeSampleSlot(int outIndex) const;

    // VIBE_Audio_LookupSampleHandleIndex @0x44ab90 — slot index of `handle`
    // across all outputs (within each output's used range), or -1 if not found.
    int lookupSampleHandleIndex(shim::VoiceHandle handle) const;

    // VIBE_Audio_OpenStream @0x44a544 — allocate a stream slot for `outIndex`,
    // open a stream on the device (modeled as a voice handle), store it, bump
    // allocatedSampleCount. Returns the stream handle, or -1 on failure.
    shim::VoiceHandle openStream(int outIndex);

    // VIBE_Audio_StartStream @0x44a74c — start a previously opened stream. Returns
    // 0 on success, -1 if the driver is absent or the handle is unknown.
    int startStream(shim::VoiceHandle streamHandle);

    // VIBE_Audio_ReleaseSampleHandle @0x44a028 — free `handle`: clear its slot,
    // decrement the owning output's allocatedSampleCount, free the device voice.
    // Returns 0 on success, -1 if the handle is not currently allocated.
    int releaseSampleHandle(shim::VoiceHandle handle);

    int  outputCount() const { return static_cast<int>(outputs_.size()); }
    DigitalOutput* outputAt(int i) {
        return (i >= 0 && i < outputCount()) ? &outputs_[static_cast<std::size_t>(i)] : nullptr;
    }
    const DigitalOutput* outputAt(int i) const {
        return (i >= 0 && i < outputCount()) ? &outputs_[static_cast<std::size_t>(i)] : nullptr;
    }
    shim::IAudioDevice* device() const { return device_; }

private:
    shim::IAudioDevice* device_;
    bool driverInstalled_ = false;
    std::vector<DigitalOutput> outputs_; // dword_62EA1C[16] (lazily grown)
};

} // namespace guild::audio
