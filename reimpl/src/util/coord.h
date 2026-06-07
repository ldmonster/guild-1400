#pragma once

namespace guild::util {

// VIBE_Coord_ConvertX @0x5c6b08 (1101 xrefs) / VIBE_Coord_ConvertY.
// The original sets the x87 rounding-control field to "toward zero", executes
// FRNDINT on st0, then restores the control word — i.e. it truncates the value
// currently on the FPU stack toward zero, leaving an integral double.
// Modeled here as an explicit double->truncated-double helper.
double ConvertX(double x);

} // namespace guild::util
