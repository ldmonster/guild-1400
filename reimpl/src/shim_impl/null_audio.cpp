#include "shim_impl/null_audio.h"

namespace guild::shim {

bool NullAudioDevice::init(int voices, int /*channels*/, int /*sampleRate*/) {
    if (voices <= 0)
        return false;
    max_voices_ = voices;
    next_voice_ = 0;
    alloc_count_ = 0;
    free_count_ = 0;
    master_volume_ = 127;
    inited_ = true;
    calls_.clear();
    return true;
}

void NullAudioDevice::shutdown() {
    inited_ = false;
}

VoiceHandle NullAudioDevice::allocVoice() {
    if (!inited_ || (alloc_count_ - free_count_) >= max_voices_)
        return -1;
    ++alloc_count_;
    return static_cast<VoiceHandle>(next_voice_++);
}

void NullAudioDevice::freeVoice(VoiceHandle v) {
    ++free_count_;
    calls_.push_back(Call{CallKind::Free, v, 0, 0, 0, 0});
}

void NullAudioDevice::playSample(VoiceHandle v, const void* /*pcm*/, std::size_t bytes,
                                 int sampleRate, int loops) {
    calls_.push_back(Call{CallKind::Play, v, bytes, sampleRate, loops, 0});
}

void NullAudioDevice::stop(VoiceHandle v) {
    calls_.push_back(Call{CallKind::Stop, v, 0, 0, 0, 0});
}

void NullAudioDevice::setVolume(VoiceHandle v, int vol) {
    calls_.push_back(Call{CallKind::SetVolume, v, 0, 0, 0, vol});
}

void NullAudioDevice::setPan(VoiceHandle v, int pan) {
    calls_.push_back(Call{CallKind::SetPan, v, 0, 0, 0, pan});
}

void NullAudioDevice::setMasterVolume(int vol) {
    master_volume_ = vol;
}

} // namespace guild::shim
