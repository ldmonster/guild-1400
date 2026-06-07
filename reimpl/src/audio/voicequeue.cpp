#include "audio/voicequeue.h"

namespace guild::audio {

bool VoiceQueue::enqueue(VoiceSlot* voice, int delay, const void* pcm,
                         std::size_t bytes, int sampleRate) {
    // VIBE_VoiceQueue_Enqueue @0x57ef28.
    if (!voice)
        return 0; // original: !a1 => return 0
    VoicePool::setLoopFlag(voice, true); // VIBE_Sound_SetLoopFlag(a1, 1)
    VoiceQueueNode node;
    node.voice = voice;     // +0x00
    node.delay = delay;     // +0x04
    node.started = false;   // zeroed node
    node.pcm = pcm;
    node.pcmBytes = bytes;
    node.sampleRate = sampleRate;
    queue_.push_back(node); // append at tail (walk +0x0C to end, or set head)
    return true;
}

void VoiceQueue::processNext(int tick) {
    // VIBE_VoiceQueue_ProcessNext @0x57eff0.
    if (paused_)             // dword_642014
        return;
    if (queue_.empty())      // !dword_6420F8
        return;

    VoiceQueueNode& head = queue_.front();

    // Branch test: started || (tick - lastStart) <= delay.
    bool elapsedWithin = static_cast<unsigned>(tick - lastStart_)
                         <= static_cast<unsigned>(head.delay);
    if (head.started || elapsedWithin) {
        if (head.started) {
            // Already playing: when the device says it finished, free + advance.
            if (!voices_->voiceIsPlaying(head.voice)) {
                VoicePool::setLoopFlag(head.voice, false);
                queue_.erase(queue_.begin());
                lastStart_ = tick; // dword_6420FC = v3

                // The original immediately retires a following empty/placeholder
                // line and, if ambient was ducked, restores it (SetFadeVolume 1).
                if (!queue_.empty() && ducked_) {
                    if (!queue_.front().pcm) {
                        VoicePool::setLoopFlag(queue_.front().voice, false);
                        queue_.erase(queue_.begin());
                    }
                    ducking_.apply(false); // restore ambient
                    ducked_ = false;
                }
            }
        }
        // else: started==false but still within delay window — wait, do nothing.
    } else {
        // Not started and the delay window has passed: start the line now.
        if (head.pcm) {
            if (!ducked_) {
                ducking_.apply(true); // duck ambient to 0
                ducked_ = true;
            }
            voices_->startVoice(head.voice, head.pcm, head.pcmBytes,
                                head.sampleRate, /*loops=*/0);
        }
        lastStart_ = tick;   // dword_6420FC = v3
        head.started = true; // head[+8] = 1
    }
}

void VoiceQueue::flushAll() {
    // VIBE_VoiceQueue_FlushAll @0x57ee40.
    if (ducked_) {
        ducking_.apply(false); // SetFadeVolume(1.0, 2000)
        ducked_ = false;
    }
    for (auto& n : queue_)
        VoicePool::setLoopFlag(n.voice, false);
    queue_.clear();
}

} // namespace guild::audio
