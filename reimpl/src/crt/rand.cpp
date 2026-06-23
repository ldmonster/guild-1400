#include "crt/rand.h"

namespace guild::crt {

namespace {
// Lives in the per-thread CRT data block in the original; global here.
// MSVC CRT initial seed is 1.
u32 g_randState = 1;
} // namespace

// VIBE_Util_GetRandStatePtr @0x5cb8b0 — off_64A90C() + 12 in the original (the per-thread
// CRT data block + 12); a single global here (see header note).
u32* RandStatePtr() {
    return &g_randState;
}

int RandNext() {
    u32* state = RandStatePtr();
    if (!state)
        return 0;
    u32 v = 1103515245u * *state + 12345u; // 32-bit wraparound is intentional
    *state = v;
    return static_cast<int>((v >> 16) & 0x7FFFu);
}

void Srand(u32 seed) {
    *RandStatePtr() = seed;
}

// VIBE_Util_RandSeed @0x5cb8e0 — faithful translation (null-guarded, returns ptr).
u32* RandSeed(u32 seed) {
    u32* ptr = RandStatePtr();   // 0x5cb8e3: call VIBE_Util_GetRandStatePtr
    if (ptr)                     // 0x5cb8ea: test eax,eax / jz
        *ptr = seed;             // 0x5cb8ec: mov [eax], edx
    return ptr;                  // 0x5cb8ef: return eax
}

} // namespace guild::crt
