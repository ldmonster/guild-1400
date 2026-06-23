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
//   0x46797c: mov eax, 2; call RandomModulo  -> the modulus is HARDCODED to 2.
//   return (u16)RandomModulo(2) != 0;  (one RandNext draw, true with prob 1/2)
bool RandomBoolCheck() {
    return static_cast<u16>(RandomModulo(2)) != 0;
}

// gilde.exe 0x467994 — VIBE_AiMethod_RandomValue
//   0x467994: mov eax, 7; call RandomModulo  -> the modulus is HARDCODED to 7.
//   return (u16)RandomModulo(7);  (one RandNext draw, result in 0..6)
int RandomValue() {
    return static_cast<u16>(RandomModulo(7));
}

// gilde.exe 0x47b9c0 — VIBE_AiAction_AlwaysAllow
int AlwaysAllow() {
    return 1;
}

// gilde.exe 0x47b9c8 — VIBE_AiAction_CheckSameFaction
bool CheckSameFaction(u32 targetFlags, u32 gameTimeLo, u32 gameTimeHi) {
    // Short-circuit && — the RandomModulo(4) roll only happens if both equalities
    // hold, exactly as in the original (it draws RNG last).
    //
    // 0x47b9da/0x47b9dd: the low dword of qword_13CE852 is sign-extended (sar 1Fh)
    // and divided with `idiv` -> SIGNED `% 4`. The (flags & 3) side is 0..3, so this
    // only matches when gameTimeLo % 4 (signed) is in 0..3, i.e. for negative
    // gameTimeLo the remainder can be 0/-1/-2/-3 and never equals (flags & 3).
    // Model the signed remainder exactly.
    if (static_cast<int>(targetFlags & 3u) != static_cast<int>(gameTimeLo) % 4)
        return false;
    // 0x47b9e8: gameTimeHi is read as WORD2(qword) via `xor edx,edx; mov dx,...`
    // (zero-extended 16-bit), so its `% 8` is effectively unsigned over 0..0xFFFF.
    if ((targetFlags & 7u) != ((gameTimeHi & 0xFFFFu) % 8u))
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
