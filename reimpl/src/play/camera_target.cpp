#include "play/camera_target.h"

#include "util/coord.h"   // ConvertX — x87 FRNDINT truncate-toward-zero (0x5c6b08)

namespace guild::play {

// gilde.exe 0x4c0864 — VIBE_Camera_ComputeWorldTarget.
//
// 1:1 with the disasm. The float math is done in x87 80-bit precision in the
// original; each result is reduced to single-precision when stored to a `float`
// local (var_8/var_4/...) before ConvertX, so we mirror that by keeping the
// intermediates as float (matching the fstp [esp+..] spills) then ConvertX +
// (int) truncation. ConvertX = trunc toward zero; the trailing fistp then takes
// the already-integral value (round-to-int is a no-op after frndint).
void ComputeWorldTarget(CameraTargetGlobals& g, u16 mask) {
    if ((mask & 0x40) != 0) {
        // v5 = flt_13FC778 * 0.5 ; v4 = 0.5 * flt_13FC77C
        // (the order in the disasm: fld 0.5; fld viewHalfX; fmul; -> var_C;
        //  fmul viewHalfY -> var_10. Both use the same loaded 0.5.)
        float vX = g.viewHalfX * kCamHalf;            // var_C
        float vY = kCamHalf * g.viewHalfY;            // var_10
        // v6 = vX + flt_13FCF48 (centerX) -> var_8 ; v7 = vY + flt_13FCD14 -> var_4
        float px = vX + g.centerX;                    // var_8
        float py = vY + g.centerY;                    // var_4

        if ((mask & 0x100) != 0) {
            // v2 = var_4 - (1.0 * flt_61E514)  == py - 6.0
            // @0x4c08b8 fsubr var_4: the difference STAYS on the x87 stack
            // (80-bit) into ConvertX + fistp — there is no fstp to a float
            // local. Model the wide intermediate with double; narrowing to
            // float first could round up across an integer boundary and
            // change the truncation.
            double ay = (double)py - (double)kCamOne * (double)kCamOffsetY;
            g.targetX = (i32)util::ConvertX(ay);      // fistp A4
            // remaining st was the dup'd 1.0; fmul flt_61E518 -> 1.0*10.0;
            // fadd var_8 (px) == px + 10.0  (@0x4c08cd, also kept on-stack)
            double ax = (double)kCamOne * (double)kCamOffsetX + (double)px;
            g.targetY = (i32)util::ConvertX(ax);      // fistp A0
        } else {
            float ay = (float)util::ConvertX((double)py); // fistp A4
            g.targetX = (i32)ay;
            float ax = (float)util::ConvertX((double)px); // fistp A0
            g.targetY = (i32)ax;
        }
    } else {
        // A4 = packedX >> 17 (arithmetic: sar 16 then sar 1)
        g.targetX = g.packedX >> 17;
        // A0 = packedY >> 17  (packedY == the misaligned 16.16 read)
        g.targetY = g.packedY >> 17;
    }
}

// gilde.exe 0x5d8ae8 — VIBE_Coord_Push.
//   dword_64A1B8 = a2(edx); dword_64A1BC = a4(ebx); dword_64A1C0 = a3(ecx);
//   dword_64A1B4 = result(eax); return result;
i32 CoordPush(CoordState& s, i32 a, i32 b, i32 c, i32 d) {
    s.b = b;   // dword_64A1B8 = a2 (@edx)
    s.c = c;   // dword_64A1BC = a4 (@ebx)
    s.d = d;   // dword_64A1C0 = a3 (@ecx)
    s.a = a;   // dword_64A1B4 = result (@eax)
    return a;
}

} // namespace guild::play
