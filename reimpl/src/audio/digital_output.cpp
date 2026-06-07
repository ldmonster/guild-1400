#include "audio/digital_output.h"

namespace guild::audio {

namespace {
// The original tracks exactly 16 output slots (dword_62EA1C[16]); we keep the
// vector sized to 16 so slot indices match the array semantics.
void ensureSlots(std::vector<DigitalOutput>& outs) {
    if (outs.size() < static_cast<std::size_t>(kMaxDigitalOutputs))
        outs.resize(kMaxDigitalOutputs);
}
} // namespace

// gilde.exe 0x449910 — VIBE_Audio_OpenDigitalOutput
// (__usercall, eax = open(a1=&outHandle@eax, a2=channels@edx, a3=bits@ebx);
//  rate arrives in v12/ecx). We expose channels/rate/bits directly.
int DigitalAudio::openDigitalOutput(int channels, int sampleRate, int bitsPerSample,
                                    int maxSampleHandles) {
    // if (!dword_62EADC) return -1;  — driver must be installed.
    if (!driverInstalled_)
        return -1;

    ensureSlots(outputs_);

    // Find the first free output slot among the 16 (dword_62EA1C[i]==0).
    int slot = -1;
    for (int i = 0; i < kMaxDigitalOutputs; ++i) {
        if (!outputs_[static_cast<std::size_t>(i)].open) { slot = i; break; }
    }
    if (slot < 0)
        return -1; // v8 < 0 => return -1

    DigitalOutput& o = outputs_[static_cast<std::size_t>(slot)];
    o = DigitalOutput{};

    // Build the WAVEFORMAT-like descriptor (offsets +0x04..+0x12).
    o.formatTag     = 1;                              // *(+2) and *(+4) PCM tag
    o.channels      = static_cast<u16>(channels);     // +0x06 (a2)
    o.sampleRate    = static_cast<u32>(sampleRate);   // +0x08 (v12)
    o.bitsPerSample = static_cast<u16>(bitsPerSample);// +0x12 (a3)
    // +0x0C bytesPerSec = channels * (bits>>3) * rate.
    o.bytesPerSec   = static_cast<u32>(channels) * (static_cast<u32>(bitsPerSample) >> 3)
                    * static_cast<u32>(sampleRate);
    // +0x10 blockAlign = channels * (bits>>3).
    o.blockAlign    = static_cast<u16>(static_cast<u32>(channels)
                    * (static_cast<u32>(bitsPerSample) >> 3));

    // AIL_waveOutOpen(...) — route to the shim device. Failure => free + -1.
    if (!device_->init(maxSampleHandles, channels, sampleRate))
        return -1;

    // dword_62EAE0 = AIL_get_preference(1) — max sample handles for this output.
    o.maxSampleHandles = static_cast<u32>(maxSampleHandles > 0 ? maxSampleHandles : 0);
    o.allocatedSampleCount = 0; // +0x14 = 0
    // Allocate + zero the per-output slot arrays (dword_62EA5C / dword_62EA9C).
    o.sampleSlots.assign(o.maxSampleHandles, 0);
    o.streamSlots.assign(o.maxSampleHandles, 0);

    o.open = true; // dword_62EA1C[slot] now non-null
    return slot;
}

// gilde.exe 0x44aac8 — VIBE_Audio_FindFreeSampleSlot
// (__usercall, eax = find(a1=output@eax))
int DigitalAudio::findFreeSampleSlot(int outIndex) const {
    if (outIndex < 0 || outIndex >= outputCount())
        return -1;
    const DigitalOutput& o = outputs_[static_cast<std::size_t>(outIndex)];
    if (!o.open)
        return -1;
    // if (allocatedSampleCount >= maxSampleHandles) return -1;
    if (o.allocatedSampleCount >= o.maxSampleHandles)
        return -1;
    if (o.maxSampleHandles == 0)
        return -1;
    // Walk slots until a zero entry; bail if we run off the capacity.
    int idx = 0;
    for (u32 i = 0; i < o.maxSampleHandles; ++i) {
        if (o.sampleSlots[i] == 0)
            return idx;
        if (++idx >= static_cast<int>(o.maxSampleHandles))
            return -1;
    }
    return -1;
}

// gilde.exe 0x449e70 — VIBE_Audio_AllocateSampleHandle
// (__usercall, eax = alloc(a1=output@eax, a2=&outHandle@edx))
shim::VoiceHandle DigitalAudio::allocateSampleHandle(int outIndex) {
    if (!driverInstalled_)                       // if (!dword_62EADC) return -1;
        return -1;
    if (outIndex < 0 || outIndex >= outputCount())
        return -1;
    DigitalOutput& o = outputs_[static_cast<std::size_t>(outIndex)];
    if (!o.open)
        return -1;

    int free = findFreeSampleSlot(outIndex);     // VIBE_Audio_FindFreeSampleSlot
    if (free < 0)
        return -1;

    shim::VoiceHandle h = device_->allocVoice(); // AIL_allocate_sample_handle
    if (h < 0)                                   // if (!sample_handle) return -1;
        return -1;

    ++o.allocatedSampleCount;                    // ++a1[5]  (+0x14)
    o.sampleSlots[static_cast<std::size_t>(free)] = h; // slot = handle
    return h;
}

// gilde.exe 0x44ab90 — VIBE_Audio_LookupSampleHandleIndex
// (__usercall, eax = lookup(a1=handle@eax))
int DigitalAudio::lookupSampleHandleIndex(shim::VoiceHandle handle) const {
    for (int i = 0; i < outputCount(); ++i) {
        const DigitalOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.open)
            continue;
        // Search only the used range [0, allocatedSampleCount) — wait: the
        // original scans [0, maxSampleHandles (+0x24)). It compares against the
        // capacity field, which is +0x18 here. Match that exactly.
        for (u32 s = 0; s < o.maxSampleHandles; ++s) {
            if (o.sampleSlots[s] == handle)
                return static_cast<int>(s);
        }
    }
    return -1;
}

// gilde.exe 0x44a544 — VIBE_Audio_OpenStream
// (__usercall, eax = open(a1=output@eax, a2=fileHandle@edx, a3=&outStream@ebx))
shim::VoiceHandle DigitalAudio::openStream(int outIndex) {
    if (!driverInstalled_)
        return -1;
    if (outIndex < 0 || outIndex >= outputCount())
        return -1;
    DigitalOutput& o = outputs_[static_cast<std::size_t>(outIndex)];
    if (!o.open)
        return -1;

    // VIBE_Audio_FindFreeStreamSlot @0x44ab2c (same shape as the sample one).
    int free = -1;
    if (o.allocatedSampleCount < o.maxSampleHandles && o.maxSampleHandles != 0) {
        int idx = 0;
        for (u32 i = 0; i < o.maxSampleHandles; ++i) {
            if (o.streamSlots[i] == 0) { free = idx; break; }
            if (++idx >= static_cast<int>(o.maxSampleHandles)) { free = -1; break; }
        }
    }
    if (free < 0)
        return -1;

    shim::VoiceHandle h = device_->allocVoice(); // AIL_open_stream
    if (h < 0)
        return -1;

    o.streamSlots[static_cast<std::size_t>(free)] = h;
    ++o.allocatedSampleCount;                    // ++a1[5]
    return h;
}

// gilde.exe 0x44a74c — VIBE_Audio_StartStream
// (__usercall, eax = start(a1=streamHandle@eax))
int DigitalAudio::startStream(shim::VoiceHandle streamHandle) {
    // if (!dword_62EADC || LookupStreamHandleIndex < 0) return -1;
    if (!driverInstalled_)
        return -1;
    bool found = false;
    for (int i = 0; i < outputCount() && !found; ++i) {
        const DigitalOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.open)
            continue;
        for (u32 s = 0; s < o.maxSampleHandles; ++s) {
            if (o.streamSlots[s] == streamHandle) { found = true; break; }
        }
    }
    if (!found)
        return -1;
    // AIL_start_stream — the device begins playback; nothing to push here.
    return 0;
}

// gilde.exe 0x44a028 — VIBE_Audio_ReleaseSampleHandle
// (__usercall, eax = release(a1=handle@eax))
int DigitalAudio::releaseSampleHandle(shim::VoiceHandle handle) {
    for (int i = 0; i < outputCount(); ++i) {
        DigitalOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.open)
            continue;
        for (u32 s = 0; s < o.maxSampleHandles; ++s) {
            if (o.sampleSlots[s] == handle) {
                o.sampleSlots[s] = 0;
                if (o.allocatedSampleCount > 0)
                    --o.allocatedSampleCount;
                device_->freeVoice(handle); // AIL_release_sample_handle
                return 0;
            }
        }
    }
    return -1;
}

} // namespace guild::audio
