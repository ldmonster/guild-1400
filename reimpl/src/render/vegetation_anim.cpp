#include "render/vegetation_anim.h"

#include <cmath>  // std::lrint — the bare-fistp round-nearest-even convention

// =============================================================================
// guild::render — VEGETATION PER-FRAME ANIMATION (light cache).  See the header
// for the full provenance + the rule-8 finding (no wind sway exists in the
// original; the per-frame veg update is a dynamic relight).
// =============================================================================
namespace guild::render {

// gilde.exe 0x5c9d04 loc_5C9D7B/8D/B3 — i8 unpack scale/bias.
//   v = ((double)(__int16)byte + flt_628CDC) * flt_628CD8.
// The source byte is read as `unsigned __int8` then widened to (__int16): values
// 0..255 stay 0..255 (no sign flip), so the centring is purely via the -128 bias.
float UnpackVertexComponent(u8 packedByte) {
    // (__int16)(unsigned __int8) — the byte is zero-extended to 16 bits.
    i16 widened = static_cast<i16>(static_cast<u8>(packedByte));
    // The decompile casts the int to (double), adds the float bias, multiplies by
    // the float scale (both promoted to double), then narrows to the float store.
    double v = (static_cast<double>(widened) + static_cast<double>(kPackedVertexBias))
               * static_cast<double>(kPackedVertexScale);
    return static_cast<float>(v);
}

void UnpackVertexPosition(const u8 packed[3], float outModel[3]) {
    outModel[0] = UnpackVertexComponent(packed[0]);
    outModel[1] = UnpackVertexComponent(packed[1]);
    outModel[2] = UnpackVertexComponent(packed[2]);
}

// gilde.exe 0x5c8560 loc_5C87FE..5C8865 — UNLIT arm (byte_649D70 == 0).
//   v26 = (int)(v30 * flt_628C98 + v29 * flt_628C9C + v31 * flt_628CA0);
//   v21 = v26;  if (v26 >= 0xFF) v21 = -1;  *(u8*)(+70) = v21;
// where v29 = rgb[+48] (R wt), v30 = rgb[+52] (G wt), v31 = rgb[+56] (B wt).
// The compare is on the unclamped (unsigned) int; >= 255 saturates to 0xFF.
//
// ROUNDING (verified disasm @0x5c8844): the `(int)(...)` is a BARE `fistp`
// (NO VIBE_Coord_ConvertX truncation before it), so under the default x87
// control word it rounds to NEAREST-EVEN, not toward zero. std::lrint honours
// the FE rounding mode (round-to-nearest-even by default), matching fistp 1:1.
// (e.g. 200*(.59+.30+.11) ≈ 199.99999702 rounds to 200, NOT 199; 41.9 -> 42.)
u8 ComputeVertexIntensityUnlit(const float rgb[3]) {
    // Operand order exactly as emitted: G-term first, then R-term, then B-term.
    // The original evaluates this on the x87 stack; we accumulate in double.
    double sum = static_cast<double>(rgb[1]) * static_cast<double>(kLumWeightG)
               + static_cast<double>(rgb[0]) * static_cast<double>(kLumWeightR)
               + static_cast<double>(rgb[2]) * static_cast<double>(kLumWeightB);
    // v26 is `unsigned int v26` assigned the fistp result; v21 = (char)v26.
    unsigned int n = static_cast<unsigned int>(static_cast<int>(std::lrint(sum)));
    if (n >= 0xFFu)
        return 0xFFu;            // v21 = -1  (==0xFF as a byte)
    return static_cast<u8>(n);
}

// gilde.exe 0x5c8560 loc_5C86D7..5C878F — LIT arm (byte_649D70 != 0).
//   v29=rgb[+48]; v30=rgb[+52]; v31=rgb[+56];
//   v28 = max(v29, v30);  v15 = max(v28, v31);     (the max of the three)
//   if (v15 > dbl_628CAC /*255.0*/) { v16 = flt_628CA4/*255*/ / v15;
//       v29*=v16; v30*=v16; v31*=v16; }
//   *(u8*)(+70)=(int)v29; *(u8*)(+69)=(int)v30; *(u8*)(+68)=(int)v31;
// outBGR[] is the in-memory byte order: [0]=+68(B), [1]=+69(G), [2]=+70(R).
void ComputeVertexIntensityLit(const float rgb[3], u8 outBGR[3]) {
    float r = rgb[0];   // +48
    float g = rgb[1];   // +52
    float b = rgb[2];   // +56

    // v28 = (r <= g) ? g : r;  then v15 = (v28 <= b) ? b : v28.
    float m = (r <= g) ? g : r;
    m = (m <= b) ? b : m;

    if (static_cast<double>(m) > kLitClampMax) {
        float s = kLitClampScale / m;   // flt_628CA4 / v15
        r = r * s;
        g = g * s;
        b = b * s;
    }
    // Per-channel store: the engine's `(int)` is a BARE `fistp` (verified disasm
    // @0x5c8764/8775/8786 — NO truncation before it), so round-NEAREST-EVEN under
    // the default x87 control word, matched by std::lrint. (e.g. 127.5 -> 128.)
    outBGR[2] = static_cast<u8>(static_cast<int>(std::lrint(r))); // +70  (R)
    outBGR[1] = static_cast<u8>(static_cast<int>(std::lrint(g))); // +69  (G)
    outBGR[0] = static_cast<u8>(static_cast<int>(std::lrint(b))); // +68  (B)
}

// gilde.exe 0x5c8560 — the host-independent driver shape (seed, transform via
// hook, light-accumulate via hook, quantize). The genuine bone-matrix transform
// and the scene-graph dynamic-light walk are the two runtime leaves (hooks).
void BuildVegetationCache(VegVertex* verts, u32 count, bool lit,
                          const VegCacheHooks& hooks) {
    // loc_5C85F8 — seed every vertex's colour scratch to (200,200,200).
    //   v6[12]=flt_64A074; v6[13]=flt_64A078; v6[14]=flt_64A07C;  (R,G,B)
    for (u32 i = 0; i < count; ++i) {
        verts[i].rgb[0] = kVegBaseColour;   // +48
        verts[i].rgb[1] = kVegBaseColour;   // +52
        verts[i].rgb[2] = kVegBaseColour;   // +56
    }

    // loc_5C85EA — VIBE_Mesh_TransformPackedVertices: i8-unpack + bone matrix.
    if (hooks.transformVertices)
        hooks.transformVertices(verts, count, hooks.ctx);

    // loc_5C8663/5C86AD — scene-graph dynamic-light walk + ApplyToCachedVertices.
    if (hooks.accumulateLights)
        hooks.accumulateLights(verts, count, hooks.ctx);

    // loc_5C86C8 — quantize: byte_649D70 selects the lit (3-channel) vs unlit
    // (single grayscale byte) arm.
    if (lit) {
        for (u32 i = 0; i < count; ++i) {
            u8 bgr[3];
            ComputeVertexIntensityLit(verts[i].rgb, bgr);
            verts[i].iB = bgr[0];
            verts[i].iG = bgr[1];
            verts[i].iR = bgr[2];
        }
    } else {
        for (u32 i = 0; i < count; ++i) {
            // The unlit arm writes ONLY +70 (R byte); +68/+69 are untouched.
            verts[i].iR = ComputeVertexIntensityUnlit(verts[i].rgb);
        }
    }
}

} // namespace guild::render
