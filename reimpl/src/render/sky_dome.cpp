#include "render/sky_dome.h"

#include "util/math.h"   // VectorLerp, VectorNormalize, Atan2 (1:1 engine math)

namespace guild::render {

// gilde.exe 0x5ef980 — VIBE_Sky_BuildDomeMesh.
//
// Faithful translation of the disasm. Register/stack mapping:
//   v21 (var_C)  : V accumulator, init (float)rectYmin, += vStep per row
//   v20 (var_10) : uStep = (rectXmax-rectXmin) * flt_62C148   (1/7)
//   v16 (var_20) : vStep = (rectYmax-rectYmin) * flt_62C14C   (0.2)
//   v22 (var_8)  : U accumulator, reset to (float)rectXmin per row, += uStep/col
//   tRow         : (float)row * flt_62C14C
//   tCol         : (float)col * flt_62C148
//   v13 (var_50) : VectorLerp(c30, c10, tRow)   (row's start edge)
//   v14 (var_40) : VectorLerp(c20, c00, tRow)   (row's end   edge)
//   v15 (var_30) : VectorLerp(v13, v14, tCol) -> Normalize -> atan2(x,z)
//
// The original computes v23=(rectYmax-rectYmin) as a float very early (var_4)
// but never uses that exact temporary for geometry — vStep is the value that
// feeds the loop. We reproduce the observable writes only.
int BuildDome(const SkyDomeInputs& in, bool rebuild, SkyDomeVertex* out) {
    // if (!result) return; — caller guarantees non-null `out`.
    // *(result+0xAA8) = -1 happens at the caller's sky object (cache invalidate);
    // not modeled here (no live object). if (!a2) early-out -> no geometry.
    if (!rebuild)
        return 0;

    // uStep = (rectXmax - rectXmin) * (1/7) ; the original promotes the integer
    // difference via fild (signed) before the multiply.
    const float uStep = (float)((double)(in.rectXmax - in.rectXmin) * (double)kSkyColStep);
    // vStep = (rectYmax - rectYmin) * 0.2
    const float vStep = (float)((double)(in.rectYmax - in.rectYmin) * (double)kSkyRowStep);
    // V accumulator initial value = (float)rectYmin (fild dword_13ECE5C).
    float vAcc = (float)in.rectYmin;

    // Mutable copies of the corner inputs (VectorLerp takes float* args).
    float c30[3] = {in.c30[0], in.c30[1], in.c30[2]};
    float c10[3] = {in.c10[0], in.c10[1], in.c10[2]};
    float c20[3] = {in.c20[0], in.c20[1], in.c20[2]};
    float c00[3] = {in.c00[0], in.c00[1], in.c00[2]};

    int written = 0;
    for (int row = 0; row < kSkyDomeRows; ++row) {           // v2 < 6
        const float tRow = (float)((double)row * (double)kSkyRowStep);

        float edgeStart[3];  // v13 = VectorLerp(c30, c10, tRow)
        float edgeEnd[3];    // v14 = VectorLerp(c20, c00, tRow)
        util::VectorLerp(c30, c10, tRow, edgeStart);
        util::VectorLerp(c20, c00, tRow, edgeEnd);

        // U accumulator resets to (float)rectXmin at the start of every row
        // (fild dword_13ECE58 -> var_8 inside the outer loop).
        float uAcc = (float)in.rectXmin;

        for (int col = 0; col < kSkyDomeCols; ++col) {        // v3 < 8
            const float tCol = (float)((double)col * (double)kSkyColStep);

            float dir[3];     // v15 = VectorLerp(edgeStart, edgeEnd, tCol)
            util::VectorLerp(edgeStart, edgeEnd, tCol, dir);
            util::VectorNormalize(dir);
            // longitude = atan2(dir.x, dir.z)  (fld var_28(=dir[2]); fld var_30(=dir[0]); Atan2)
            const float longitude = (float)util::Atan2((double)dir[0], (double)dir[2]);

            SkyDomeVertex& vtx = out[written++];
            vtx.longitude = longitude;          // *(esi+0x5FC)
            vtx.one0 = 1.0f;                     // [ecx-0x18] = 3F800000
            vtx.one1 = 1.0f;                     // [ecx-0x14] = 3F800000
            vtx.u = uAcc;                        // [ecx-0x20] = current U
            vtx.v = vAcc;                        // [ecx-0x1C] = current V (row const)

            uAcc = uAcc + uStep;                 // v22 = v11 (U += uStep)
        }

        vAcc = vAcc + vStep;                     // v21 = v21 + v16
    }
    return written;                              // 48
}

} // namespace guild::render
