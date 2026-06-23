#include "render/terrain_uvtable.h"

namespace guild::render {

// gilde.exe 0x5b94cc — VIBE_Render_ComputeFilterWeights, the a1[0..23] writes.
// Exact value flow from the captured decompile:
//   v63 = 1.0 / (double)(unsigned int)dword_64A038;   (stored single)
//   v50 = 1.0 - v63 - v63;  v47 = 0.0;  v48 = v50;  v51 = 0.0;
//   v56 = v50;  v57 = v50;
//   a1[2]=e a1[3]=e  a1[0]=e+v47 a1[1]=e+v48  a1[4]=e+v50 a1[5]=e+v51
//   a1[6]=e+v50 a1[7]=e+v51  a1[8]=e+v56 a1[9]=e+v57  a1[10]=e+v47 a1[11]=e+v48
//   a1[12]=e a1[13]=e  a1[14]=e+v56 a1[15]=e+v57  a1[16]=e+v47 a1[17]=e+v48
//   a1[18]=e a1[19]=e  a1[20]=e+v50 a1[21]=e+v51  a1[22]=e+v56 a1[23]=e+v57
// i.e. with E = e and F = e + (1 - 2e):
void BuildTerrainUvTable(float out[kTerrainUvTableSize], u32 mipTileSize) {
    if (mipTileSize == 0)
        mipTileSize = 1;                                  // guard (engine: >= 4)
    const float e = (float)(1.0 / (double)mipTileSize);   // v63
    const float f = (float)(1.0 - (double)e - (double)e); // v50
    const float ef = e + f;                               // the inset far corner

    out[0]  = e;  out[1]  = ef;   // T0.v0 = (e, e+f)
    out[2]  = e;  out[3]  = e;    // T0.v1 = (e, e)
    out[4]  = ef; out[5]  = e;    // T0.v2 = (e+f, e)
    out[6]  = ef; out[7]  = e;    // T1.v0 = (e+f, e)
    out[8]  = ef; out[9]  = ef;   // T1.v1 = (e+f, e+f)
    out[10] = e;  out[11] = ef;   // T1.v2 = (e, e+f)
    out[12] = e;  out[13] = e;    // T2.v0 = (e, e)
    out[14] = ef; out[15] = ef;   // T2.v1 = (e+f, e+f)
    out[16] = e;  out[17] = ef;   // T2.v2 = (e, e+f)
    out[18] = e;  out[19] = e;    // T3.v0 = (e, e)
    out[20] = ef; out[21] = e;    // T3.v1 = (e+f, e)
    out[22] = ef; out[23] = ef;   // T3.v2 = (e+f, e+f)
}

// gilde.exe 0x5c1f78..0x5c1f95 — the per-cell sub-texture id selector.
//   if (cellFlag & 0x40):  al = byte_13DCE58[(quadIdx & 0xFF) + base] & 0x3F
//   else:                  al = 0
// `subTexSrc` is byte_13DCE58 + base (the runtime per-cell table; the static image
// is all-zero, get_bytes 0x13DCE58 verified -> the result is always 0 unless the
// runtime writer ran). The (quadIdx & 0xFF) masks the LOW byte of the linear quad
// index exactly as the engine (mov eax, edi; and eax, 0FFh).
u32 TerrainSubTexId(u8 cellFlag, const u8* subTexSrc, i32 quadIdx) {
    if ((cellFlag & 0x40) == 0)
        return 0;
    if (subTexSrc == nullptr)
        return 0;                                     // the all-zero shipped table
    return (u32)(subTexSrc[(u32)quadIdx & 0xFF] & 0x3F);
}

// gilde.exe 0x5c1ff5 — poly+0x10 = &flt_13FE540[subTexId*0x60] (tri0 = floats 0..5).
void TerrainQuadUvT0(float out[kTriUvFloats], const float* uvTable, u32 subTexId) {
    const float* rec = uvTable + TerrainUvBaseIndex(subTexId);
    for (int i = 0; i < kTriUvFloats; ++i) out[i] = rec[i];
}

// gilde.exe 0x5c2015 — poly+0x38 = (that + 0x18) (tri1 = floats 6..11).
void TerrainQuadUvT1(float out[kTriUvFloats], const float* uvTable, u32 subTexId) {
    const float* rec = uvTable + TerrainUvBaseIndex(subTexId) + kTriUvFloats;
    for (int i = 0; i < kTriUvFloats; ++i) out[i] = rec[i];
}

// gilde.exe 0x5bf22c — seam-UV midpoint blend (flt_628B48 = 0.5). Two diagonal
// cases, decoded exactly from the disassembly (see header). The x87 sequence is
// fld/fadd/fmul: the products are computed in extended precision then stored single
// (fstp dword); reproduced with float arithmetic (the inputs are the table's exact
// single values, the 0.5 multiply is exact, so the single-store result matches).
void TerrainSeamBlendUv(float rec[kTriUvFloats], bool diagTLBR) {
    if (diagTLBR) {
        // @0x5bfd27: average verts 1 & 2 -> vert 2's slot (indices 4,5).
        rec[4] = (rec[2] + rec[4]) * kSeamUvBlend;
        rec[5] = (rec[3] + rec[5]) * kSeamUvBlend;
    } else {
        // @0x5c2348: average verts 0 & 1 -> vert 0's slot (indices 0,1).
        rec[0] = (rec[0] + rec[2]) * kSeamUvBlend;
        rec[1] = (rec[1] + rec[3]) * kSeamUvBlend;
    }
}

} // namespace guild::render
