#include "util/coord.h"
#include <cmath>

namespace guild::util {

// gilde.exe 0x5c6b08 — VIBE_Coord_ConvertX
//   fstcw cw; set RC=truncate; fldcw; frndint (round st0 to integer); fldcw cw
// Rounding toward zero == std::trunc.
double ConvertX(double x) {
    return std::trunc(x);
}

} // namespace guild::util
