#include "util/math_random.h"
#include "crt/rand.h"

namespace guild::util {

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo (__usercall, eax = fn(n@ax))
//   if (n) return (int)RandNext() % n;  return 0;
int RandomModulo(u16 n) {
    if (n)
        return crt::RandNext() % n;
    return 0;
}

} // namespace guild::util
