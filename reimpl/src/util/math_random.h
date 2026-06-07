#pragma once
#include "guild/common/types.h"

namespace guild::util {

// VIBE_Math_RandomModulo @0x58b89c (344 callers); byte-identical twins:
//   VIBE_Util_RandomModulo @0x404b68, VIBE_Util_RandModulo @0x43c65c.
// Returns RandNext() % n, or 0 when n == 0.
int RandomModulo(u16 n);

} // namespace guild::util
