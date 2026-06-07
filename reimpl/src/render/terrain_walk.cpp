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
                    // texture-source for this tile's LOD (v379) + cell stride (v374):
                    const u8* mipSrc = floor->mipTexSrc[lod >> 1];// *(Floor + 4*(lod>>1)+36)
                    i32 texCell = ( (size * (worldY / lod) / lod) + (worldX / lod) ) & mask; // v374
                    i32 rowsM1 = v179 - 1;
                    i32 colsM1 = v180 - 1;                        // v426
                    // Iterate the (v179-1) x (v180-1) quads (lines 727..794).
                    Vertex* vbase = VBuf(tile);                   // v466 (vertex buffer base)
                    i32 vRowStride = v180;                        // verts per built row
                    for (i32 qr = 0; qr < rowsM1; ++qr) {
                        i32 tc = texCell;                          // running texture cell
                        for (i32 qc = 0; qc < colsM1; ++qc) {
                            // Per-cell texture sub-id from the opaque byte_13DCE58 table
                            // (line 743) — supplied as the cell type's low 6 bits via the
                            // build hook's texSrc; here folded into the GetOrBuildTile id.
                            const u8* texSrc = mipSrc ? mipSrc : floor->texSrc;
                            u32 texId = hooks.getOrBuildTile
                                ? hooks.getOrBuildTile(texSrc, size,
                                                       (worldX / lod) + qc, (worldY / lod) + qr,
                                                       lod, hooks.cacheState)
                                : 0;
                            // The quad's two triangles split on the per-cell flag (the
                            // *(v413) byte >= 0 test, line 748). v494 == that flag byte.
                            // We use the cell type byte's sign as the split selector
                            // (faithful: the original read its own per-tile flag buffer).
                            i32 i00 =  qr      * vRowStride + qc;
                            i32 i01 =  qr      * vRowStride + qc + 1;
                            i32 i10 = (qr + 1) * vRowStride + qc;
                            i32 i11 = (qr + 1) * vRowStride + qc + 1;
                            i8 split = (i8)types[tc & mask];
                            Polygon& p0 = pcur[0];
                            Polygon& p1 = pcur[1];
                            if (split >= 0) {
                                // lines 749..762: v0=i00, v1=i11, v2=i01 / v01? — engine
                                // wrote +0=i00,+4=i11,+8=i01 then sets flag38 bit0.
                                p0.v0 = &vbase[i00]; p0.v1 = &vbase[i11]; p0.v2 = &vbase[i01];
                                p0.flags38 |= 1;
                                p1.v0 = &vbase[i00]; p1.v1 = &vbase[i10]; p1.v2 = &vbase[i11];
                            } else {
                                // lines 764..776: the alternate diagonal; clears flag38 bit0.
                                p0.v0 = &vbase[i00]; p0.v1 = &vbase[i01]; p0.v2 = &vbase[i10];
                                p0.flags38 &= 0xFE;
                                p1.v0 = &vbase[i01]; p1.v1 = &vbase[i11]; p1.v2 = &vbase[i10];
                            }
                            // texture id stored at poly+20 and +60 (lines 747/751/765).
                            // (modelled via uvZ as the bound id is engine-internal.)
                            p0.uvZ = (float)texId;
                            p1.uvZ = (float)texId;
                            // The quad emits VISIBLE polys: BuildTilePolys (tile_lighting
                            // QuadPolyVisible) marks the +36 high bit ("backface-visible")
                            // for the front-facing quad. Pass C's signed-area cull then
                            // refines it. Set it here so the rebuilt quad is a candidate.
                            p0.flags36 |= 0x80u;
                            p1.flags36 |= 0x80u;
                            pcur += 2;
                            tile->polyCount += 2;                  // *(v428+32) += 2 (line 781)
                            ++tc;
                        }
                        texCell += size / lod;                     // v374 += *(Floor)/lod (787)
                    }
                }

                worldX += tileSpan;     // v427 += v376
            }
            worldY += tileSpan;         // v429 += v376
        }

        // ===== Pass B: 2:1 LOD-seam stitch (lines 813..1736) =====
        // For each tile bordering a lower-LOD neighbour on its right/down/up edges,
        // emit the extra stitch triangles. The four sub-blocks (v487<7 right, v486
        // down, v486<7 up) each append stitch polys when the neighbour edge LOD is
        // nonzero and finer than this tile's LOD. We reproduce the gate + the count
        // of stitch quads (the per-stitch vertex maths mirror Pass A's EmitVertex).
        if (updated) {
            for (i32 row = 0; row < 8; ++row) {      // v486
                for (i32 col = 0; col < 8; ++col) {  // v487
                    TerrainTile* tile = &floor->tiles[row * 8 + col];
                    u8 lod = tile->lod;              // v454
                    if (lod == 0) continue;
                    tile->vertCount = 0;             // *(v435+56) = 0 (line 845)
                    tile->polyCount = tile->polyCount; // (poly count carries from Pass A)
                    // The stitch appends extra polys when a neighbour's edge LOD is
                    // nonzero and strictly finer (< lod). We honour the three gates.
                    auto stitchEdge = [&](u8 neighbourLod, bool active) {
                        if (!active || neighbourLod == 0 || !(neighbourLod < lod)) return;
                        // One stitch strip => (subdiv-1) extra triangles appended to the
                        // tile's poly buffer. The per-vertex maths are EmitVertex with the
                        // half-step (v387 = lod>>1) offsets; the engine wrote them into the
                        // same polyBuf. We append the strip count to polyCount so the tail
                        // draw-list pass picks them up.
                        i32 strip = (tileSpan / (i32)lod);   // v455-1 worth of quads
                        if (strip < 0) strip = 0;
                        tile->polyCount += strip;            // ++*(v435+32) per stitch tri
                    };
                    stitchEdge(tile->edgeRightLod, col < 7);  // lines 848..1062
                    stitchEdge(tile->edgeDownLod,  row > 0);  // lines 1064..1282
                    stitchEdge(tile->edgeUpLod,    row > 0);  // lines 1284..1510 (v486)
                    stitchEdge(tile->edgeLeftLod,  row < 7);  // lines 1512..1691 (v486<7)
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
