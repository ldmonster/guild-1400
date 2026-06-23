#include "render/water_render.h"

#include "render/terrain_walk.h"   // TerrainRenderState (gathered view globals)
#include "render/tile_geometry.h"  // TileLightParams / ClampLightByte
#include "util/coord.h"            // ConvertX (x87 truncate-toward-zero)

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe constants (recovered via get_bytes).
// ---------------------------------------------------------------------------
namespace {
constexpr float kWaterShadeBase = 0.5f;   // flt_628B2C (0x3F000000)
// flt_5CA2B0/B4/B8 == (0,0,1) — the sun direction the water-shade dot uses.
constexpr float kSunDir[3] = {0.0f, 0.0f, 1.0f};
// flt_628B30 == 2.0 (0x40000000) — the per-region type-index light scale
// (mirrors flt_628B4C == 2.0 the ground vertex build uses; see v101 @0x5bec97).
constexpr float kTypeLightScale = 2.0f;

// VIBE_Math_VectorNormalize @0x5cb148 — L2-normalise a 3-vector in place.
//   mag = sqrt(x*x + y*y + z*z) stored as a float;
//   test mag & 0x7FFFFFFF; jz -> zero the vector (loc_5CB1A0 writes x=y=z=0);
//   else inv = 1.0/mag (1.0 / the float-rounded magnitude); x,y,z *= inv.
// The guard tests the FLOAT-rounded magnitude (5cb169: fstp [var_C]; test &
// 0x7FFFFFFF), and on a zero magnitude the engine explicitly stores zeros (not
// "leave untouched"). For a 3-vector the only way mag==0 is x==y==z==0, so the
// zeroing branch is a no-op observationally, but we reproduce it for fidelity.
void VectorNormalize3(float v[3]) {
    const float mag = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (mag == 0.0f) {                 // test mag & 0x7FFFFFFF; jz loc_5CB1A0
        v[0] = 0.0f; v[1] = 0.0f; v[2] = 0.0f;
        return;
    }
    const float inv = 1.0f / mag;      // fld1; fdiv mag
    v[0] *= inv; v[1] *= inv; v[2] *= inv;
}
} // namespace

// gilde.exe 0x5be73a — waterShade = 0.5 - |normalize(axisHView) . (0,0,1)|.
float ComputeWaterShade(const float axisHView[3]) {
    float h[3] = {axisHView[0], axisHView[1], axisHView[2]};
    VectorNormalize3(h);   // VIBE_Math_VectorNormalize @0x5cb148 (v74 normalise)
    float dot = h[0] * kSunDir[0] + h[1] * kSunDir[1] + h[2] * kSunDir[2];
    return kWaterShadeBase - std::fabs(dot);   // flt_628B2C - fabs(...)
}

// gilde.exe 0x5be8b3..0x5be909 — per-water-vertex world position.
//   pos.x = wave.x + base.x        (var_8C + var_18)
//   pos.y = wave.y + base.y        (var_88 + var_14)
//   pos.z = wave.z + base.z        (var_84 + var_10)
// The height/depth term (v95/var_34) carries wh*axisH + wave.w + ampBase; it is
// the per-vertex depth the engine accumulates but the screen xyz are the three
// components above. We fold the height axis (dword_13FD520/524/528 * wh) into the
// xyz (the engine adds v74/v75/v76 == axisH*wh into the same x/y/z before base).
void BuildWaterVertexPos(const float base[3], const float axisHView[3],
                         const float wave[4], i16 wh, float ampBase, float outPos[3]) {
    // v74/v75/v76 = axisH(view) * (i16)waterHeight  (0x5be8be..0x5be8c6)
    const float whf = (float)wh;
    for (int k = 0; k < 3; ++k) {
        // 0x5be8d9/0x5be8e8/0x5be8f7: axisH*wh + wave.xyz, then + base (var_18..).
        outPos[k] = axisHView[k] * whf + wave[k] + base[k];
    }
    (void)ampBase;   // the depth-only term (var_34) does not move screen xyz here
}

// ===========================================================================
// VIBE_Floor_TransformTileGeometry @0x5be668 — the water render arm.
// ===========================================================================
WaterDrawStats RenderWaterSurface(const WaterDrawInput& in, const TerrainRenderState& st,
                                  Vertex* vbuf, i32 vbufCap, Polygon* pbuf, i32 pbufCap,
                                  DrawList& dl, const void* waterTexture,
                                  const void* texBase, u32 texStride) {
    WaterDrawStats out{};
    if (!in.spans || in.spanCount <= 0 || !in.meshes || in.meshCount == 0 ||
        !vbuf || vbufCap <= 0 || !pbuf || pbufCap <= 0)
        return out;

    // ---- (1) SHADE PREP (0x5be6a5..0x5be73a) -------------------------------
    // waterShade scales the per-region base amplitude (v15 = mesh[20]*v85).
    const float waterShade = ComputeWaterShade(st.axisH);
    out.waterShade = waterShade;

    // The per-vertex lighting params (the SAME terrain light the ground build
    // reads; the LABEL_42 branch @0x5becb1 == BuildTileVertex's shadow branch).
    TileLightParams lp{};
    for (int k = 0; k < 3; ++k) {
        lp.heightAxis[k] = st.axisH[k];
        lp.lightScale[k] = st.lightScale[k];
        lp.ambient[k]    = st.ambient[k];
        lp.shadowBias[k] = st.shadowBias[k];
    }

    const i32 N = in.size;
    const i32 mask = in.mask;             // Floor+0x0C  (N-1)
    const float* originView = st.originView;  // flt_1404250..
    const float* axisU = st.axisU;        // flt_13FFD40..
    const float* axisV = st.axisV;        // flt_13FD500..

    i32 vCount = 0;   // running vertex cursor (tile+28 == our vbuf)
    i32 pCount = 0;   // running poly cursor   (tile+44 == our pbuf)

    // ---- (2) WATER VERTEX BUILD: the per-tile span loop (0x5be74d..0x5bea49) ---
    // FAITHFUL POINT LINKAGE (wave-7). The engine's strips/points/polys buffers are
    // PER FLOOR TILE (v190[13]/[7]/[11]). RenderTerrain calls this PER TILE, so the
    // span loop here walks one tile's strip list (*(tile+52)), writing each span
    // cell's vertex into the per-tile 80-byte POINT buffer (*(tile+28)) at the index
    // FindRegionOffset stamps: pointIndex = span.base + (col - span.lo) (0x5be882:
    // v21 = v91 + (col & mask); the *v2 store walks the point buffer by +80). The
    // per-cell POLYS then resolve their 4 corners through FindRegionOffset into the
    // SAME point buffer (poly.pTL/pBR/pBL/pTR == off/80, computed by the builder).
    //
    // The compacted WaterRegions::spans/polys are flattened across all 8x8 tiles, so
    // a span's `base` (a per-tile cumulative point index) collides between tiles. We
    // restore the per-tile grouping via WaterStripSpan::tile / WaterPoly::tile and
    // give each (tile, pointIndex) its own global vertex slot — this reproduces the
    // engine's per-tile point buffer + the exact corner threading (NOT the wave-6
    // (row,col) lattice approximation).
    if (N <= 0) return out;

    // Map (tile, perTilePointIndex) -> global vertex index. Tiles are 0..63.
    // Per-tile point count = sum of the tile's span lengths == the engine's v191.
    // We address it as a flat (tile * maxPointsPerTile) table sized lazily.
    // First pass: find, per tile, the max point index used, to size the table.
    i32 maxPts = 0;
    for (i32 s = 0; s < in.spanCount; ++s) {
        const WaterStripSpan& sp = in.spans[s];
        const i32 hiPt = sp.base + (sp.hi - sp.lo);   // last point index in span
        if (hiPt + 1 > maxPts) maxPts = hiPt + 1;
    }
    if (maxPts <= 0) return out;
    const std::size_t kTiles = 64;
    std::vector<i32> pointVert(kTiles * (std::size_t)maxPts, -1);
    auto pvSlot = [&](i32 tile, i32 pt) -> i32& {
        return pointVert[(std::size_t)(tile & 63) * (std::size_t)maxPts + (std::size_t)pt];
    };

    auto emitVertex = [&](const float* meshF, float ampBase, i32 row, i32 col) -> i32 {
        if (vCount >= vbufCap) return -1;
        const i32 rcell = (row & mask) * N + (col & mask);
        // base = originView + row*axisV + col*axisU   (0x5be779..0x5be838).
        float base[3];
        for (int k = 0; k < 3; ++k)
            base[k] = originView[k] + (float)row * axisV[k] + (float)col * axisU[k];
        // wh = water-height byte (Floor+0x18) at the cell (signed i16). 0x5be88d:
        // v21 = v91 + (col & mask), v91 = (mask & span.lo)*N -> per-cell water height.
        i16 wh = 0;
        if (in.waterHeights) wh = (i16)(i32)in.waterHeights[rcell];
        // wave = waveOut[ (row&3)*4 + (col&3) ]  (var_48 + (col&3)*16; +0x38..).
        const i32 wIdx = 14 + ((row & 3) * 4 + (col & 3)) * 4;
        const float* wave = meshF + wIdx;
        Vertex& v = vbuf[vCount];
        std::memset(&v, 0, sizeof(Vertex));
        float pos[3];
        BuildWaterVertexPos(base, st.axisH, wave, wh, ampBase, pos);
        v.x = pos[0]; v.y = pos[1]; v.z = pos[2];
        // per-vertex light: the terrain type byte (LABEL_42 == BuildTileVertex).
        u8 typeByte = in.types ? in.types[rcell] : 0;
        const float l = (float)(typeByte & 0x7F) * kTypeLightScale;
        float r, g, b;
        if ((typeByte & 0x80) == 0) {
            r = lp.lightScale[0] * l + lp.ambient[0];
            g = lp.lightScale[1] * l + lp.ambient[1];
            b = lp.lightScale[2] * l + lp.ambient[2];
        } else {
            r = lp.lightScale[0] * l + lp.shadowBias[0] + lp.ambient[0];
            g = lp.lightScale[1] * l + lp.shadowBias[1] + lp.ambient[1];
            b = lp.lightScale[2] * l + lp.shadowBias[2] + lp.ambient[2];
        }
        v.lightIdx = ClampLightByte(r);   // +66 R
        v._pad41   = ClampLightByte(g);   // +65 G
        v.color0   = ClampLightByte(b);   // +64 B
        return vCount++;
    };

    // Pass 1: per span, emit one vertex per cell into its (tile, pointIndex) slot.
    // region id: the span's marker (FindRegionOffset-stamped pass-4 region) -> the
    // WaterMesh; the per-region ampBase = mesh[20]*waterShade + (i16)mesh[8] (v15).
    for (i32 s = 0; s < in.spanCount; ++s) {
        const WaterStripSpan& span = in.spans[s];
        const u8 region = span.marker;
        if (region >= (u8)in.meshCount) continue;
        const float* meshF = in.meshes + (std::size_t)region * 86u;
        const i32 row = span.row, col0 = span.lo, colHi = span.hi;
        if (colHi < col0) continue;
        // gilde.exe 0x5be833/0x5be849/0x5be854 — ampBase (v18/v95) =
        //   mesh_float[5]*waterShade + (i16)(u8 *)(mesh + 8).
        // The +8 term is `xor eax,eax; mov al,[esi+8]; fild word ptr` — a RAW
        // BYTE at mesh+8 (low byte of float[2]), zero-extended to u8 then
        // sign-extended-as-i16 (a no-op for 0..255), NOT the float[2] value
        // converted. (An earlier reconstruction read meshF[2] as a float and
        // truncated — wrong whenever the byte is nonzero.)
        const u8 ampByte = reinterpret_cast<const u8*>(meshF)[8];
        const float ampBase =
            meshF[5] * waterShade + (float)(i16)(u8)ampByte;
        ++out.spansWalked;
        for (i32 c = col0; c <= colHi; ++c) {
            const i32 pt = span.base + (c - col0);   // engine point index
            if (pt < 0 || pt >= maxPts) continue;
            i32& slot = pvSlot(span.tile, pt);
            if (slot >= 0) continue;                 // point already emitted
            const i32 vi = emitVertex(meshF, ampBase, row, c);
            if (vi < 0) break;
            slot = vi;
        }
    }
    out.vertsBuilt = vCount;

    // Pass 2: per WaterPoly, form the two triangles the engine writes (the 80-byte
    // poly == two 40-byte sub-polys), threading the corner POINT indices through the
    // FindRegionOffset linkage the builder resolved:
    //   tri0 = pTL, pBR, pBL   (off0/80, off1/80, off2/80)
    //   tri1 = pTL, pTR, pBR   (off0/80, off3/80, off1/80)
    // Corners whose FindRegionOffset returned 0 (point index -1 — a region boundary
    // the span list does not cover) drop that triangle, exactly as the engine's
    // point buffer would leave it unreferenced.
    auto cornerVert = [&](i32 tile, i32 pt) -> i32 {
        if (pt < 0 || pt >= maxPts) return -1;
        return pvSlot(tile, pt);
    };
    if (in.polys && in.polyCount > 0) {
        for (i32 pi = 0; pi < in.polyCount; ++pi) {
            const WaterPoly& wp = in.polys[pi];
            const i32 vTL = cornerVert(wp.tile, wp.pTL);
            const i32 vBR = cornerVert(wp.tile, wp.pBR);
            const i32 vBL = cornerVert(wp.tile, wp.pBL);
            const i32 vTR = cornerVert(wp.tile, wp.pTR);
            if (vTL >= 0 && vBR >= 0 && vBL >= 0 && vTR >= 0) ++out.linkedPolys;
            // scroll members for this poly's region (result+28/+32 = mesh+328/+332).
            float sU = 0, sV = 0;
            if (wp.regionId < (u8)in.meshCount) {
                const float* meshF = in.meshes + (std::size_t)wp.regionId * 86u;
                sU = meshF[82]; sV = meshF[83];   // texAccumA / texAccumB
            }
            // tri0 = (TL, BR, BL)
            if (vTL >= 0 && vBR >= 0 && vBL >= 0 && pCount < pbufCap) {
                Polygon& p = pbuf[pCount];
                p = Polygon{};
                p.v0 = &vbuf[vTL]; p.v1 = &vbuf[vBR]; p.v2 = &vbuf[vBL];
                p.flags36 = 0x80;
                p.matIndex = (i32)wp.regionId;
                p.uvX = sU;                       // result+24-class scroll U (carry)
                p.uvY = sV;                       // result+28 = mesh[82] scroll
                p.uvZ = waterTexture ? 1.0f : 0.0f;  // texture-bound signal
                ++pCount; ++out.polysBuilt;
            }
            // tri1 = (TL, TR, BR)
            if (vTL >= 0 && vTR >= 0 && vBR >= 0 && pCount < pbufCap) {
                Polygon& p = pbuf[pCount];
                p = Polygon{};
                p.v0 = &vbuf[vTL]; p.v1 = &vbuf[vTR]; p.v2 = &vbuf[vBR];
                p.flags36 = 0x80;
                p.matIndex = (i32)wp.regionId;
                p.uvX = sU;
                p.uvY = sV;
                p.uvZ = waterTexture ? 1.0f : 0.0f;
                ++pCount; ++out.polysBuilt;
            }
        }
    }

    // ---- (3) PROJECT (0x5bea4f..0x5bee30) ----------------------------------
    // Per-vertex perspective reproject: screenX = D0C*x*(1/z)+D18; screenY =
    // AF8*y*(1/z)+D10 (the no-fog branch @0x5bee13; byte_649DD8 fog branch omitted
    // — the city runs the no-fog reproject). Vertices behind the eye (z<=0) keep
    // their model coords (the engine's clip stage removes them).
    for (i32 i = 0; i < vCount; ++i) {
        Vertex& v = vbuf[i];
        if (v.z > 0.0f) {
            const float invZ = 1.0f / v.z;
            v.screenX = st.projXScale * v.x * invZ + st.projXOff;
            v.screenY = st.projYScale * v.y * invZ + st.projYOff;
            v._pad18  = invZ;            // +0x1C (1/z) the raster reads
            v.clipFlags = 0x80;          // on-screen (high bit set), not culled
        } else {
            v.clipFlags = 0x10;          // behind eye (clip bit) — append skips it
        }
    }

    // ---- (4) APPEND (0x5bec1d..0x5bef03): per visible front-facing poly -----
    // signed-area backface cull (the engine's Pass-C cull, area <= 0 keeps under
    // the from-above projection), then append with the water texture sort key.
    //
    // Sort key (@0x5beebb): v68 = mesh[1] (the per-region water texture handle); if
    // non-null, key = ((v68 - dword_1406A84) >> 7) + 1 (texBase = dword_1406A84,
    // record stride 128 -> >>7), else key = 0 (the white-default branch @0x5bef03).
    // The arm carries ONE EF_WASS texture handle (all regions share it). When a real
    // `texBase` is supplied AND `waterTexture` lies in the record array we compute
    // the exact record index; otherwise (opaque handle / headless) the key is 1 for
    // a bound texture, 0 for the white default.
    auto sortKeyFor = [&](const void* tex) -> u32 {
        if (!tex) return 0u;                               // !Ptr -> *v67 = 0
        if (texBase && texStride) {
            const std::uintptr_t t = (std::uintptr_t)tex;
            const std::uintptr_t b = (std::uintptr_t)texBase;
            if (t >= b) return (u32)(((t - b) / (std::uintptr_t)texStride) + 1u);
        }
        return 1u;                                         // opaque bound handle
    };
    const u32 texSort = sortKeyFor(waterTexture);
    for (i32 i = 0; i < pCount; ++i) {
        if (dl.count >= dl.capacity) break;
        Polygon& p = pbuf[i];
        if (!p.v0 || !p.v1 || !p.v2) continue;
        // both endpoints must be on-screen (the engine appends only fully visible
        // water polys; straddling polys are clipped by the flush).
        if ((p.v0->clipFlags & 0x80) == 0 || (p.v1->clipFlags & 0x80) == 0 ||
            (p.v2->clipFlags & 0x80) == 0)
            continue;
        // signed screen area: skip only DEGENERATE (zero-area) triangles. The water
        // surface is a horizontal sheet and the engine marks the water polys with
        // the no-cull flag (flags38 bit1 / +38), so both windings are drawn — the
        // sheet is visible regardless of the camera azimuth (a one-sided cull would
        // hide the water when viewed from the "back" hemisphere). Degenerate tris
        // (collinear projected points) are dropped (the engine's |normal| < eps gate).
        const float ax = p.v1->screenX - p.v0->screenX;
        const float ay = p.v1->screenY - p.v0->screenY;
        const float bx = p.v2->screenX - p.v0->screenX;
        const float by = p.v2->screenY - p.v0->screenY;
        const float area = ax * by - ay * bx;
        if (area == 0.0f) continue;     // degenerate (collinear)
        p.flags38 |= 0x02;              // no-cull (the engine's water +38 bit)
        DrawListEntry& e = dl.entries[dl.count];
        e.sortKey = texSort;            // ((tex-texBase)>>7)+1 (water texture key)
        e.poly = &p;
        ++dl.count;
        ++out.appended;
        if (texSort) {
            ++out.texturedPolys;
            // The animated EF_WASS scroll members the engine copied into the poly
            // (mesh[82]/[83] -> poly+28/+32; here poly.uvY/poly.uvX). Report the
            // last appended textured poly's scroll for introspection / golden checks.
            out.scrollU = p.uvX;        // mesh[82] texAccumA (carried scroll U)
            out.scrollV = p.uvY;        // mesh[83] texAccumB (poly+28 store)
        }
    }
    return out;
}

} // namespace guild::render
