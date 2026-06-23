#pragma once
#include "guild/common/types.h"

// camera_recon5_flight — VIBE_Camera_Flight (namespace guild::render).
//
//   0x43f4d8 VIBE_Camera_Flight (__usercall eax=a1@eax, edx=a2@edx)
//            The base "camera flight" script command (sibling of the
//            CmdCameraFlight / Timed shims reconstructed in camera_recon.{h,cpp}
//            at 0x43f528 / 0x43f5dc, which divide by 17 / 14 with selector 6).
//            This variant calls VIBE_Render_DrawTextLabels3D(a1[0] / 17, 2,
//            a2[0]); on failure it logs the "CameraFlight(): Could not find one
//            or few dummies" error via VIBE_Script_ReportError. Always returns 1.
//
// DrawTextLabels3D, ReportError and the script-context global (dword_62E8A8) are
// live leaves -> routed through a small context/hook. The integer division by 17
// (signed: `sar edx,1Fh / idiv 11h`), the selector constant 2, and the
// always-return-1 contract ARE reconstructed 1:1.

namespace guild::render {

using namespace guild; // i32 etc.

struct CameraFlightCtx {
    // VIBE_Render_DrawTextLabels3D(a1[0]/17, 2, a2[0]) -> nonzero on success.
    bool drawOk = true;
    // captured side effects:
    i32  drawArg0 = 0;          // a1[0] / 17 (the divided value passed)
    i32  drawArg2 = 0;          // a2[0]
    bool drawCalled = false;
    bool reportedError = false; // VIBE_Script_ReportError fired (DrawTextLabels3D==0)
};

// gilde.exe 0x43f4d8 — VIBE_Camera_Flight.
// a1Val == *a1 (first dword of the camera-flight param block), a2Val == *a2.
// Returns 1 (the original's invariant return value).
i32 Camera_Flight(i32 a1Val, i32 a2Val, CameraFlightCtx& ctx);

} // namespace guild::render
