#pragma once
// Portable default backend for IAudioDevice — produces no sound but records every
// call (voice alloc/free/play/stop/volume/pan/master) for inspection by tests.
// NOT a translation of gilde.exe; a clean implementation of the interface contract.
#include "shim/IAudioDevice.h"
#include <cstddef>
#include <vector>

namespace guild::shim {

class NullAudioDevice : public IAudioDevice {
public:
    enum class CallKind { Play, Stop, SetVolume, SetPan, Free };

    struct Call {
        CallKind kind;
        VoiceHandle voice = -1;
        std::size_t bytes = 0;   // Play: PCM size
        int sampleRate = 0;      // Play
        int loops = 0;           // Play
        int value = 0;           // SetVolume vol / SetPan pan
    };

    // IAudioDevice
    bool init(int voices, int channels, int sampleRate) override;
    void shutdown() override;
    VoiceHandle allocVoice() override;
    void freeVoice(VoiceHandle) override;
    void playSample(VoiceHandle, const void* pcm, std::size_t bytes,
                    int sampleRate, int loops) override;
    void stop(VoiceHandle) override;
    void setVolume(VoiceHandle, int vol) override;
    void setPan(VoiceHandle, int pan) override;
    void setMasterVolume(int vol) override;

    // --- test/inspection hooks (not part of the interface) ---
    const std::vector<Call>& calls() const { return calls_; }
    int masterVolume() const { return master_volume_; }
    int liveVoices() const { return alloc_count_ - free_count_; }
    void clearCalls() { calls_.clear(); }

private:
    std::vector<Call> calls_;
    int max_voices_ = 0;
    int next_voice_ = 0;
    int alloc_count_ = 0;
    int free_count_ = 0;
    int master_volume_ = 127;
    bool inited_ = false;
};

} // namespace guild::shim
