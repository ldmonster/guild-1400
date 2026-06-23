#include "render/camera_recon5_flight.h"

namespace guild::render {

// ===========================================================================
// gilde.exe 0x43f4d8 — VIBE_Camera_Flight
//
//   mov eax, [a1]          ; numerator = a1[0]
//   mov ebx, 11h           ; divisor = 17
//   sar edx, 1Fh / idiv ebx ; signed division a1[0] / 17
//   push a2[0]; push selector=2; push (a1[0]/17)
//   call VIBE_Render_DrawTextLabels3D
//   test eax,eax / jz -> ReportError("CameraFlight(): Could not find ...")
//   eax = 1 ; ret
// ===========================================================================
i32 Camera_Flight(i32 a1Val, i32 a2Val, CameraFlightCtx& ctx) {
    // Signed integer division by 17 (matches sar/idiv: C++ truncates toward zero,
    // identical to x86 idiv for this operand pair).
    i32 arg0 = a1Val / 17;

    ctx.drawArg0 = arg0;
    ctx.drawArg2 = a2Val;
    ctx.drawCalled = true;

    // VIBE_Render_DrawTextLabels3D(arg0, 2, a2Val) -> ctx.drawOk.
    if (!ctx.drawOk) {
        // VIBE_Script_ReportError(dword_62E8A8, *(dword_62E8A8+152), aCameraflightCo)
        ctx.reportedError = true;
    }
    return 1;
}

} // namespace guild::render
