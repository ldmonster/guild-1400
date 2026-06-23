// =============================================================================
// guild::render — scene floor (terrain) block, the full LoadFloorRegions
// grammar. See scene_floor.h for the decompiled layout / provenance notes.
//   gilde.exe 0x5e78a8  VIBE_WorldIo_LoadFloorRegions
//   gilde.exe 0x5dcca0  VIBE_Bio_ReadArrayQuick
//   gilde.exe 0x5bd44c  VIBE_Floor_LoadFromHeightmap (pure-math mirrors)
// =============================================================================
#include "render/scene_floor.h"

#include "render/scene_load.h"   // SceneReader / ParseSceneHeader (0x5e7e38)

namespace guild::render {

namespace {

// ---------------------------------------------------------------------------
// Object-body SKIP walk for the shipped scene version (tag 0x3A6C00BB class).
// This is the byte grammar of VIBE_WorldIo_ReadObject @0x5e67c8 exactly as
// reconstructed (and live-proven over the whole AUGSBURG city) by
// src/play/scene_view.cpp ReadObject — reproduced here as a pure SKIP so this
// render-layer module reaches the floor flag without depending on the play
// layer. Any grammar fix must be applied to BOTH readers (they are the same
// function's reconstruction). Validated to land on the floor flag to the byte
// in all 10 shipped Staedte scenes (scene_floor tests).
// ---------------------------------------------------------------------------

// VIBE_Object_Spawn @0x5b054c: for kind>=5 the type byte is chosen from the
// name's first char ('r'->6, 's'->8, 'p'->7, else 5).
int TypeFromName(char c0) {
    const unsigned char v = (unsigned char)c0;
    if (v >= 0x72) {            // >= 'r'
        if (v <= 0x72) return 6;   // 'r'
        if (v == 0x73) return 8;   // 's'
        return 5;
    }
    if (v == 0x70) return 7;       // 'p'
    return 5;
}

// VIBE_Event_LoadEventBindings @0x5f4bc8: u32 count, then count*(string,string).
void SkipEventBindings(SceneReader& r) {
    const u32 n = r.ReadDword();
    for (u32 i = 0; i < n && !r.atEnd(); ++i) { r.ReadString(); r.ReadString(); }
}

void SkipObject(SceneReader& r, u32 ver) {
    if (r.atEnd()) return;
    const u8 present = r.ReadByte();
    if (present) {
        const std::string name = r.ReadString();
        if (ver >= 0x3A6C00B2u) r.ReadDword();   // +512 owner-object id
        if (ver >= 0x3A6C00ABu) r.ReadDword();   // +535
        const u32 kind = r.ReadDword();
        if (ver >= 0x3A6C00A6u) r.ReadByte();    // explicit-kind byte
        const int type = ((int)kind < 5) ? (int)kind
                                         : TypeFromName(name.empty() ? 0 : name[0]);
        if (type == 0) {                          // light / sfx node
            r.ReadByte(); r.ReadDword();
            r.ReadVec3(); r.ReadVec3(); r.ReadVec3();
        } else if (type == 1 || type == 4) {      // mesh object
            r.ReadByte(); r.ReadByte();
            if (ver >= 0x3A6C000Du) {
                r.ReadByte();
                if (ver >= 0x3A6C00A6u) {
                    r.ReadByte(); r.ReadByte();
                    if (ver >= 0x3A6C00B4u) r.ReadByte();
                    if (ver >= 0x3A6C00B9u) r.ReadByte();
                }
            }
            r.ReadDword();                        // +532
            const u32 lodN = r.ReadDword();
            if ((int)lodN > 0) r.ReadString();    // mesh name
            r.ReadVec3(); r.ReadVec3();           // +76 pos, +132 euler
            if (r.ReadByte()) { r.ReadVec3(); r.ReadVec3(); }   // +92 / +144
            if (ver >= 0x3A6C00AFu)
                for (int i = 0; i < 10; ++i) { r.ReadVec3(); r.ReadVec3(); }
        } else if (type == 2 || type == 3) {      // dummy / locator
            r.ReadVec3(); r.ReadVec3();
            if (r.ReadByte()) { r.ReadVec3(); r.ReadVec3(); }
            if (ver >= 0x3A6C00AFu)
                for (int i = 0; i < 10; ++i) { r.ReadVec3(); r.ReadVec3(); }
        } else {                                  // 5..8 animated/light/particle
            r.ReadByte();
            if (ver >= 0x3A6C00A9u) r.ReadByte();
            r.ReadDword(); r.ReadDword(); r.ReadDword();
            r.ReadVec3(); r.ReadVec3(); r.ReadVec3();
            if (ver >= 0x3A6C00ACu) { r.ReadDword(); r.ReadDword(); }
            const int kf = (ver < 0x3A6C00BAu) ? 6 : 7;
            for (int i = 0; i < kf; ++i) {
                r.ReadDword(); r.ReadDword(); r.ReadDword();
                r.ReadVec3(); r.ReadVec3(); r.ReadVec3();
                if (ver >= 0x3A6C00ACu) { r.ReadDword(); r.ReadDword(); }
            }
        }
    }
    if (r.atEnd()) return;
    if (r.ReadByte()) SkipObject(r, ver);         // child
    if (r.atEnd()) return;
    if (r.ReadByte()) SkipObject(r, ver);         // sibling
    if (ver >= 0x3A6C00A7u) SkipEventBindings(r);
}

bool IsPow2(u32 v) { return v != 0 && (v & (v - 1)) == 0; }

// gilde.exe 0x5dcca0 — VIBE_Bio_ReadArrayQuick (stream@eax, expected@edx,
// out@ebx): u32 elemSize, u32 count; if (count == expected) alloc+read
// count*elemSize bytes, else seek forward count*elemSize and return null
// ("bio_rd_array_quick: Tried to load invalid array-sizes...").
SceneFloorArray ReadArrayQuick(SceneReader& r, u32 expectedCount) {
    SceneFloorArray a;
    a.elemSize = r.ReadDword();
    a.count    = r.ReadDword();
    // payload size: the original computes count*elemSize with a 32-bit imul
    // (alloc size) and reads/seeks exactly that many bytes.
    const u32 payload = a.count * a.elemSize;
    if (a.count == expectedCount && !r.eof()) {
        a.data.reserve(payload);
        for (u32 i = 0; i < payload && !r.eof(); ++i) a.data.push_back(r.ReadByte());
        a.accepted = !r.eof() && a.data.size() == payload;
        if (!a.accepted) a.data.clear();          // truncated stream -> no array
    } else {
        for (u32 i = 0; i < payload && !r.eof(); ++i) (void)r.ReadByte();  // Vfs_Seek
    }
    return a;
}

// gilde.exe 0x5dc980 — VIBE_Bio_ReadVec4 (4 raw LE floats).
void ReadVec4(SceneReader& r, float out[4]) {
    for (int i = 0; i < 4; ++i) out[i] = r.ReadFloat();
}

// One water-region record (the @0x5e79d3..0x5e7c3c loop body), stream fields
// only — the Texture_LoadByName/UploadToSurface side effects are not replayed
// (rec+0/+4 receive the runtime Texture*, see scene_floor.h).
SceneFloorWaterRegion ReadWaterRegion(SceneReader& r, u32 ver) {
    SceneFloorWaterRegion w;
    w.texName  = r.ReadString();                 // @0x5e79dc
    w.texByteA = r.ReadByte();                   // @0x5e79ea (read, unused)
    w.texByteB = r.ReadByte();                   // @0x5e79f8 -> 4*(b&0xF) tex flag
    w.texByteC = r.ReadByte();                   // @0x5e7a06 -> (b&1) tex flag
    for (int i = 0; i < 4; ++i) w.flagBytes[i] = r.ReadByte();  // @0x5e7a6d..0x5e7ae1
    // rec+8 packing (@0x5e7a89/0x5e7aa5/0x5e7ad8/0x5e7b04): byte0, byte1 raw;
    // byte2 = (b2&1) | ((b3&1)<<1); byte3 = 0 (the dword is zeroed first).
    w.packedFlags = (u32)w.flagBytes[0] | ((u32)w.flagBytes[1] << 8) |
                    ((u32)((w.flagBytes[2] & 1) | ((w.flagBytes[3] & 1) << 1)) << 16);
    if (ver < kFloorVerWaterV20) {
        // @0x5e7dbf: rec+20 = 0x427C0000 (63.0f, exactly representable)
        w.param20 = detail::F32FromBits(0x427C0000u);
    } else {
        w.param20 = detail::F32FromBits(r.ReadDword()); // @0x5e7b4e -> rec+20
    }
    w.raw12 = r.ReadDword();                     // @0x5e7b61 -> rec+12
    w.raw16 = r.ReadDword();                     // @0x5e7b74 -> rec+16
    ReadVec4(r, w.vecA);                         // @0x5e7b87 -> rec+24
    if (ver < kFloorVerVec4Raw) {
        // @0x5e7b9b..0x5e7bd8: legacy scale by flt_62BEA0 @0x62BEA0
        // (dword 0x3EAAAAAB ~ 1/3, verified get_int), x87 single*single
        // stored single.
        const float k = detail::F32FromBits(0x3EAAAAABu);
        for (int i = 0; i < 4; ++i)
            w.vecA[i] = (float)((double)w.vecA[i] * (double)k);
    }
    ReadVec4(r, w.vecB);                         // @0x5e7bea -> rec+40
    if (ver < kFloorVerWaterTail) {
        w.tail340 = 0;                           // @0x5e7dd2
    } else {
        w.tail340 = (u8)r.ReadDword();           // @0x5e7c04/0x5e7c17 (low byte)
    }
    return w;
}

} // namespace

// gilde.exe 0x5e78a8 — the floor-regions body (post floor-flag).
SceneFloorBlock ParseFloorRegions(SceneReader& r, u32 ver) {
    SceneFloorBlock b;
    b.headerOk = true;
    b.floorPresent = true;
    b.ver = ver;
    if (ver < kFloorVerMin)                       // @0x5e78bf
        return b;                                 // "Incompatible Floor-Versions"

    b.name = r.ReadString();                      // ctx+0   @0x5e78ec
    if (!b.name.empty() && ver >= kFloorVerGrids) {
        b.gridN = r.ReadDword();                  // ctx+156 @0x5e790c
        b.heights = ReadArrayQuick(r, b.gridN);   // ctx+64  @0x5e791a
        if (ver >= kFloorVerLightOffs)
            b.lightOffsets = ReadArrayQuick(r, 4 * b.gridN); // ctx+140 @0x5e793a
    }
    if (ver >= kFloorVerWater) {                  // @0x5e7945
        b.waterFlag = r.ReadByte();               // @0x5e7954
        if (b.waterFlag) {
            b.waterRegionCount = (i32)r.ReadDword();  // ctx+152 @0x5e7970
            if (b.waterRegionCount > 0) {
                if (ver >= kFloorVerGrids)
                    b.waterHeights = ReadArrayQuick(r, b.gridN); // ctx+68 @0x5e7998
                // 344*count "d3_int_io:FloorWaterRegions" records @0x5e79c2.
                for (i32 i = 0; i < b.waterRegionCount && !r.eof(); ++i)
                    b.waterRegions.push_back(ReadWaterRegion(r, ver));
            }
        }
    }
    b.textureName = r.ReadString();               // ctx+72  @0x5e7c48
    if (ver >= kFloorVerGrids && !b.textureName.empty())
        b.textureGrid = ReadArrayQuick(r, b.gridN); // ctx+136 @0x5e7c6c
    if (ver <= kFloorVerLegacyStrs)               // @0x5e7c79: 5 legacy strings
        for (int i = 0; i < 5; ++i) (void)r.ReadString();
    // typeNames: 8 slots when ver >= 0x3A6C00B0 (loc_5E7C9C, end = +0x200),
    // 6 slots otherwise (loc_5E7DDF, end = +0x180).
    b.typeNameCount = (ver >= kFloorVerTypeNames8) ? 8 : 6;
    for (int i = 0; i < b.typeNameCount; ++i)
        b.typeNames[i] = r.ReadString();          // ctx+164+64*i
    {   // cellScale/heightScale are FLOATS read via Bio_ReadDword @0x5e7cc5/0x5e7cd3
        b.cellScale   = r.ReadFloat();            // ctx+144
        b.heightScale = r.ReadFloat();            // ctx+148
    }
    if (ver <= kFloorVerLegacyStrs)               // @0x5e7ce0: 256 legacy strings
        for (int j = 0; j < 256; ++j) (void)r.ReadString();
    // (VIBE_Floor_LoadFromHeightmap(ctx) is invoked here @0x5e7cfe — the Floor
    //  build itself is the terrain module's; see FloorPlacement for the math.)
    if (ver >= kFloorVerOriginVec3) {             // @0x5e7d18
        SceneVec3 v = r.ReadVec3();
        b.origin[0] = v.x; b.origin[1] = v.y; b.origin[2] = v.z;
        b.hasOrigin = true;                       // -> floor +144/+148/+152
    }
    b.ok = !r.eof();
    return b;
}

SceneFloorBlock ParseSceneFloorBlock(const u8* ed3, std::size_t size) {
    SceneFloorBlock b;
    if (!ed3 || size < 8)
        return b;

    SceneReader r(ed3, size);
    SceneHeader h;
    if (!ParseSceneHeader(r, h))                  // 0x5e7e38 header + tag gate
        return b;
    b.headerOk = true;
    b.ver = h.tag;

    const u32 objCount = r.ReadDword();           // object-list count
    for (u32 i = 0; i < objCount && !r.atEnd(); ++i)
        SkipObject(r, h.tag);                     // 0x5e67c8 grammar (skip)
    if (r.atEnd() || r.eof())
        return b;

    const u8 floorFlag = r.ReadByte();            // 0x5e7e38: floor flag byte
    b.floorPresent = floorFlag != 0;
    if (!floorFlag)
        return b;                                 // no floor block in this scene

    SceneFloorBlock body = ParseFloorRegions(r, h.tag);
    body.headerOk = true;
    body.floorPresent = true;
    return body;
}

SceneFloorHeights ParseSceneFloorHeights(const u8* ed3, std::size_t size) {
    SceneFloorHeights f;
    const SceneFloorBlock b = ParseSceneFloorBlock(ed3, size);
    if (!b.headerOk || !b.floorPresent)
        return f;
    // Compatibility mapping (see scene_floor.h): the old "three size dwords"
    // were really [N][ArrayQuick elemSize][ArrayQuick count].
    f.name  = b.name;
    f.sizeX = b.gridN;
    f.sizeY = b.heights.elemSize;
    f.third = b.heights.count;
    if (!b.heights.accepted ||                    // count != N -> engine gets null
        f.sizeX != f.sizeY || !IsPow2(f.sizeX) || f.sizeX > 4096)
        return f;                                 // malformed / unexpected grid
    f.heights = b.heights.data;
    f.ok = true;
    return f;
}

// ---------------------------------------------------------------------------
// Pure-math mirrors of VIBE_Floor_LoadFromHeightmap @0x5bd44c.
// ---------------------------------------------------------------------------

FloorPlacement DeriveFloorPlacement(i32 n, float cellScale, float heightScale) {
    FloorPlacement p;
    // @0x5bd716..0x5bd75c (x87 doubles, stored single):
    //   v146 = -N; originX = (double)v146 * flt_628ADC * cellScale
    //   originY = heightScale * flt_628AE0
    //   originZ = flt_628ADC * (double)N * cellScale
    //   flt_628ADC @0x628ADC = 0x3F000000 (0.5)
    //   flt_628AE0 @0x628AE0 = 0xC2800000 (-64.0)
    //     [get_int over the binary: u32le 3263168512 == 0xC2800000 == -64.0f,
    //      double-confirmed by the float-constant sweep; the earlier -50.0
    //      (0xC2480000) documentation was a misread of the neighbouring dword.]
    p.originX = (float)((double)(-n) * 0.5 * (double)cellScale);
    p.originY = (float)((double)heightScale * -64.0);
    p.originZ = (float)(0.5 * (double)n * (double)cellScale);
    // @0x5bd768..0x5bd7f8: axisU=(cellScale,0,0), axisV=(0,0,-cellScale),
    // axisH=(0,heightScale,0).
    p.axisU[0] = cellScale;   p.axisU[1] = 0;           p.axisU[2] = 0;
    p.axisV[0] = 0;           p.axisV[1] = 0;           p.axisV[2] = -cellScale;
    p.axisH[0] = 0;           p.axisH[1] = heightScale; p.axisH[2] = 0;
    // @0x5bd7c1: floor+4 = N/8 (signed idiv via the sar/sbb sequence).
    p.tileSpan = n / 8;
    return p;
}

void NormalizeFloorTextureGrid(u8* grid, std::size_t count,
                               u8* outMin, u8* outMax) {
    // @0x5bd8ab..0x5bdc95: v83(min)=255, v84(max)=0; for each byte b:
    //   if (max <= b) max = b;  if (min >= b) min = b;
    // then every byte -= min (0-based indices into the 8 texture slots).
    int mn = 255, mx = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (mx <= grid[i]) mx = grid[i];
        if (mn >= grid[i]) mn = grid[i];
    }
    for (std::size_t i = 0; i < count; ++i)
        grid[i] = (u8)(grid[i] - (u8)mn);
    if (outMin) *outMin = (u8)mn;
    if (outMax) *outMax = (u8)mx;
}

// ---------------------------------------------------------------------------
// City Heightmap build (VIBE_Heightmap_BuildTerrainMesh @0x5c5610 scales).
// ---------------------------------------------------------------------------

bool BuildCityHeightmapFromFloor(const SceneFloorHeights& f,
                                 const float lo[3], const float hi[3],
                                 Heightmap& hm, std::vector<u8>& storage) {
    if (!f.ok || f.heights.empty())
        return false;
    storage = f.heights;                          // caller-owned height bytes
    hm = Heightmap{};
    hm.size    = (i32)f.sizeX;
    hm.heights = storage.data();
    // X/Z mapping: the 1:1 grid-scale core of BuildTerrainMesh @0x5c5610
    // (scaleX = (maxX-minX)/(size-1.75); originZ = maxZ, scaleZ negative).
    DeriveGridScaleXZ(&hm, lo[0], hi[0], hi[2], lo[2]);
    // Y mapping (@0x5c5b95 / @0x5c5bd8..0x5c5bec):
    //   originY = minY + 1.0 (fld1; fadd minY; fstp — a single-precision add)
    //   scaleY  = (maxY - minY) * flt_628BA8 (x87: fld maxY; fsub minY;
    //             fmul flt_628BA8; fstp single).
    //   flt_628BA8 @0x628BA8 = 0x3B81848E = 0.0039525721f (~ 1/253.0) —
    //   the EXACT dword, kTerrainScaleYNorm (heightmap.h).
    hm.originY = lo[1] + 1.0f;
    hm.scaleY  = (float)(((double)hi[1] - (double)lo[1]) *
                         (double)kTerrainScaleYNorm);
    return true;
}

bool BuildCityHeightmapFromFloor(const SceneFloorBlock& b,
                                 const float lo[3], const float hi[3],
                                 Heightmap& hm, std::vector<u8>& storage) {
    if (!b.ok || !b.heights.accepted || b.heights.data.empty())
        return false;
    SceneFloorHeights f;
    f.ok      = true;
    f.name    = b.name;
    f.sizeX   = b.gridN;
    f.sizeY   = b.heights.elemSize;
    f.third   = b.heights.count;
    f.heights = b.heights.data;
    return BuildCityHeightmapFromFloor(f, lo, hi, hm, storage);
}

} // namespace guild::render
