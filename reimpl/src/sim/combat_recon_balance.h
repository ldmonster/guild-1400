#pragma once
// ===========================================================================
// combat_recon_balance — reconstructed pure-math leaves of the combat /
// physics-dispatch cluster of gilde.exe (Die Gilde / Europa 1400).
//
// Strict 1:1 from the Hex-Rays decompile (reference of record). This unit only
// contains the functions whose logic is *pure* (integer / float table math with
// no live coupling to the entity/RNG/render subsystems beyond inert hooks):
//
//   * gilde.exe 0x48b744 — VIBE_Combat_InitDefaultParameters
//       Initializes the global combat-balance table (dword_B59C40), a
//       6 modes x 5 buckets x 6 role-weight grid (stride 120 bytes / 30 floats
//       per mode, 24 bytes / 6 floats per bucket). After loading the raw
//       constants it runs two post-passes:
//         (1) scale each cell by 0.01 and make the 6 columns CUMULATIVE within
//             each bucket row (col[i] += col[i-1]);
//         (2) dedup — zero any later column equal to an earlier one in the row.
//       The resulting cumulative thresholds in [0,1] are what
//       VIBE_Combat_ComputeUnitBalanceWeights / AssignUnitsToRoles read out of
//       dword_B59C40[30*mode + 6*bucket + col] (see sim/combat_battle.h).
//
//   * gilde.exe 0x5d8e80 — VIBE_Physics_Update
//       Sprite/velocity animation dispatch over a 17-byte-stride state table
//       (dword_1406420). Pure index + bitfield-shift math; the two render
//       leaves it calls (VIBE_Velocity_Apply @0x5d883c, VIBE_Animation_Basic
//       @0x5d85b8) live in the render module and are modeled here as inert
//       hooks so the integer dispatch can be exercised 1:1.
//
// Other targets in the cluster are NOT here (see combat_recon_balance.cpp
// header comment for the per-address omit/already-present rationale).
// ===========================================================================
#include <array>

#include "guild/common/types.h"

namespace guild {
namespace sim {

using namespace ::guild;

// --- combat balance table --------------------------------------------------

// dword_B59C40 — 6 modes * 5 buckets * 6 columns of float32 = 180 floats.
// The original is a flat global with 120-byte mode stride (30 floats) and a
// 24-byte bucket stride (6 floats). We mirror the exact flat 180-float layout
// so byte-offset arithmetic (30*mode + 6*bucket + col) is identical.
constexpr int kCombatBalanceModes   = 6;   // outer loop bound
constexpr int kCombatBalanceBuckets = 5;   // v3: base..base+120 step 24 -> 5 rows
constexpr int kCombatBalanceCols    = 6;   // innermost loop bound
constexpr int kCombatBalanceFloats  =
    kCombatBalanceModes * kCombatBalanceBuckets * kCombatBalanceCols; // 180

// gilde.exe 0x48b744 — VIBE_Combat_InitDefaultParameters.
// Fills `table` (must hold >= kCombatBalanceFloats floats; the original global
// is larger but only the first 180 floats participate in the two math passes)
// with the default constants, then applies the cumulative + dedup passes.
// Returns the original return value (the loop's last `v11+120` == 840).
int InitDefaultParameters(float* table);

// Convenience wrapper returning the post-processed 180-float table by value.
std::array<float, kCombatBalanceFloats> InitDefaultParametersTable();

// --- physics sprite-dispatch state record ----------------------------------

// gilde.exe — one entry of dword_1406420 (17-byte stride). Field offsets are
// taken from the byte displacements in VIBE_Physics_Update:
//   +0x00 dword bank            (dword_1406420)
//   +0x06 byte  shapeIndex      (word_1406426, low byte used as arg5)
//   +0x08 dword gateB           (dword_1406428; gates the velocity branch)
//   +0x0D word  velY            (word_140642D; -> v6 high half)
//   +0x0F word  velX            (word_140642F; -> v7 high half)
GUILD_PACKED_BEGIN
struct PhysicsSpriteState {
    u32 bank;        // +0x00
    u8  pad1;        // +0x04
    u8  pad2;        // +0x05
    u8  shapeIndex;  // +0x06
    u8  pad3;        // +0x07
    u32 gateB;       // +0x08
    u8  pad4;        // +0x0C
    i16 velY;        // +0x0D
    i16 velX;        // +0x0F
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(PhysicsSpriteState) == 17, "Physics state stride must be 17");

// Inert render-leaf hooks. The real implementations (render module) submit
// frames into a sprite bank; here the default hooks record the last call so the
// integer dispatch in VIBE_Physics_Update can be verified 1:1 without the
// render backend. Override via SetPhysicsRenderHooks for wiring.
struct PhysicsRenderCall {
    bool  called = false;
    int   x = 0, y = 0;
    u32   bank = 0;
    int   arg = 0;        // a2 passed through
    u8    shape = 0;
};

struct PhysicsRenderHooks {
    // mirrors VIBE_Velocity_Apply(x, y, bank, a2, shape)
    void (*velocityApply)(int x, int y, u32 bank, int a2, u8 shape) = nullptr;
    // mirrors VIBE_Animation_Basic(x, y, bank, a2, shape) -> int
    int  (*animationBasic)(int x, int y, u32 bank, int a2, u8 shape) = nullptr;
};

void SetPhysicsRenderHooks(const PhysicsRenderHooks& hooks);

// Last-call probes for the default inert hooks (test/inspection only).
const PhysicsRenderCall& LastVelocityApplyCall();
const PhysicsRenderCall& LastAnimationBasicCall();
void ResetPhysicsRenderProbes();

// gilde.exe 0x5d8e80 — VIBE_Physics_Update.
// a1 = state index, a2 = passthrough arg (a2@<edx>), a3 = packed input whose
// HIWORD seeds v6's low half (HIWORD(a3) -> LOWORD(v6)).
// Returns: if the bank slot is empty, 17*a1 (the byte offset). Otherwise the
// return of VIBE_Animation_Basic (via the hook).
int Physics_Update(int a1, int a2, int a3, PhysicsSpriteState* table);

} // namespace sim
} // namespace guild
