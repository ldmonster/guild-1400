#pragma once
// Per-person behavior method stack for the Guild AI planner (gilde.exe).
//   AiMethodStack_Push   0x4691e4
//   AiMethodStack_Pop    0x4691fc
//
// The original is a single file-global stack: dword_B59658 holds the entry count
// and the method-id bytes live immediately after it (byte_B5965C[0..count-1]).
// The recursive planner (SelectBestRecursive) peeks the stack to skip methods
// already in progress. We model it as one instance so the codec is testable in
// isolation; one live instance reproduces the original's single global state.
#include "guild/common/types.h"
#include "ai/types.h"

namespace guild::ai {

class MethodStack {
public:
    MethodStack() = default;

    // VIBE_AiMethodStack_Push @0x4691e4 — push a method id. The original does an
    // unbounded write (count += 1; byte_B5965C[count-1] = id); we clamp at the
    // modeled capacity so the test harness never overruns. Returns the new count
    // (the original returns the post-increment count in eax).
    int Push(u8 methodId);

    // VIBE_AiMethodStack_Pop @0x4691fc — decrement the count (no clear). Mirrors
    // the original's bare --dword_B59658, including its lack of an empty guard;
    // we keep the count from going negative for safety in the reimpl.
    void Pop();

    // Peek the entry at depth i (0 == bottom). The planner reads these via
    // (dword at &dword_B59658 + i + 1) >> 24 == byte_B5965C[i].
    u8 Peek(int i) const;

    // Top-of-stack entry (last pushed). Returns 0 when empty.
    u8 Top() const;

    // True if `methodId` is anywhere on the stack (the planner's "already in
    // progress, skip it" test).
    bool Contains(u8 methodId) const;

    int  count() const { return count_; }   // dword_B59658
    bool empty() const { return count_ == 0; }
    void Clear() { count_ = 0; }

private:
    int count_ = 0;                          // dword_B59658
    u8  entries_[kMethodStackCapacity] = {}; // byte_B5965C[]
};

} // namespace guild::ai
