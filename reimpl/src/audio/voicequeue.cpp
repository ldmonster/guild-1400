#include "audio/voicequeue.h"

namespace guild::audio {

bool VoiceQueue::enqueue(VoiceSlot* voice, int delay, const void* pcm,
                         std::size_t bytes, int sampleRate,
                         const std::string& sampleName) {
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
    node.sampleName = sampleName; // voice->sample->name (read by ProcessNext)
    queue_.push_back(node); // append at tail (walk +0x0C to end, or set head)
    return true;
}

namespace {
// loc_5CB930 is strstr(haystack, needle): the original passes the sample's name
// string (voice->sample + 4) as haystack and a literal needle. Returns true iff
// `needle` occurs in `name` (empty needle => true, matching libc strstr).
bool NameContains(const std::string& name, const char* needle) {
    return name.find(needle) != std::string::npos;
}
} // namespace

void VoiceQueue::processNext(int tick) {
    // VIBE_VoiceQueue_ProcessNext @0x57eff0.
    if (paused_)             // dword_642014 != 0 -> jnz return
        return;
    if (queue_.empty())      // dword_6420F8 == 0 -> jz return
        return;

    VoiceQueueNode& head = queue_.front();

    // 0x57f024..0x57f040: take the "started" branch when head.started, OR when
    // (u32)(tick - lastStart) <= (u32)head.delay (jbe loc_57F0B9). Once started,
    // the original's 64-bit compare RHS becomes (started<<32 | delay) which is
    // always >= the zero-extended elapsed, so the started case always lands here.
    bool elapsedWithin = static_cast<unsigned>(tick - lastStart_)
                         <= static_cast<unsigned>(head.delay);
    if (head.started || elapsedWithin) {
        // loc_57F0B9: only act when the head has actually started; within-window
        // not-yet-started just returns (cmp [eax+8],0 / jz loc_57F067).
        if (!head.started)
            return;
        // Still being reported as playing by the device -> nothing to do.
        if (voices_->voiceIsPlaying(head.voice))   // jnz loc_57F067
            return;

        // Head finished: clear its loop flag, free it, advance, stamp lastStart.
        VoicePool::setLoopFlag(head.voice, false);
        queue_.erase(queue_.begin());
        lastStart_ = tick;                  // dword_6420FC = v2

        if (queue_.empty())                 // ecx == 0 -> return (NO un-duck here)
            return;
        VoiceQueueNode& next = queue_.front();
        if (!next.voice)                    // [ecx] == 0 -> return
            return;
        if (!next.pcm)                      // next.voice->sample == 0 -> return
            return;
        if (!ducked_)                       // dword_642028 == 0 -> return
            return;
        // strstr(next.sample.name, "MausclickLinks"): only then retire the next
        // line and restore ambient.
        if (!NameContains(next.sampleName, "MausclickLinks"))
            return;

        VoicePool::setLoopFlag(next.voice, false);
        queue_.erase(queue_.begin());
        ducking_.apply(false);              // SetFadeVolume(1.0, 2000) (ambient on)
        ducked_ = false;                    // dword_642028 = 0
        return;
    }

    // 0x57f046..: not started and the delay window has passed -> start the line.
    if (head.voice && head.pcm) {           // head.voice && head.voice->sample
        // strstr(head.sample.name, "Fanfare"): such lines duck ambient on start.
        if (NameContains(head.sampleName, "Fanfare")) {
            if (!ducked_) {
                ducking_.apply(true);       // SetFadeVolume(0.0, 2000)
                ducked_ = true;
            }
        }
        voices_->startVoice(head.voice, head.pcm, head.pcmBytes,
                            head.sampleRate, /*loops=*/0);
    }
    lastStart_ = tick;   // dword_6420FC = v2
    head.started = true; // head[+8] = 1
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
