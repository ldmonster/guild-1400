#pragma once
// guild::audio — voice (speech) queue of gilde.exe (f3snd snd queue).
//
// A singly-linked FIFO of pending voice lines. Each tick, the head is examined:
// if it hasn't started yet and its scheduled delay has elapsed it is played;
// once started, when the device reports it finished it is freed and the next
// line advances. Speech also ducks ambient music while a line plays.
//
// Recovered from:
//   VIBE_VoiceQueue_Enqueue     @0x57ef28  (16-byte node, append at tail)
//   VIBE_VoiceQueue_ProcessNext @0x57eff0  (head state machine)
//   VIBE_VoiceQueue_FlushAll    @0x57ee40
//   VIBE_VoiceQueue_SetPauseFlag@0x57efe8
#include "guild/common/types.h"
#include "audio/voice.h"
#include <vector>

namespace guild::audio {

// 16-byte queue node (VIBE_Memory_AllocDebug(0x10) in Enqueue).
//   +0x00 voice slot handle
//   +0x04 delay threshold (13*tick units; play once elapsed)
//   +0x08 started flag
//   +0x0C next node ptr
struct VoiceQueueNode {
    VoiceSlot* voice = nullptr; // +0x00
    int delay = 0;              // +0x04
    bool started = false;       // +0x08
    const void* pcm = nullptr;  // PCM to play when the node starts
    std::size_t pcmBytes = 0;
    int sampleRate = 44100;
};

// The original ducks ambient music to 0 while speech plays (SetFadeVolume 0/1).
// We surface that intent via a callback the host (music facade) can wire.
struct VoiceQueueDucking {
    // duck(true)  => fade ambient to 0 over 2000ms while a line plays.
    // duck(false) => restore ambient to 1 over 2000ms when the queue empties.
    void (*duck)(void* ctx, bool on) = nullptr;
    void* ctx = nullptr;
    void apply(bool on) const { if (duck) duck(ctx, on); }
};

class VoiceQueue {
public:
    explicit VoiceQueue(VoicePool* voices) : voices_(voices) {}

    void setDucking(const VoiceQueueDucking& d) { ducking_ = d; }

    // VIBE_VoiceQueue_SetPauseFlag @0x57efe8 — when set, ProcessNext is a no-op.
    void setPaused(bool paused) { paused_ = paused; }

    // VIBE_VoiceQueue_Enqueue @0x57ef28 — append a line to the tail, marking its
    // voice looping (so the recycler won't steal it before it plays). Returns
    // false when `voice` is null (original returns 0).
    bool enqueue(VoiceSlot* voice, int delay, const void* pcm, std::size_t bytes,
                 int sampleRate);

    // VIBE_VoiceQueue_ProcessNext @0x57eff0 — advance the head one step against
    // the current tick (in the original tick = 13 * frameCounter).
    void processNext(int tick);

    // VIBE_VoiceQueue_FlushAll @0x57ee40 — drop every queued line, clearing loop
    // flags, and restore ambient music if it was ducked.
    void flushAll();

    bool empty() const { return queue_.empty(); }
    std::size_t size() const { return queue_.size(); }
    VoiceQueueNode* head() { return queue_.empty() ? nullptr : &queue_.front(); }

private:
    VoicePool* voices_;
    std::vector<VoiceQueueNode> queue_; // front == list head (dword_6420F8)
    VoiceQueueDucking ducking_;
    bool paused_ = false;   // dword_642014
    bool ducked_ = false;   // dword_642028 (ambient currently ducked)
    int  lastStart_ = 0;    // dword_6420FC (tick the head was started)
};

} // namespace guild::audio
