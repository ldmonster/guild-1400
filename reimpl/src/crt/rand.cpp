#include "crt/rand.h"

namespace guild::crt {

namespace {
// Lives in the per-thread CRT data block in the original; global here.
// MSVC CRT initial seed is 1.
u32 g_randState = 1;
} // namespace

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

} // namespace guild::crt
