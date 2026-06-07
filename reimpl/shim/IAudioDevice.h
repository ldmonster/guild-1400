#pragma once
// Host audio boundary. The original used Miles Sound System (mss32.dll, _AIL_*).
// guild::audio is a facade over this interface; a backend may use OpenAL/miniaudio.
#include <cstdint>

namespace guild::shim {

using VoiceHandle = std::int32_t; // -1 = invalid

class IAudioDevice {
public:
    virtual ~IAudioDevice() = default;
    virtual bool init(int voices, int channels, int sampleRate) = 0;
    virtual void shutdown() = 0;

    virtual VoiceHandle allocVoice() = 0;
    virtual void freeVoice(VoiceHandle) = 0;
    virtual void playSample(VoiceHandle, const void* pcm, std::size_t bytes,
                            int sampleRate, int loops) = 0;
    virtual void stop(VoiceHandle) = 0;
    virtual void setVolume(VoiceHandle, int vol) = 0;   // 0..127
    virtual void setPan(VoiceHandle, int pan) = 0;      // 0..127, 64 = center
    virtual void setMasterVolume(int vol) = 0;
};

} // namespace guild::shim
