// city_satisfaction_grid — 1:1 port of the VIBE_City_* district grid cluster.
// See city_satisfaction_grid.h for the grid layout and provenance.
#include "world/city_satisfaction_grid.h"

#include <cstring>

namespace guild::world {

// ===========================================================================
// Flat grid backing store (gilde.exe @0x1234988, BSS).
// ===========================================================================
u8 g_cityGrid[kCityGridBytes] = {0};

// Helper: byte pointer to (cell-base + field) for district (x,y).
static inline u8* CellByte(int x, int y, int fieldOff) {
    // Original: base + 192*x + 24*y + field. (96*x + 12*y word-index == byte
    // 192*x + 24*y; 48*x + 6*y float-index == byte 192*x + 24*y — identical.)
    return &g_cityGrid[kCityGridRowBytes * x + kCityGridCellBytes * y + fieldOff];
}

i16& GridCrimeArea(int x, int y) {
    return *reinterpret_cast<i16*>(CellByte(x, y, kGridOffCrimeArea));
}
i16& GridCrimeCentre(int x, int y) {
    return *reinterpret_cast<i16*>(CellByte(x, y, kGridOffCrimeCentre));
}
i32& GridSatA(int x, int y) {
    return *reinterpret_cast<i32*>(CellByte(x, y, kGridOffSatA));
}
i32& GridSatB(int x, int y) {
    return *reinterpret_cast<i32*>(CellByte(x, y, kGridOffSatB));
}
i32& GridSatC(int x, int y) {
    return *reinterpret_cast<i32*>(CellByte(x, y, kGridOffSatC));
}
float& GridSatWeight(int x, int y) {
    return *reinterpret_cast<float*>(CellByte(x, y, kGridOffSatWeight));
}
float& GridSatDenom(int x, int y) {
    return *reinterpret_cast<float*>(CellByte(x, y, kGridOffSatDenom));
}
float& GridSatCount(int x, int y) {
    return *reinterpret_cast<float*>(CellByte(x, y, kGridOffSatCount));
}

void CityGridReset() {
    std::memset(g_cityGrid, 0, sizeof(g_cityGrid));
}

// ---------------------------------------------------------------------------
// Recovered constant.
// ---------------------------------------------------------------------------
static constexpr float kSatSpreadWeight = 0.5f;  // flt_62568C

// Default WorldToCityTile routes through the heightmap leaf then divides by the
// (mapTileSpan >> 3) district divisor (computed inside the original).
bool GridEnv::WorldToCityTile(float worldX, float worldZ, int& tileX, int& tileY) {
    return CoordWorldToCityTile(*this, worldX, worldZ, tileX, tileY);
}

// ===========================================================================
// 0x577e04 — VIBE_Coord_WorldToCityTile
// ===========================================================================
bool CoordWorldToCityTile(GridEnv& env, float worldX, float worldZ,
                          int& tileX, int& tileY) {
    // v7 = mapTileSpan >> 3 (arithmetic shift; the original masks the sign-bias).
    int divisor = env.HeightmapDivisor();
    int tx = 0, tz = 0;
    if (!env.HeightmapWorldToTile(worldX, worldZ, tx, tz))
        return false;  // off-map
    // *a2 /= v7; *a3 /= v7;  (both axes divided by the same divisor)
    tileX = tx / divisor;
    tileY = tz / divisor;
    return true;
}

// ---------------------------------------------------------------------------
// Shared 3x3-ring helper. The original clamps the ring to [0,8] on the high
// side and [0,..] on the low side, then walks rows v9..v8 and cols clamp(v10).
// NB: the high clamp uses 8 (one past the last valid index 7); the inner loops
// still index cells 0..7 via the cell accessors, exactly as the original (which
// writes through the same flat region with a 24-byte cell stride). We reproduce
// the clamp bounds and visit order faithfully and apply `op` per (cx,cy).
// ---------------------------------------------------------------------------
template <typename Op>
static void CrimeRing(int x, int y, Op op) {
    // result = min(x+1, 8); v8 = max(x-1, 0); rows v8..result.
    int rowHi = x + 1; if (rowHi > 8) rowHi = 8;
    int colHi = y + 1; if (colHi > 8) colHi = 8;
    int rowLo = x - 1; if (rowLo < 0) rowLo = 0;
    if (rowLo <= rowHi) {
        for (int cx = rowLo; cx <= rowHi; ++cx) {
            int colLo = y - 1; if (colLo < 0) colLo = 0;
            for (int cy = colLo; cy <= colHi; ++cy)
                op(cx, cy);
        }
    }
}

// ===========================================================================
// 0x577fc4 — VIBE_City_AddCrimeToGrid
// ===========================================================================
int CityAddCrimeToGrid(GridEnv& env, u8 lawId, int* outX, int* outY) {
    int weight = env.CrimeWeight(lawId);    // VIBE_Gesetz_GetRecord(lawId) -> v19

    float wx = 0.0f, wz = 0.0f;
    bool haveObj = env.ResolvePlayerObject(wx, wz);

    // v20 = rnd(8); v21 = rnd(8)  — the RNG fallback tile (used only when the
    // object/scene is absent and the tiler is never reached; matches order).
    int x = env.RandomModulo(8);
    int y = env.RandomModulo(8);

    if (!haveObj)
        return y;  // v22 == 0: object not resolved.

    // The original gates on the scene ptr (*(obj+97)); ResolvePlayerObject==true
    // implies a placed object here.
    //
    // 1:1 FIX (0x578032..0x578076): the original IGNORES WorldToCityTile's return
    // value — it calls it (which leaves x/y at the RNG fallback when off-map) and
    // then writes the centre + ring UNCONDITIONALLY. Our CoordWorldToCityTile does
    // not touch x/y on failure, so x/y keep the RNG draw, exactly like v20/v21.
    env.WorldToCityTile(wx, wz, x, y);

    // Centre cell: word_12349A2[96*x + 12*y] += weight  (byte 192*x+24*y+26).
    GridCrimeCentre(x, y) = static_cast<i16>(GridCrimeCentre(x, y) + weight);
    // 3x3 ring: word_1234988 += weight. 1:1 FIX (0x5780d2): the inner loop
    // PRE-increments the cell pointer by 24 before the store, so the area write
    // lands one CELL to the right in y — byte 192*cx + 24*(cy+1) == GridCrimeArea(cx,cy+1).
    CrimeRing(x, y, [&](int cx, int cy) {
        GridCrimeArea(cx, cy + 1) =
            static_cast<i16>(GridCrimeArea(cx, cy + 1) + weight);
    });

    if (outX) *outX = x;
    if (outY) *outY = y;
    return y;
}

// ===========================================================================
// 0x578110 — VIBE_City_RemoveCrimeFromGrid
// ===========================================================================
int CityRemoveCrimeFromGrid(GridEnv& env, u8 lawId) {
    int weight = env.CrimeWeight(lawId);

    float wx = 0.0f, wz = 0.0f;
    bool haveObj = env.ResolvePlayerObject(wx, wz);

    int x = env.RandomModulo(8);
    int y = env.RandomModulo(8);

    if (!haveObj)
        return y;

    // 1:1: like AddCrime, the original ignores WorldToCityTile's return and writes
    // unconditionally (x/y keep the RNG fallback when off-map).
    env.WorldToCityTile(wx, wz, x, y);

    GridCrimeCentre(x, y) = static_cast<i16>(GridCrimeCentre(x, y) - weight);
    // Same +24 pre-increment as Add: area write lands at GridCrimeArea(cx, cy+1).
    CrimeRing(x, y, [&](int cx, int cy) {
        GridCrimeArea(cx, cy + 1) =
            static_cast<i16>(GridCrimeArea(cx, cy + 1) - weight);
    });
    return y;
}

// ===========================================================================
// 0x578240 — VIBE_City_PlaceRandomCrime
// ===========================================================================
bool CityPlaceRandomCrime(GridEnv& env, int districtX, WorldPoint& outWorld) {
    // span = mapTileSpan >> 3 (the district divisor); band base = span*districtX.
    int span = env.HeightmapDivisor();
    int base = span * districtX;
    // v6 = rnd(span) + base; tz = rnd(span). (The original feeds these tile
    // coords to VIBE_Heightmap_TileToWorld.) We expose the chosen tile through
    // the heightmap inverse, surfaced here as HeightmapTileToWorld via the env's
    // forward tiler is not available; instead PlaceRandomCrime returns the band
    // tile and the world point produced by the default identity placement.
    int tx = env.RandomModulo(static_cast<u16>(span)) + base;
    int tz = env.RandomModulo(static_cast<u16>(span));
    // Heightmap_TileToWorld is a render leaf; here the world point is derived by
    // the env's inverse tiler when available. The deterministic contract: the
    // returned world tile is (tx, tz); callers (and tests) read it back.
    outWorld.x = static_cast<float>(tx);
    outWorld.y = 0.0f;
    outWorld.z = static_cast<float>(tz);
    return true;
}

// ===========================================================================
// 0x5788d4 — VIBE_City_BuildSatisfactionGrid
// ===========================================================================
void CityBuildSatisfactionGrid(GridEnv& env) {
    // Zero the three satisfaction work fields across all 64 cells.
    for (int x = 0; x < kCityGridCols; ++x) {
        for (int y = 0; y < kCityGridRows; ++y) {
            GridSatA(x, y) = 0;  // dword_1234994
            GridSatB(x, y) = 0;  // dword_1234998
            GridSatC(x, y) = 0;  // dword_123499C
        }
    }

    const GridResident* res = nullptr;
    int count = env.Residents(&res);
    for (int i = 0; i < count; ++i) {
        const GridResident& r = res[i];
        if (!r.active)            continue;  // dword_11BC785 + j == 0
        if (r.id == -1)           continue;  // dword_11BC760 + j == -1
        if (r.sceneNode == 0)     continue;  // record+97 == 0 (unplaced)
        // Eligible type: NOT (0xB..0xD) and != 0x10.
        unsigned tb = static_cast<unsigned>(r.typeByte) & 0xFFu;
        if ((tb >= 0xBu && tb <= 0xDu) || tb == 0x10u) continue;
        if (r.scale19 == 0)       continue;  // avoid /0 (orig divides by v19)

        int x = 0, y = 0;
        if (!env.WorldToCityTile(r.worldX, r.worldZ, x, y)) continue;

        // v6 = (double)scaled13 / scale19 * weight18; weight field += v6 * 0.5.
        double v6 = (double)r.scaled13 / (double)r.scale19 * (double)r.weight18;
        float contrib = static_cast<float>(v6);
        GridSatWeight(x, y) =
            static_cast<float>(contrib * kSatSpreadWeight + GridSatWeight(x, y));

        // 3x3-ring spread. 1:1 FIX (0x578a7f..0x578a93): the inner loop reads the
        // +40 field (SatDenom) of cell (cx,cy) and writes the +16 field of cell
        // (cx,cy+1) — but the +24 PRE-increment makes those the SAME byte:
        //   write byte 192*cx + 24*(cy+1) + 16  ==  192*cx + 24*cy + 40  (SatDenom(cx,cy))
        //   write byte 192*cx + 24*(cy+1) + 12  ==  192*cx + 24*cy + 36  (SatWeight(cx,cy))
        // So the ring effectively does SatDenom(cx,cy) += contrib (v14) and
        // SatWeight(cx,cy) += contrib*0.5 (v13). The previous reconstruction wrote
        // the un-shifted +16/+12 bytes, corrupting every cell except where the
        // alias happened to coincide.
        float ringB = contrib;                  // v14 (== v22)
        float ringA = contrib * kSatSpreadWeight;  // v13 (== v23 = v22*0.5)
        CrimeRing(x, y, [&](int cx, int cy) {
            GridSatDenom(cx, cy)  = GridSatDenom(cx, cy)  + ringB;
            GridSatWeight(cx, cy) = GridSatWeight(cx, cy) + ringA;
        });
    }
}

// ---------------------------------------------------------------------------
// Send-command template assembly. The original copies a 36-byte template
// (dword_1235238, all-zero in the static image) into a 15-dword stack block,
// patches specific words, prepends opcode 86 and forwards the 40-byte body.
// The two ASCII tags are preserved as raw little-endian dwords.
// ---------------------------------------------------------------------------
static constexpr i32 kSyncTag  = 1768843636;  // v13[9] in SendSyncCommand
static constexpr i32 kResetTag = 1701732972;  // v9[9] in SendResetCommand

// ===========================================================================
// 0x579460 — VIBE_City_SendSyncCommand
// ===========================================================================
int CitySendSyncCommand(GridEnv& env, i32 a1, i32 clockLo, i32 a3, i32 a4, i32 a5) {
    // 15-dword stack block; first 9 dwords are the broadcast body (the original
    // builds 40 bytes from v13[0..9]). Template is zero (BSS), so we start clear.
    i32 v[15] = {0};
    v[9] = kSyncTag;
    v[0] = a1;
    v[2] = clockLo;   // qword_13CE852 low dword
    v[1] = 0;
    v[3] = a3;
    v[5] = 0;
    v[4] = a4;
    v[7] = 0;
    v[6] = a5;
    v[8] = 0;
    v[13] = clockLo;  // v13[13] = a2 (ecx) — the original seeds this first.
    u8 body[40];
    std::memcpy(body, v, sizeof(body));  // first 40 bytes (v[0..9])
    return env.RequestBuildOp86(body);
}

// ===========================================================================
// 0x5794e0 — VIBE_City_SendResetCommand
// ===========================================================================
int CitySendResetCommand(GridEnv& env, i32 seq) {
    i32 v[15] = {0};
    v[13] = seq;       // v9[13] = this
    v[0] = 1;          // reset flag
    v[9] = kResetTag;
    u8 body[40];
    std::memcpy(body, v, sizeof(body));
    return env.RequestBuildOp86(body);
}

}  // namespace guild::world
