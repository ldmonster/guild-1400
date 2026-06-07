#pragma once
// City district satisfaction / reputation grid (gilde.exe).
//
// This module recovers the cluster of VIBE_City_* functions that maintain the
// 8x8 city DISTRICT GRID — the spatial standing/reputation score the simulation
// uses to colour districts by crime pressure and resident satisfaction (the
// "Ruf"/standing of a district, distinct from a person's +0x1B1 reputation byte
// done in sim/command_apply and the AI favorability scorers). Crime events raise
// a per-district crime accumulator (with a 3x3 neighbourhood spread); residents
// raise a per-district population/satisfaction accumulator; a per-tick rebuild
// folds law state into the grid; and two network commands broadcast / reset it.
//
// Translated functions:
//   VIBE_Coord_WorldToCityTile     0x577e04 — world coords -> 0..7 district tile
//   VIBE_City_AddCrimeToGrid       0x577fc4 — add a crime weight to a district + ring
//   VIBE_City_RemoveCrimeFromGrid  0x578110 — subtract a crime weight (inverse)
//   VIBE_City_PlaceRandomCrime     0x578240 — pick a random world point in a band
//   VIBE_City_BuildSatisfactionGrid 0x5788d4 — rebuild the satisfaction floats
//   VIBE_City_SendSyncCommand      0x579460 — opcode-86 grid sync broadcast
//   VIBE_City_SendResetCommand     0x5794e0 — opcode-86 grid reset broadcast
//
// THE GRID (gilde.exe @0x1234988, all BSS / zero in the static image). The
// original addresses it through several named base pointers that all alias one
// flat region with a 192-byte ROW stride (8 rows) and a 24-byte CELL stride
// (8 cells/row). The per-cell fields the recovered functions touch:
//   word_1234988  cell+0   i16  crime "centre"/area accumulator (Add/Remove ring)
//   word_12349A2  cell+26  i16  crime centre cell (Add/Remove direct write)
//   dword_1234994 cell+12  i32  satisfaction work field A (Build reset/accumulate)
//   dword_1234998 cell+16  i32  satisfaction work field B
//   dword_123499C cell+20  i32  satisfaction work field C
//   flt_12349AC   cell+36  f32  satisfaction weight (Build write)
//   flt_12349B0   cell+40  f32  satisfaction denom (Aggregate, clamped >= 1.0)
//   flt_12349B4   cell+44  f32  satisfaction resident count (Aggregate)
// INTENTIONAL FIELD OVERLAP (load-bearing!): the cell stride is 24 bytes but the
// per-cell field offsets reach +44 (SatCount). Therefore the "satisfaction"
// fields of cell (x,y) ALIAS the low fields of cell (x,y+1):
//   SatWeight(x,y) @+36  IS  SatA(x,y+1) @+12   (same byte 192*x+24*y+36)
//   SatDenom(x,y)  @+40  IS  SatB(x,y+1) @+16
//   SatCount(x,y)  @+44  IS  SatC(x,y+1) @+20
// The original relies on this overlap (its named globals genuinely share storage),
// so the rebuild's centre-write and ring-spread accumulate into overlapping bytes.
// We replicate it byte-exactly with one flat buffer; do NOT "fix" the overlap.
//
// To stay byte-faithful AND testable we back the whole region with one flat
// buffer and reproduce the exact (192*x + k*y + field) pointer arithmetic. The
// cross-module leaves (law record, entity resolve, heightmap, RNG, network
// command) are injected through GridEnv so the math is exercised in isolation.
//
// Recovered FP/word constants (get_bytes; raw patterns in the .cpp):
//   flt_62568C = 0.5  — satisfaction ring-spread weight (Build)
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// Flat grid backing store. Reproduces the gilde.exe @0x1234988 region.
// ===========================================================================
constexpr int kCityGridRows     = 8;    // district rows (y)
constexpr int kCityGridCols     = 8;    // district cols (x)
constexpr int kCityGridRowBytes = 192;  // byte stride per row (orig)
constexpr int kCityGridCellBytes= 24;   // byte stride per cell (orig)

// Per-cell byte offsets within the 24-byte cell (relative to grid base 0x1234988).
constexpr int kGridOffCrimeArea   = 0;    // word_1234988
constexpr int kGridOffSatA        = 12;   // dword_1234994
constexpr int kGridOffSatB        = 16;   // dword_1234998
constexpr int kGridOffSatC        = 20;   // dword_123499C
constexpr int kGridOffCrimeCentre = 26;   // word_12349A2
constexpr int kGridOffSatWeight   = 36;   // flt_12349AC
constexpr int kGridOffSatDenom    = 40;   // flt_12349B0
constexpr int kGridOffSatCount    = 44;   // flt_12349B4

// Region big enough to cover the 3x3-ring's edge over-reach: CrimeRing(7,7)
// visits cell-x 8 / cell-y 8 (one past the nominal grid), so the max byte
// touched is 192*8 + 24*8 + 44 + 4 = 1776. The original's grid is followed by
// adjacent BSS that absorbs this; we replicate with explicit slack.
constexpr int kCityGridBytes = kCityGridRows * kCityGridRowBytes + 256;  // 1792
extern u8 g_cityGrid[kCityGridBytes];

// Typed cell accessors (faithful to the original base-pointer + index math).
i16&  GridCrimeArea(int x, int y);    // word_1234988[96*x + 12*y]
i16&  GridCrimeCentre(int x, int y);  // word_12349A2[96*x + 12*y]
i32&  GridSatA(int x, int y);         // dword_1234994 + (192*x + 24*y)
i32&  GridSatB(int x, int y);         // dword_1234998 + (192*x + 24*y)
i32&  GridSatC(int x, int y);         // dword_123499C + (192*x + 24*y)
float& GridSatWeight(int x, int y);   // flt_12349AC[48*x + 6*y]
float& GridSatDenom(int x, int y);    // flt_12349B0 + (192*x + 24*y)
float& GridSatCount(int x, int y);    // flt_12349B4[48*x + 6*y]

void CityGridReset();  // zero the whole region

// ===========================================================================
// Cross-module environment (injected leaves). A production backend wires these
// to the real sim/render/net globals; tests bind a deterministic mock.
// ===========================================================================

// A resident record the satisfaction rebuild scans (one entry of the
// dword_11BC760 list, stride 45 bytes). The original copies 45 bytes and reads
// a packed (scaled13 / scale19 / weight18 / lawId14) tuple; here those fields
// are surfaced so tests feed exact values.
struct GridResident {
    bool   active     = false;  // dword_11BC785 + j  (non-zero => live entry)
    i32    id         = -1;     // dword_11BC760 + j  (-1 => skip)
    i32    sceneNode  = 0;      // record+97: scene ptr (0 => no placement)
    int    typeByte   = 0;      // dword_13CE294[589*typeByte]: gates 0xB..0xD,0x10
    i16    scaled13   = 0;      // v16[13]: numerator
    i32    scale19    = 1;      // v19: denominator (must be non-zero)
    i32    weight18   = 0;      // v18: per-resident weight
    u8     lawId14    = 0;      // v16[14]: law id passed to GesetzGetRecord
    float  worldX     = 0.0f;   // record+97 -> +76: world coords for tiling
    float  worldZ     = 0.0f;
};

struct GridEnv {
    virtual ~GridEnv() = default;

    // VIBE_Math_RandomModulo(n): 0..n-1 (ANSI LCG). Used for fallback tile picks.
    virtual int RandomModulo(u16 n) = 0;

    // VIBE_Coord_WorldToCityTile: world coords -> {tileX, tileY} in 0..7, or false
    // if off the heightmap. The grid functions call this; tests can override it,
    // otherwise the default routes through Heightmap()/HeightmapDivisor().
    virtual bool WorldToCityTile(float worldX, float worldZ, int& tileX, int& tileY);

    // Heightmap leaf for the default WorldToCityTile: maps world (x,z) -> raw tile
    // (tx,tz); returns false if off-map. The divisor (map size >> 3) turns raw
    // tiles into 0..7 district indices.
    virtual bool HeightmapWorldToTile(float worldX, float worldZ, int& tx, int& tz) = 0;
    virtual int  HeightmapDivisor() = 0;  // (mapTileSpan >> 3), >= 1

    // The resident list the satisfaction rebuild scans (dword_11BC760 et al).
    virtual int Residents(const GridResident** out) = 0;  // returns count

    // VIBE_GameObject_ResolveEntityById: resolve the local-player object for a
    // crime add/remove; returns {worldX, worldZ, ok}. ok==false => grid untouched.
    virtual bool ResolvePlayerObject(float& worldX, float& worldZ) = 0;

    // The crime weight a Gesetz/law record carries (VIBE_Gesetz_GetRecord(lawId)
    // -> the v19/v16 weight the Add/Remove functions apply). Default 1.
    virtual int CrimeWeight(u8 lawId) { (void)lawId; return 1; }

    // VIBE_Command_RequestBuildOp86: broadcast a 40-byte grid command body.
    // Returns the command's result code; default no-op returns 0.
    virtual int RequestBuildOp86(const u8 body[40]) { (void)body; return 0; }
};

// ===========================================================================
// 0x577e04 — VIBE_Coord_WorldToCityTile  (__usercall eax=world@eax, edx=tx, ebx=tz)
// ===========================================================================
// Resolves world coords to a raw tile via HeightmapWorldToTile, then divides
// both axes by (mapTileSpan >> 3) to get a 0..7 district index. Returns false
// (0) off-map. Faithful to the divisor-then-/ sequence in the original.
bool CoordWorldToCityTile(GridEnv& env, float worldX, float worldZ,
                          int& tileX, int& tileY);

// ===========================================================================
// 0x577fc4 — VIBE_City_AddCrimeToGrid  (__usercall dl=lawId, ecx=outY, ebx=outX)
// ===========================================================================
// Resolves the local player's district, adds `CrimeWeight(lawId)` to the centre
// cell's crime-centre field (word_12349A2) and to every cell in the clamped 3x3
// ring's crime-area field (word_1234988). Writes the resolved (x,y) back through
// outX/outY. No-op (returns last tile y) if the player or its scene is missing.
int CityAddCrimeToGrid(GridEnv& env, u8 lawId, int* outX, int* outY);

// ===========================================================================
// 0x578110 — VIBE_City_RemoveCrimeFromGrid  (__fastcall(unused, lawId))
// ===========================================================================
// Exact inverse of AddCrimeToGrid: subtracts CrimeWeight(lawId) from the same
// centre + 3x3-ring cells. Does NOT write back tile coords (the original drops
// them). Returns the resolved tile y (or the last RNG fallback).
int CityRemoveCrimeFromGrid(GridEnv& env, u8 lawId);

// ===========================================================================
// 0x578240 — VIBE_City_PlaceRandomCrime  (__usercall al=districtX, ebx=outWorld)
// ===========================================================================
// Picks a uniformly random raw tile (tx, tz) inside district column `districtX`
// (band [districtX*span, (districtX+1)*span)) and converts it back to world
// coords via the heightmap. `outWorld` receives the world position. Returns the
// heightmap call's success byte.
struct WorldPoint { float x = 0, y = 0, z = 0; };
bool CityPlaceRandomCrime(GridEnv& env, int districtX, WorldPoint& outWorld);

// ===========================================================================
// 0x5788d4 — VIBE_City_BuildSatisfactionGrid  (void)
// ===========================================================================
// Zeroes the satisfaction work fields (A/B/C), then scans the resident list:
// each live, placed resident of an eligible type adds its (scaled13/scale19 *
// weight18) contribution to its own cell's satisfaction-weight field and spreads
// half of it (flt_62568C) to the clamped 3x3 ring (fields B/A). Faithful to the
// original's two interleaved accumulators.
void CityBuildSatisfactionGrid(GridEnv& env);

// ===========================================================================
// 0x579460 — VIBE_City_SendSyncCommand  (__userpurge eax=a1, ecx=clock, ..a3..a5)
// ===========================================================================
// Assembles the 40-byte opcode-86 broadcast body from a base template plus the
// four scalar arguments + game clock, and forwards it to RequestBuildOp86.
// `clockLo` is the 32-bit game-clock value the original copies from qword_13CE852.
int CitySendSyncCommand(GridEnv& env, i32 a1, i32 clockLo, i32 a3, i32 a4, i32 a5);

// ===========================================================================
// 0x5794e0 — VIBE_City_SendResetCommand  (__thiscall(seq))
// ===========================================================================
// Assembles the opcode-86 RESET body (word[0]=1, the 'tser' tag at word[9]) and
// forwards it to RequestBuildOp86. `seq` is the this/sequence value.
int CitySendResetCommand(GridEnv& env, i32 seq);

}  // namespace guild::world
