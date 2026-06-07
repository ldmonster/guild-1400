#include "ai/method.h"

#include "crt/rand.h"

namespace guild::ai {

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo
int RandomModulo(u16 n) {
    if (n)
        return static_cast<int>(crt::RandNext()) % n; // RandNext result >= 0
    return 0;
}

// gilde.exe 0x46797c — VIBE_AiMethod_RandomBoolCheck
bool RandomBoolCheck(u16 n) {
    return RandomModulo(n) != 0;
}

// gilde.exe 0x467994 — VIBE_AiMethod_RandomValue
int RandomValue(u16 n) {
    return static_cast<u16>(RandomModulo(n));
}

// gilde.exe 0x47b9c0 — VIBE_AiAction_AlwaysAllow
int AlwaysAllow() {
    return 1;
}

// gilde.exe 0x47b9c8 — VIBE_AiAction_CheckSameFaction
bool CheckSameFaction(u32 targetFlags, u32 gameTimeLo, u32 gameTimeHi) {
    // Short-circuit && — the RandomModulo(4) roll only happens if both equalities
    // hold, exactly as in the original (it draws RNG last).
    if ((targetFlags & 3u) != (gameTimeLo % 4u))
        return false;
    if ((targetFlags & 7u) != (gameTimeHi % 8u))
        return false;
    return !static_cast<u16>(RandomModulo(4));
}

// gilde.exe 0x47be3c — VIBE_AiAction_PrepareGroupMember
bool PrepareGroupMember(int capacity, int activeCrimes, int* outGroupSize) {
    if (capacity < 2)
        return false;
    if (activeCrimes <= 0)
        return false;
    int half = capacity / 2;
    int size = (half >= activeCrimes) ? activeCrimes : half;
    if (outGroupSize)
        *outGroupSize = size;
    return true;
}

} // namespace guild::ai
