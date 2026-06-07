#include "ai/methodstack.h"

namespace guild::ai {

// gilde.exe 0x4691e4 — VIBE_AiMethodStack_Push  (__usercall, eax = (a1@al))
//   result = dword_B59658 + 1;
//   *((char*)&dword_B59658 + result + 3) = a1;   // == byte_B5965C[count]
//   dword_B59658 = result;  return result;
// The post-increment count is written, so the new id lands at entries_[count_]
// after the increment (1-based store; entry index == old count).
int MethodStack::Push(u8 methodId) {
    if (count_ < kMethodStackCapacity)        // bounds guard (orig is unbounded)
        entries_[count_] = methodId;
    ++count_;
    return count_;
}

// gilde.exe 0x4691fc — VIBE_AiMethodStack_Pop
//   --dword_B59658;
void MethodStack::Pop() {
    if (count_ > 0)                            // orig has no guard; safe-clamp here
        --count_;
}

u8 MethodStack::Peek(int i) const {
    if (i < 0 || i >= count_ || i >= kMethodStackCapacity)
        return 0;
    return entries_[i];
}

u8 MethodStack::Top() const {
    if (count_ <= 0)
        return 0;
    return Peek(count_ - 1);
}

// Mirrors the planner's skip-scan: while (entry[i] != id) ++i; over [0, count).
bool MethodStack::Contains(u8 methodId) const {
    for (int i = 0; i < count_ && i < kMethodStackCapacity; ++i) {
        if (entries_[i] == methodId)
            return true;
    }
    return false;
}

} // namespace guild::ai
