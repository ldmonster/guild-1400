#include "render/terrain_walk.h"
#include <cmath>

// =============================================================================
// guild::render — VIBE_Floor_RenderTerrain @0x5bf22c, the complete whole-walk.
// 1:1 translation of the 283-basic-block driver. The per-tile maths reuse the
// already-reconstructed sub-systems (TileSubdivCount, BuildTileVertex,
// ClampLightByte, AppendTilePolysToDrawList) where the original inlined them.
// =============================================================================
namespace guild::render {

// File-scope constants recovered byte-for-byte from gilde.exe .rdata:
//   flt_628B4C = 2.0   (0x628b4c: 00 00 00 40)  type-byte -> light mul
//   flt_628B48 = 0.5   (0x628b48: 00 00 00 3f)  seam-edge midpoint blend weight
//   dbl_628B54 = 255.0 (0x628b54: .. e0 6f 40)  fog-shade light cap
static constexpr float  kTypeLightMul = 2.0f;   // flt_628B4C
static constexpr float  kSeamBlend    = 0.5f;   // flt_628B48
static constexpr double kFogShadeCap  = 255.0;  // dbl_628B54

// ---------------------------------------------------------------------------
// Helper: a tile's 80-byte Vertex cursor / 40-byte poly cursor are raw byte
// buffers in the original; we index them as Vertex/Polygon arrays (same stride).
// ---------------------------------------------------------------------------
static inline Vertex*  VBuf(TerrainTile* t)  { return reinterpret_cast<Vertex*>(t->vertexBuf); }
static inline Polygon* PBuf(TerrainTile* t)  { return reinterpret_cast<Polygon*>(t->polyBuf); }

// The 16.0f stamp the stitch writes into the appended poly +24 (the original wrote
// the raw dword 1098907648 == 0x41800000 == 16.0f).  @0x5bf22c lines 1047/1267/1496/1666.
static constexpr float kStitchUvStamp = 16.0f;  // 1098907648 == 0x41800000

// gilde.exe 0x5bf22c lines 505..531 — Phase-0 light-param select.
void SetupTerrainLight(TerrainRenderState& st, bool flatLit, const float sunScale[3],
                       const float sunAmbient[3], const float sunBias[3]) {
    if (flatLit) {
        // (*(Floor+7280) & 2): flat-lit — ambient 0, scale 1, shadowBias 0.
        for (int c = 0; c < 3; ++c) {
            st.ambient[c]    = 0.0f;   // flt_13FD4F0/4F4/4F8
            st.lightScale[c] = 1.0f;   // flt_13FD510/514/518
            st.shadowBias[c] = 0.0f;   // flt_13FD530/534/538
        }
    } else {
        for (int c = 0; c < 3; ++c) {
            st.ambient[c]    = sunAmbient[c]; // flt_64A074/078/07C
            st.lightScale[c] = sunScale[c];   // Floor+208/212/216
            st.shadowBias[c] = sunBias[c];    // flt_64A084/088/08C
        }
    }
}

// gilde.exe 0x5bf22c lines 532..578 — rotate a floor axis vector through the view
// rotation. `viewMat3x3` points at view+396; columns: out.z uses +0/+16/+32 (396/412/
// 428), out.x uses +4/+20/+36 (400/416/432), out.y uses +8/+24/+40 (404/420/436).
void RotateFloorAxis(const float in[3], const float* viewMat3x3, float out[3]) {
    // Original ordering: the FIRST stored component uses view+396/412/428 (m[0]/m[4]/m[8]).
    out[0] = in[0]*viewMat3x3[0] + in[1]*viewMat3x3[4] + in[2]*viewMat3x3[8];   // +396/412/428
    out[1] = in[0]*viewMat3x3[1] + in[1]*viewMat3x3[5] + in[2]*viewMat3x3[9];   // +400/416/432
    out[2] = in[0]*viewMat3x3[2] + in[1]*viewMat3x3[6] + in[2]*viewMat3x3[10];  // +404/420/436
}

// ---------------------------------------------------------------------------
// Per-vertex build helper used everywhere in the walk (lines 652..700 etc.). Writes
// world xyz (heightAxis*h + acc) + the three clamped RGB light bytes into Vertex v.
// This is exactly BuildTileVertex (tile_geometry) with the walk's accumulator/axes.
// ---------------------------------------------------------------------------
static void EmitVertex(Vertex& v, const TerrainRenderState& st, const float acc[3],
                       u8 heightSample, u8 typeByte) {
    TileLightParams p;
    p.heightAxis[0] = st.axisH[0]; p.heightAxis[1] = st.axisH[1]; p.heightAxis[2] = st.axisH[2];
    p.lightScale[0] = st.lightScale[0]; p.lightScale[1] = st.lightScale[1]; p.lightScale[2] = st.lightScale[2];
    p.ambient[0] = st.ambient[0]; p.ambient[1] = st.ambient[1]; p.ambient[2] = st.ambient[2];
    p.shadowBias[0] = st.shadowBias[0]; p.shadowBias[1] = st.shadowBias[1]; p.shadowBias[2] = st.shadowBias[2];
    u32 packed = BuildTileVertex(v, p, acc, heightSample, typeByte);
    // The engine mirrored the +64 light dword to +68 (the *(v183-12)=v191 store). Our
    // Vertex models +64/+65/+66; the mirror is consumed by the rasterizer's +68 read,
    // which our DrawList-based reconstruction does not serialise. (packed is the value.)
    (void)packed;
}

// gilde.exe 0x5bf22c — the COMPLETE whole-walk.
i32 RenderTerrain(TerrainFloor* floor, TerrainRenderState& st, const TerrainWalkHooks& hooks,
                  bool animateWater) {
    // ---- Phase 0: entry gate (line 497) ------------------------------------
    if (!st.engineOn || floor == nullptr)
        return 0;                       // if (byte_649D70 && dword_13FCD1C)

    const i32 size     = floor->size;       // v377
    const i32 tileSpan = floor->tileSpan;   // v376
    const i32 mask     = floor->mask;       // v378
    const u8* heights  = floor->heights;    // v381
    const u8* types    = floor->types;      // v380

    i32 polysAppended = 0;

    // (Phase-0 light + axis setup are done by the caller via SetupTerrainLight /
    //  RotateFloorAxis into st before calling, mirroring the engine writing the
    //  globals at the top of the fn; st carries them.)

    // ---- Phase 1: geometry build (if Floor+7280 & 1, line 579) -------------
    i32 updated = 0;
    if (floor->flatLit & 1) {
        // updated = VIBE_Floor_UpdateTileVisibility(result)  (line 581)
        updated = hooks.updateVisibility ? hooks.updateVisibility(floor) : 1;

        // ===== Pass A: per-tile vertex grid + quad poly build (lines 582..812) =====
        // The tile array is row-major 8x8 at Floor+224; v427/v429 are tile-X/Y world
        // sample offsets (col*tileSpan, row*tileSpan).
        i32 worldY = 0;                         // v429 (row * tileSpan)
        for (i32 row = 0; row < 8; ++row) {     // v369
            i32 worldX = 0;                     // v427 (col * tileSpan)
            for (i32 col = 0; col < 8; ++col) { // v370
                TerrainTile* tile = &floor->tiles[row * 8 + col];
                u8 lod = tile->lod;             // v372 = *(v428+94)
                if (lod == 0) {
                    tile->subdivCached = 0;     // *(v428+16) = 0  (line 800)
                    tile->polyCount    = 0;     // *(v428+32) = 0
                    worldX += tileSpan;
                    continue;
                }

                // Subdivision counts (lines 610..617) — reuse TileSubdivCount.
                //   v180 = base, minus 4/lod when the COLUMN index (v370) == 7
                //   v179 = base, minus 4/lod when the ROW index    (v369) == 7
                i32 v180 = TileSubdivCount(tileSpan, lod, col, row, true);   // col-edge term
                i32 v179 = TileSubdivCount(tileSpan, lod, col, row, false);  // row-edge term

                // Cache the subdiv product on a LOD/visibility change (lines 618..619).
                if (tile->lod != tile->prevLod || updated)
                    tile->subdivCached = v180 * v179;

                // World accumulator for the tile's (0,0) corner (lines 598..609):
                //   acc = originView + axisU*worldX + axisV*worldY
                float fx = (float)worldX, fy = (float)worldY;
                float acc[3] = {
                    st.axisU[0]*fx + st.axisV[0]*fy + st.originView[0],
                    st.axisU[1]*fx + st.axisV[1]*fy + st.originView[1],
                    st.axisU[2]*fx + st.axisV[2]*fy + st.originView[2],
                };
                // Per-step axis (axisU * lod) used to advance along a tile row (623..628):
                float fl = (float)lod;
                float stepU[3] = { st.axisU[0]*fl, st.axisU[1]*fl, st.axisU[2]*fl };
                float stepV[3] = { st.axisV[0]*fl, st.axisV[1]*fl, st.axisV[2]*fl };

                i32 cellBase = (worldX + size * worldY) & mask;   // v373

                // Build the (v179 x v181) vertex grid (lines 630..709). v181 == v180.
                Vertex* vcur = VBuf(tile);   // v183
                if (v179 != 0) {
                    float rowAcc[3] = { acc[0], acc[1], acc[2] };
                    i32 cell = cellBase;
                    for (i32 r = 0; r < v179; ++r) {            // v371 loop
                        float colAcc[3] = { rowAcc[0], rowAcc[1], rowAcc[2] };
                        i32 c = cell;                            // v184
                        for (i32 k = 0; k < v180; ++k) {         // v185 loop
                            u8 h = heights[c];                   // *(v381 + v184)
                            u8 t = types[c];                     // *(v380 + v184)
                            EmitVertex(*vcur, st, colAcc, h, t);
                            ++vcur;
                            colAcc[0] += stepU[0]; colAcc[1] += stepU[1]; colAcc[2] += stepU[2]; // 701..703
                            c = (lod + c) & mask;                // v184 = mask & (lod+v184) (704)
                        }
                        rowAcc[0] += stepV[0]; rowAcc[1] += stepV[1]; rowAcc[2] += stepV[2]; // 642..644
                        cell = (lod * size + cell) & mask;       // v373 = (lod*size + v373)&mask (646)
                    }
                }

                // Quad poly build (lines 711..795), only when visibility changed.
                if (updated) {
                    tile->polyCount = 0;                          // *(v428+32) = 0
                    Polygon* pcur = PBuf(tile);                   // v192
                    // The per-LOD slope/visibility flag buffer v379 = *(Floor +
                    // 4*(lod>>1) + 36) — the BuildTilePolys @0x5bc45c OUTPUT (NOT the
                    // raw texture source). Its bytes carry the 0x80 slope/visibility
                    // bit (BuildTilePolys' QuadPolyVisible result) and the 0x40
                    // sub-texture marker; the diagonal split reads its SIGN. This is
                    // the SAME buffer the texture sub-id sampling indexes.
                    const u8* slopeBuf = floor->mipTexSrc[lod >> 1];// v379
                    // v374 (line 718): linear cell index into v379, masked ONCE by
                    // Floor+8 (== N*N-1, the LINEAR cell mask) at init; the inner
                    // walk then advances it raw (v413 = v379 + v374; ++v413 per col;
                    // v374 += N/lod per row, unmasked).
                    i32 v374 = ( (size * (worldY / lod) / lod) + (worldX / lod) ) & mask; // v374
                    i32 rowsM1 = v179 - 1;
                    i32 colsM1 = v180 - 1;                        // v426
                    // Iterate the (v179-1) x (v180-1) quads (lines 727..794).
                    Vertex* vbase = VBuf(tile);                   // v466 (vertex buffer base)
                    i32 vRowStride = v180;                        // verts per built row
                    for (i32 qr = 0; qr < rowsM1; ++qr) {
                        const u8* v413 = slopeBuf ? (slopeBuf + v374) : nullptr; // v413
                        for (i32 qc = 0; qc < colsM1; ++qc) {
                            // v494 = *v413 (line 740); v194 = *v413 & 0x40 -> the
                            // sub-texture id from byte_13DCE58 (line 741..743). When no
                            // slope buffer is bound (standalone walk) the split defaults
                            // to 0 (>= 0 => the TL-BR diagonal), matching an all-zero
                            // BuildTilePolys output (every quad visible, TL-BR split).
                            i8 v494 = v413 ? (i8)v413[qc] : (i8)0;
                            // Per-cell texture sub-id from the opaque byte_13DCE58 table
                            // (line 743) — supplied via the build hook's texSrc.
                            const u8* texSrc = slopeBuf ? slopeBuf : floor->texSrc;
                            u32 texId = hooks.getOrBuildTile
                                ? hooks.getOrBuildTile(texSrc, size,
                                                       (worldX / lod) + qc, (worldY / lod) + qr,
                                                       lod, hooks.cacheState)
                                : 0;
                            // The quad's two triangles split on the per-cell flag (the
                            // *(v413) byte >= 0 test, line 748). v494 IS that flag byte
                            // (the real BuildTilePolys output buffer — proxy removed).
                            i32 i00 =  qr      * vRowStride + qc;
                            i32 i01 =  qr      * vRowStride + qc + 1;
                            i32 i10 = (qr + 1) * vRowStride + qc;
                            i32 i11 = (qr + 1) * vRowStride + qc + 1;
                            i8 split = v494;
                            Polygon& p0 = pcur[0];
                            Polygon& p1 = pcur[1];
                            // VERTEX NAMING (1:1 with @0x5bf22c lines 730..786):
                            //   v417 = vert(row,col)     = i00   (TL)
                            //   v416 = vert(row,col+1)   = i01   (TR)
                            //   v414 = vert(row+1,col)   = i10   (BL)
                            //   v415 = vert(row+1,col+1) = i11   (BR)
                            // The split byte v494 = *(v379 + v374) is the per-LOD slope/
                            // hidden-flag buffer (BuildTilePolys @0x5bc45c output); >=0 ==
                            // the TL-BR diagonal, <0 == the BL-TR diagonal. Re-verified
                            // wave-5: the engine has NO backface cull anywhere in the
                            // terrain path (RasterizeTexturedTriangle @0x5F7D58 normalises
                            // winding by signed-area sign; ComputeVertexClipFlags @0x5ad614
                            // is frustum-only), so this exact winding is the genuine one —
                            // the wave-4 row-mirror hack is removed (see header / progress).
                            if (split >= 0) {
                                // @0x5bf22c lines 752..761 (v494 >= 0):
                                //   tri0 = (v417,v415,v414) = (i00,i11,i10)
                                //   tri1 = (v417,v416,v415) = (i00,i01,i11)
                                //   +38 |= 1
                                p0.v0 = &vbase[i00]; p0.v1 = &vbase[i11]; p0.v2 = &vbase[i10];
                                p0.flags38 |= 1;
                                p1.v0 = &vbase[i00]; p1.v1 = &vbase[i01]; p1.v2 = &vbase[i11];
                                p1.flags38 |= 1;
                            } else {
                                // @0x5bf22c lines 765..774 (v494 < 0):
                                //   tri0 = (v414,v417,v416) = (i10,i00,i01)
                                //   tri1 = (v416,v415,v414) = (i01,i11,i10)
                                //   +38 &= ~1
                                p0.v0 = &vbase[i10]; p0.v1 = &vbase[i00]; p0.v2 = &vbase[i01];
                                p0.flags38 &= 0xFE;
                                p1.v0 = &vbase[i01]; p1.v1 = &vbase[i11]; p1.v2 = &vbase[i10];
                                p1.flags38 &= 0xFE;
                            }
                            // texture id stored at poly+20 and +60 (lines 747/751/765).
                            // (modelled via uvZ as the bound id is engine-internal.)
                            p0.uvZ = (float)texId;
                            p1.uvZ = (float)texId;

                            // ---- PER-QUAD UV EMISSION (@0x5c1f95..0x5c203f) ----
                            // subTexId = (cellFlag & 0x40) ? byte_13DCE58[..]&0x3F : 0.
                            // The cell flag is *v413 (== v494) — its 0x40 bit gates the
                            // sub-id, its 0x80 bit (the visible/slope bit) gates whether
                            // the UV pointers are stamped at all. Pass A here always emits
                            // a VISIBLE quad (flags36|0x80 below), matching the engine's
                            // (cellFlag & 0x80) branch for built quads. tri0 gets the
                            // first 6 floats of flt_13FE540[subTexId*0x60], tri1 the next 6.
                            if (st.uvTable && tile->polyUv) {
                                u8 cellFlag = (u8)v494;          // *v413 (var_4)
                                const i32 cellU = (worldX / lod) + qc;   // absolute cell
                                const i32 cellV = (worldY / lod) + qr;
                                u32 subTexId = TerrainSubTexId(cellFlag, st.subTexSrc,
                                                               cellU, cellV);
                                i32 pIdx = (i32)(&p0 - PBuf(tile));   // poly slot of tri0
                                TerrainQuadUvT0(&tile->polyUv[kTriUvFloats * pIdx],
                                                st.uvTable, subTexId);          // poly+0x10
                                TerrainQuadUvT1(&tile->polyUv[kTriUvFloats * (pIdx + 1)],
                                                st.uvTable, subTexId);          // poly+0x38
                            }
                            // The quad emits VISIBLE polys: BuildTilePolys (tile_lighting
                            // QuadPolyVisible) marks the +36 high bit ("backface-visible")
                            // for the front-facing quad. Pass C's signed-area cull then
                            // refines it. Set it here so the rebuilt quad is a candidate.
                            p0.flags36 |= 0x80u;
                            p1.flags36 |= 0x80u;
                            pcur += 2;
                            tile->polyCount += 2;                  // *(v428+32) += 2 (line 781)
                        }
                        v374 += size / lod;                        // v374 += *(Floor)/lod (787)
                    }
                }

                worldX += tileSpan;     // v427 += v376
            }
            worldY += tileSpan;         // v429 += v376
        }

        // ===== Pass B: 2:1 LOD-seam stitch (lines 813..1736) =====
        // RECONSTRUCTED 1:1 from the full decompile @0x5bf22c lines 848..1691 (four
        // arms). For a tile (row=v486, col=v487) with lod=v454 the stitch fixes the
        // T-junction crack on each edge whose neighbour has a strictly FINER edge-LOD
        // byte (*(neighbour+318) < lod). Per boundary poly it splits the seam edge at
        // its MIDPOINT (half-step v387 = lod>>1):
        //   (1) emits ONE midpoint Vertex at vertexBuf[subdivCached] (v465 = 80*
        //       subdivCached + vertexBuf) at the half-step world position along the
        //       seam (same world/light math as Pass-A BuildTileVertex), recording it
        //       into the clip scratch v483[3*i + {0,1,2}] = {vertPtr, seamU, seamV};
        //   (2) RE-POINTS the boundary poly's FAR seam-vertex pointer to the midpoint
        //       (so it spans [near-corner .. midpoint]);
        //   (3) APPENDS ONE poly at polyBuf[polyCount] (v482/v481/v480/v479) — a copy
        //       of the boundary poly with v1 := midpoint (so it spans [.. midpoint ..
        //       far-corner]) and +24 (uvX) stamped 16.0f (1098907648);
        //   (4) bumps polyCount(+32)+1, subdivCached(+16)+1, vertCount(+56)+1.
        // Boundary-poly walk per arm (v455=cols=v180, v456=rows=v179; quad pairs are
        // pcur[0]/pcur[1] @ poly index 2*(qr*colsM1+qc)/+1 from Pass A):
        //   RIGHT(col<7): guard rows!=1; loop rows-1; boundary=PBuf[2*v455-4 +1] (last
        //     column 2nd-tri), per-row stride 2*(v455-1) polys; seam down axisV.
        //   LEFT (col>0): guard rows!=1; loop rows-1; boundary=PBuf[0] (first column
        //     1st-tri), per-row stride 2*(v455-1); seam down axisV.
        //   UP   (row>0): guard cols!=1; loop cols-1; boundary=PBuf[0] (first row),
        //     stride 2 polys (one quad along U); seam across axisU.
        //   DOWN (row<7): guard cols!=1; loop cols-1; boundary=PBuf[(v455-1)*(2*v456-4)]
        //     (last row, 1st-tri base), stride 2; seam across axisU.
        // Each arm skips boundary polys whose v0 pointer is null (hidden quad: *v470==0).
        //
        // The re-pointed seam-vertex SLOT and the copied source tri differ per arm /
        // diagonal (decompiled exactly below); the diagonal is *(poly+38)&1 (the shared
        // Pass-A split flag): TL-BR (set) vs BL-TR (clear).
        //
        // SEAM-UV MIDPOINT BLEND (@0x5bf22c lines 953..1043 etc.) — RECONSTRUCTED 1:1
        // (wave-18, was the wave-17 named boundary). The engine midpoint-blends the
        // seam UV pair with flt_628B48=0.5 into a per-tile 24-byte UV scratch (tile+60
        // == drawData) backed by the global UV table flt_13FE540 (a 24-float-stride
        // table Pass-A writes poly+16/+56 pointers INTO at @0x5c1ff5/0x5c2015). The
        // full flt_13FE540 UV-emission subsystem is now modelled: Pass-A stamps each
        // quad poly's tri0/tri1 6-float UV record (TerrainQuadUvT0/T1) into the per-tile
        // polyUv array (poly+16/+56 image); Pass-B copies the boundary tri's record into
        // the per-tile uvScratch, blends the seam endpoints (TerrainSeamBlendUv, 0.5),
        // re-points both boundary + appended polys at their scratch records, and stamps
        // the blended midpoint UV into the appended midpoint vertex slot — exactly the
        // two-qmemcpy + blend the disasm performs (see emitSeamPoly). flt_13FE540 and
        // byte_13DCE58 are all-zero in the static image (get_bytes verified); the table
        // is built at runtime by BuildTerrainUvTable (@0x5b94cc) and supplied via
        // st.uvTable, so subTexId == 0 and the single 24-float record is used. When
        // st.uvTable/tile->polyUv are null the walk emits geometry only (inert).
        if (updated) {
            for (i32 row = 0; row < 8; ++row) {      // v486
                i32 worldX = 0;
                i32 worldY = row * tileSpan;         // v444
                for (i32 col = 0; col < 8; ++col) {  // v487
                    worldX = col * tileSpan;         // v436
                    TerrainTile* tile = &floor->tiles[row * 8 + col];
                    u8 lod = tile->lod;              // v454
                    if (lod == 0) continue;
                    tile->vertCount = 0;             // *(v435+56) = 0 (line 845)
                    tile->uvScratchCount = 0;        // per-tile UV scratch cursor reset
                    const i32 v387 = lod >> 1;       // half-step (line 833)

                    // v455 = cols (= v180), v456 = rows (= v179): the Pass-A subdiv
                    // counts WITH the col-7/row-7 edge adjustment (lines 834..846).
                    const i32 v455 = TileSubdivCount(tileSpan, lod, col, row, true);   // cols
                    const i32 v456 = TileSubdivCount(tileSpan, lod, col, row, false);  // rows

                    Vertex*  vbase = VBuf(tile);
                    Polygon* pbase = PBuf(tile);
                    void**   clip  = reinterpret_cast<void**>(tile->clipList);  // v483

                    // Emit one seam midpoint vertex + far-vertex re-point + appended
                    // poly for a single boundary poly. `world`/`cellIdx` give the
                    // midpoint world position + height cell; `seamU`/`seamV` the clip
                    // scratch coords; `repoint` selects which slot of the boundary poly
                    // becomes the midpoint; `copyTri` is the source tri for the append;
                    // `appendSlot` which slot of the appended copy becomes the midpoint.
                    auto emitSeamPoly =
                        [&](const float world[3], i32 cellIdx, i32 seamU, i32 seamV,
                            Polygon* boundary, int repointSlot,
                            Polygon* copySrc, int appendSlot) {
                        // Midpoint vertex at vertexBuf[subdivCached] (v465 = 80*+16 + +24).
                        Vertex* mid = &vbase[tile->subdivCached];
                        u8 h = heights[cellIdx];                  // *(v460 + v381)
                        u8 t = types[cellIdx];                    // *(v460 + v380)
                        EmitVertex(*mid, st, world, h, t);        // world xyz + RGB light

                        // Clip scratch v483[0..2] = {vert, seamU, seamV} (lines 904..906).
                        if (clip) {
                            i32 base = 3 * tile->vertCount;
                            clip[base + 0] = mid;
                            clip[base + 1] = reinterpret_cast<void*>((intptr_t)seamU);
                            clip[base + 2] = reinterpret_cast<void*>((intptr_t)seamV);
                        }

                        // Appended poly = a COPY of the boundary triangle (the engine
                        // qmemcpy'd the 40-byte record), with v1 := midpoint and uvX :=
                        // 16.0f (the +24 dword stamp).  (lines 981/1010/1043..1047 etc.)
                        Polygon* app = &pbase[tile->polyCount];
                        *app = *copySrc;
                        Vertex* far;
                        switch (repointSlot) {       // boundary poly far-vertex re-point
                            case 0:  far = boundary->v0; boundary->v0 = mid; break;
                            case 1:  far = boundary->v1; boundary->v1 = mid; break;
                            default: far = boundary->v2; boundary->v2 = mid; break;
                        }
                        (void)far;
                        switch (appendSlot) {        // appended copy gets the midpoint as v1
                            case 0:  app->v0 = mid; break;
                            case 1:  app->v1 = mid; break;
                            default: app->v2 = mid; break;
                        }
                        app->uvX = kStitchUvStamp;   // *(poly+24) = 16.0f

                        // ---- SEAM-UV MIDPOINT BLEND (@0x5bf22c, flt_628B48 = 0.5) ----
                        // Per the disasm (RIGHT arm @0x5bfcc8..0x5bfe1b, the three sibling
                        // arms identical): the engine consumes TWO per-tile 24-byte UV
                        // scratch records (tile+60 == drawData, indexed 24*counter):
                        //   rec N   = qmemcpy of the BOUNDARY poly's 6-float UV record;
                        //             midpoint-blend it (TerrainSeamBlendUv, diagonal =
                        //             boundary.flags38&1); re-point boundary's UV at rec N.
                        //   rec N+1 = qmemcpy of the APPENDED (copied) poly's 6-float UV
                        //             record; stamp the blended midpoint (u,v) into the
                        //             appended midpoint vertex's slot (v1); re-point the
                        //             appended poly's UV at rec N+1.
                        if (st.uvTable && tile->polyUv && tile->uvScratch) {
                            const bool diagTLBR = (boundary->flags38 & 1) != 0;
                            i32 bIdx = (i32)(boundary - pbase);   // boundary poly slot
                            i32 aIdx = (i32)(app - pbase);        // appended poly slot
                            // rec N <- boundary poly's UV record, blended.
                            float* recN = &tile->uvScratch[kTriUvFloats * tile->uvScratchCount];
                            const float* src = &tile->polyUv[kTriUvFloats * bIdx];
                            for (int q = 0; q < kTriUvFloats; ++q) recN[q] = src[q];
                            TerrainSeamBlendUv(recN, diagTLBR);   // 0.5 midpoint average
                            // boundary poly's UV record := rec N (poly+0x10 re-point).
                            for (int q = 0; q < kTriUvFloats; ++q)
                                tile->polyUv[kTriUvFloats * bIdx + q] = recN[q];
                            // The blended midpoint (u,v) — the slot the blend wrote into
                            // (TL-BR -> vert 2 == floats 4,5; BL-TR -> vert 0 == floats 0,1).
                            float midU = diagTLBR ? recN[4] : recN[0];
                            float midV = diagTLBR ? recN[5] : recN[1];
                            ++tile->uvScratchCount;
                            // rec N+1 <- appended poly's UV record (copy of boundary's
                            // pre-blend record == src), then stamp the blended midpoint
                            // into the appended midpoint vertex's slot (v1 == floats 2,3).
                            float* recN1 = &tile->uvScratch[kTriUvFloats * tile->uvScratchCount];
                            for (int q = 0; q < kTriUvFloats; ++q) recN1[q] = src[q];
                            recN1[2] = midU;   // *(eax+8)  = blendedU
                            recN1[3] = midV;   // *(eax+0xC)= blendedV
                            for (int q = 0; q < kTriUvFloats; ++q)
                                tile->polyUv[kTriUvFloats * aIdx + q] = recN1[q];
                            ++tile->uvScratchCount;
                        }

                        // Counters: polyCount+1, subdivCached(vertex)+1, vertCount+1.
                        tile->polyCount   += 1;      // *(v435+32) += 1
                        tile->subdivCached += 1;     // *(v435+16) += 1
                        tile->vertCount   += 1;      // *(v435+56) += 1
                    };

                    // ---- RIGHT arm (col<7): vertical seam, iterates rows ------------
                    if (col < 7 && tile->edgeRightLod != 0 && tile->edgeRightLod < lod) {
                        if (v456 != 1) {
                            // world = origin + axisU*(tileSpan+worldX) + axisV*(v387+worldY)
                            float u = (float)(tileSpan + worldX), v = (float)(v387 + worldY);
                            float acc[3] = {
                                st.axisU[0]*u + st.axisV[0]*v + st.originView[0],
                                st.axisU[1]*u + st.axisV[1]*v + st.originView[1],
                                st.axisU[2]*u + st.axisV[2]*v + st.originView[2] };
                            i32 cell = (tileSpan + worldX + size*(v387 + worldY)) & mask; // v460
                            i32 seamV = v387 + worldY;
                            i32 pIdx = 2*v455 - 4 + 1;        // last-col 2nd-tri (v470+40)
                            for (i32 i = 0; i < v456 - 1; ++i) {
                                Polygon* firstTri = &pbase[pIdx - 1];     // *v470
                                if (firstTri->v0) {
                                    Polygon* b = &pbase[pIdx];            // boundary 2nd-tri
                                    // diagonal *(v470+38)&1: TL-BR re-point v2; BL-TR v0.
                                    int rep = (b->flags38 & 1) ? 2 : 0;
                                    emitSeamPoly(acc, cell, tileSpan + worldX, seamV,
                                                 b, rep, b, /*appendSlot=*/1);
                                }
                                // advance one row down the seam (axisV*lod), cell += N*lod.
                                acc[0]+=st.axisV[0]*lod; acc[1]+=st.axisV[1]*lod; acc[2]+=st.axisV[2]*lod;
                                cell += size*lod;  seamV += lod;
                                pIdx += 2*(v455 - 1);
                            }
                        }
                    }

                    // ---- LEFT arm (col>0): vertical seam, iterates rows -------------
                    if (col > 0 && tile->edgeLeftLod != 0 && tile->edgeLeftLod < lod) {
                        if (v456 != 1) {
                            float u = (float)worldX, v = (float)(v387 + worldY);
                            float acc[3] = {
                                st.axisU[0]*u + st.axisV[0]*v + st.originView[0],
                                st.axisU[1]*u + st.axisV[1]*v + st.originView[1],
                                st.axisU[2]*u + st.axisV[2]*v + st.originView[2] };
                            i32 cell = (worldX + size*(v387 + worldY)) & mask;  // v459
                            i32 seamV = v387 + worldY;
                            i32 pIdx = 0;                      // first-col 1st-tri (v469)
                            for (i32 i = 0; i < v456 - 1; ++i) {
                                Polygon* b = &pbase[pIdx];     // boundary = first-col 1st-tri
                                if (b->v0) {
                                    // LEFT always re-points v0 (lines 1192/1241 *v469);
                                    // appended slot is v2 (TL-BR, line 1216 +2) or v1
                                    // (BL-TR, line 1265 +1).
                                    int app = (b->flags38 & 1) ? 2 : 1;
                                    emitSeamPoly(acc, cell, worldX, seamV,
                                                 b, /*repoint=*/0, b, app);
                                }
                                acc[0]+=st.axisV[0]*lod; acc[1]+=st.axisV[1]*lod; acc[2]+=st.axisV[2]*lod;
                                cell += size*lod;  seamV += lod;
                                pIdx += 2*(v455 - 1);
                            }
                        }
                    }

                    // ---- UP arm (row>0): horizontal seam, iterates cols ------------
                    if (row > 0 && tile->edgeUpLod != 0 && tile->edgeUpLod < lod) {
                        if (v455 != 1) {
                            float u = (float)(v387 + worldX), v = (float)worldY;
                            float acc[3] = {
                                st.axisU[0]*u + st.axisV[0]*v + st.originView[0],
                                st.axisU[1]*u + st.axisV[1]*v + st.originView[1],
                                st.axisU[2]*u + st.axisV[2]*v + st.originView[2] };
                            i32 cell = (v387 + worldX + size*worldY) & mask;  // v458
                            i32 seamU = v387 + worldX;
                            i32 pIdx = 0;                     // first-row poly (v468)
                            for (i32 i = 0; i < v455 - 1; ++i) {
                                Polygon* b = &pbase[pIdx];    // boundary = first-row poly
                                if (b->v0) {
                                    // TL-BR: re-point 2nd-tri.v0 (v488=v468+10 -> +40 = 2nd
                                    //   tri, slot 0) and copy 2nd tri; BL-TR: re-point
                                    //   1st-tri.v2 (v468[2]) and copy 1st tri.
                                    if (b->flags38 & 1) {
                                        Polygon* tri2 = &pbase[pIdx + 1];   // 2nd tri (v468+40)
                                        emitSeamPoly(acc, cell, seamU, worldY,
                                                     tri2, /*repoint v0*/0, tri2, /*append v1*/1);
                                    } else {
                                        emitSeamPoly(acc, cell, seamU, worldY,
                                                     b, /*repoint v2*/2, b, /*append v1*/1);
                                    }
                                }
                                acc[0]+=st.axisU[0]*lod; acc[1]+=st.axisU[1]*lod; acc[2]+=st.axisU[2]*lod;
                                cell += lod;  seamU += lod;
                                pIdx += 2;
                            }
                        }
                    }

                    // ---- DOWN arm (row<7): horizontal seam, iterates cols ----------
                    if (row < 7 && tile->edgeDownLod != 0 && tile->edgeDownLod < lod) {
                        if (v455 != 1) {
                            float u = (float)(v387 + worldX), v = (float)(tileSpan + worldY);
                            float acc[3] = {
                                st.axisU[0]*u + st.axisV[0]*v + st.originView[0],
                                st.axisU[1]*u + st.axisV[1]*v + st.originView[1],
                                st.axisU[2]*u + st.axisV[2]*v + st.originView[2] };
                            i32 cell = (v387 + worldX + size*(tileSpan + worldY)) & mask;  // v457
                            i32 seamU = v387 + worldX;
                            i32 pIdx = (v455 - 1) * (2*v456 - 4);   // last-row 1st-tri base (v467)
                            for (i32 i = 0; i < v455 - 1; ++i) {
                                Polygon* firstTri = &pbase[pIdx];   // *v467
                                if (firstTri->v0) {
                                    // v123 = v467 if TL-BR else v467+40 (1st vs 2nd tri);
                                    // re-point v123.v2 (v123+2), append copy with v1 = mid.
                                    Polygon* b = (firstTri->flags38 & 1)
                                                 ? firstTri : &pbase[pIdx + 1];
                                    emitSeamPoly(acc, cell, seamU, tileSpan + worldY,
                                                 b, /*repoint v2*/2, b, /*append v1*/1);
                                }
                                acc[0]+=st.axisU[0]*lod; acc[1]+=st.axisU[1]*lod; acc[2]+=st.axisU[2]*lod;
                                cell += lod;  seamU += lod;
                                pIdx += 2;
                            }
                        }
                    }

                    tile->prevLod = tile->lod;                // *(v461+95)=*(v461+94) (1724)
                }
            }
        }

        // ===== Pass C: clip flags + project + backface cull (lines 1737..1844) =====
        for (i32 row = 0; row < 8; ++row) {       // v485
            for (i32 col = 0; col < 8; ++col) {
                TerrainTile* tile = &floor->tiles[row * 8 + col];
                if (tile->lod == 0) continue;       // if (*(v445+94))
                i32 vcount = tile->subdivCached;    // v148 = *(v445+16)
                i32 pcount = tile->polyCount;       // v368 = *(v445+32)

                // VIBE_Render_ComputeVertexClipFlags(clipFlag, vbuf, pbuf, vcount, pcount)
                if (hooks.computeClipFlags)
                    hooks.computeClipFlags(tile->clipFlag, tile->vertexBuf, tile->polyBuf,
                                           vcount, pcount);

                Vertex* vb = VBuf(tile);
                // Project each clipped vertex (its +76 high bit set) — lines 1751..1814.
                // (We project unconditionally when no clip-flags hook ran: treat all
                //  vertices as on-screen, matching the default no-op outcode.)
                for (i32 i = 0; i < vcount; ++i) {
                    Vertex& v = vb[i];
                    if (v.z == 0.0f) continue;        // guard the 1/z divide
                    float invZ = 1.0f / v.z;          // 1.0 / *(k+8)
                    // screenX = projXScale * x * (1/z) + projXOff   (lines 1759/1768)
                    v.screenX = st.projXScale * v.x * invZ + st.projXOff;
                    // screenY = projYScale * y * (1/z) + projYOff   (lines 1763/1769)
                    v.screenY = st.projYScale * v.y * invZ + st.projYOff;
                    // (the byte_649DD8 fog-shade branch writes a +79 shade byte; modelled
                    //  below when fogShade is set.)
                    if (st.fogShade) {
                        float z2 = v.x*v.x + v.y*v.y + v.z*v.z;  // v153
                        double shade;
                        if (z2 <= st.fogNear) {
                            shade = kFogShadeCap;                // 255.0
                        } else {
                            double d = (std::sqrt((double)z2) - st.fogStart) * st.fogScale; // v269
                            if (kFogShadeCap >= d) shade = kFogShadeCap - d;
                            else shade = kFogShadeCap - kFogShadeCap;   // clamp (engine 250..)
                        }
                        // *(k+79) = (int)shade  — stored in a pad byte; not modelled in
                        // Vertex, so consumed via the light index path. (no-op store.)
                        (void)shade;
                    }
                }

                // Backface cull per poly (lines 1815..1836): signed screen-area test.
                Polygon* pb = PBuf(tile);
                for (i32 i = 0; i < pcount; ++i) {
                    Polygon& p = pb[i];
                    if ((i8)p.flags36 >= 0) continue;     // if (*(n+36) < 0)
                    Vertex* a = p.v0; Vertex* b = p.v1; Vertex* c = p.v2;
                    // clip-bit gate (line 1822): if any vertex +76 has bit 0x10, cull.
                    // (default outcode is 0, so this branch is skipped.)
                    float area = (a->screenX - c->screenX) * (a->screenY - b->screenY)
                               - (a->screenX - b->screenX) * (a->screenY - c->screenY);
                    if (area <= 0.0f) p.flags36 &= ~0x40u;   // front-facing (line 1830)
                    else              p.flags36 &= ~0x80u;   // backface -> clear visible bit (1832)
                }
            }
        }

        floor->flatLit &= ~1u;     // *(Floor+7280) &= ~1 (line 1845)
    }

    // ---- Phase 2: present-coupled tail (lines 1847..1934) ------------------
    // if (a2) AnimateWaterVertices(...)  (line 1847) — driven by the caller's meshes.
    if (animateWater) {
        // The water-vertex animation half lives in floorwater / water_anim; the walk
        // just gates it here. (No floor-side state to mutate in this reconstruction.)
    }

    DrawList* dl = st.drawList;
    for (i32 row = 0; row < 8; ++row) {           // v484
        for (i32 col = 0; col < 8; ++col) {
            TerrainTile* tile = &floor->tiles[row * 8 + col];
            if (tile->lod == 0) continue;           // if (*(v447+94))

            // Floor bbox accumulate (lines 1859..1868):
            if (st.bboxMinX >= (double)tile->bboxMinX) st.bboxMinX = tile->bboxMinX;
            if (st.bboxMaxY <= (double)tile->bboxMinY) st.bboxMaxY = tile->bboxMinY;

            // Per-tile light-table pointer (line 1869): &lightTable[26 * (clipFlag&0x3F)].
            // (stored into each visible poly +12; modelled via the hook.)

            // Per-frame counters (lines 1870..1878):
            i32 vc = tile->subdivCached;            // v165 = *(v447+16)
            i32 pc = tile->polyCount;               // v166 = *(v447+32)
            st.vertsThisFrame += vc;                // dword_13FC574 += v165
            st.vertsAlt       += vc;                // dword_13FC4E0 += v165
            st.polysThisFrame += pc;                // dword_13FC54C += v166

            if (dl) {
                // remaining capacity clamp (lines 1874..1877):
                i32 remain = dl->capacity - dl->count;          // v167
                i32 limit  = (pc < remain) ? pc : remain;       // min(v166, capacity-count)

                Polygon* pb = PBuf(tile);
                for (i32 i = 0; i < limit; ++i) {               // v168 loop
                    Polygon& p = pb[i];
                    if ((i8)p.flags36 >= 0) continue;           // if (*(ii+36) < 0)
                    u32 texId = (u32)p.uvZ;                     // *(ii+20)
                    u32 sortKey;
                    if (texId) {
                        // First-use mipmap build + frame stamp (lines 1887..1914). The
                        // texture-record byte_42 marker (==42) gate + LockSurfaceWait /
                        // BuildMipmap / Unlock are routed through the texture cache; here
                        // the texId is the already-bound record, so we just compute the key.
                        // sortKey = ((tex - dword_1406A84) >> 7) + 1   (line 1912)
                        sortKey = (texId / st.texBaseStride) + 1;
                        // *(tex+84) = dword_649D58 (frame stamp) — recorded via st.frameStamp.
                    } else {
                        sortKey = 0;                            // *v175 = 0 (line 1917)
                    }
                    // *(ii+12) = &lightTable[26*(clipFlag&0x3F)]  (line 1920) — light ptr.
                    DrawListEntry& e = dl->entries[dl->count];
                    e.sortKey = sortKey;                        // *(dword_13FC570)
                    e.poly    = &p;                             // *(dword_13FC570+4) = ii
                    ++dl->count;                                // ++dword_13FC770
                    ++polysAppended;
                }
            }

            // VIBE_Floor_TransformTileGeometry(floor, tile)  (line 1926)
            if (hooks.transformTileGeometry)
                hooks.transformTileGeometry(floor, tile);
        }
    }

    return polysAppended;
}

} // namespace guild::render
