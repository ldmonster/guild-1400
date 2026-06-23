// ===========================================================================
// combat_recon_balance.cpp — reconstructed pure-math leaves of the combat /
// physics-dispatch cluster of gilde.exe (Die Gilde). Strict 1:1 translation
// from the Hex-Rays decompile (reference of record).
//
// IMPLEMENTED HERE (faithful, pure logic):
//   * 0x48b744 VIBE_Combat_InitDefaultParameters  -> InitDefaultParameters
//   * 0x5d8e80 VIBE_Physics_Update                -> Physics_Update
//
// CLUSTER DISPOSITION (per-address rationale):
//   already present (reused, NOT redefined):
//     0x48d4d8 VIBE_Combat_ClassifyTileType  -> sim/combat_orders.cpp
//     0x5d85b8 VIBE_Animation_Basic          -> render/animation_decode.cpp
//     0x5d883c VIBE_Velocity_Apply           -> render module (frame submit)
//   omitted (rule 8 — no pure combat/physics math; pure subsystem glue with no
//   faithful standalone form: entity messaging / text rendering / object
//   attachment / picking, all coupled to person/text/cutscene/object/heightmap
//   subsystems and large mutable global state):
//     0x485e88 VIBE_Combat_AttachObjectMesh      (object-def lookup + bone attach)
//     0x48759c VIBE_Combat_ShowCommandFeedback   (text + HUD banner glue)
//     0x487ec4 VIBE_Combat_PickObjectUnderCursor (cursor pick + global mutation)
//     0x4895cc VIBE_Combat_CreateFlagObject      (universe-node spawn glue)
//     0x48fcf0 VIBE_Combat_BuildBattleInfoText   (text/speech-packet glue)
//     0x4a4944 VIBE_Combat_SpawnBloodPool        (RNG + universe-node spawn glue)
//     0x4a5270 VIBE_Duel_ReportToOffice          (person/office/messaging glue)
//     0x4a62e8 VIBE_Duel_CheckParticipants       (person/messaging/law glue)
//     0x4a69b4 VIBE_Duel_BuildMessages           (person/text/speech glue)
//     0x427b60 VIBE_Collision_ResolveMeshAgainstTerrain
//                 (mesh/terrain/object-transform engine glue; its only pure part
//                  is an AABB min/max fold that has no standalone meaning without
//                  the mesh-bound/terrain-scan leaves — omitted to avoid faking)
// ===========================================================================
#include "sim/combat_recon_balance.h"

namespace guild {
namespace sim {

// ---------------------------------------------------------------------------
// 0x48b744 — VIBE_Combat_InitDefaultParameters
// ---------------------------------------------------------------------------
//
// The original first writes ~180 float constants into the global table
// dword_B59C40 (a 6 modes x 5 buckets x 6 columns grid; 120-byte mode stride,
// 24-byte bucket stride, 4-byte column stride). It then runs two passes:
//
//   Pass A (0x48baaf..0x48baf2):
//     for v1 in 0..5:               // mode
//       for v3 = 120*v1; v3 != 120*(v1+1); v3 += 24:   // bucket (5 rows)
//         for v4 = 0; v4 < 6; ++v4, v5 += 4:           // column
//           table[v5] *= dbl_61B8BC;                   // 0.01
//           if (v4) table[v5] += table[v5-4];          // cumulative
//
//   Pass B (0x48bafb..0x48bb7a):
//     for v12 in 0..5:
//       for v13 = 120*v12; v13 != 120*(v12+1); v13 += 24:
//         for v6 = 0; v6 < 6; ++v6, v7 += 4:
//           for v8 = v6+1; v8 < 6; ++v8, v9 += 4:
//             if (table[v9] == table[v7]) table[v9] = 0;
//
//   Returns `result = v11 + 120` from the last (v12==5) pass; with v11 stepping
//   120->240->360->480->600->720 across the 6 modes, the final result is 840.
//
// Note flt_B59C3C (= 0.0) sits at table[-1]; it is never read because Pass A
// only does the `+= prev` when v4 != 0, so the read is always table[v5-4] with
// v5-4 in-bounds. We translate the loops exactly.
//
// The constant block: the raw default values (already byte-exact float bit
// patterns from the decompile, all whole numbers 5..100).
namespace {

// 30 rows (6 modes x 5 buckets) of 6 columns each = 180 floats, in flat order
// matching dword_B59C40[30*mode + 6*bucket + col].
constexpr float kDefaultParams[kCombatBalanceFloats] = {
    // mode 0
    10, 10,  0,  0, 30, 50,
    40, 20,  0,  0, 20, 20,
    80, 20,  0,  0,  0,  0,   // mode0 bucket2 col0 = 80 (ecx=0x42A00000, preserved across SetGrayColorThunk; B59C70)
    70, 30,  0,  0,  0,  0,
    50, 50,  0,  0,  0,  0,
    // mode 1
    10, 10,  0,  0, 30, 50,
    80, 20,  0,  0,  0,  0,
    80, 20,  0,  0,  0,  0,
    60, 40,  0,  0,  0,  0,
    50, 50,  0,  0,  0,  0,
    // mode 2
     5,  0,  5,  0, 20, 70,
    50,  0, 10,  0, 20, 20,
    80,  0, 20,  0,  0,  0,
    70,  0, 30,  0,  0,  0,
    50,  0, 50,  0,  0,  0,
    // mode 3
    20,  0, 80,  0,  0,  0,
    30,  0, 70,  0,  0,  0,
    40,  0, 60,  0,  0,  0,
    60,  0, 40,  0,  0,  0,
    70,  0, 30,  0,  0,  0,
    // mode 4
    20,  0,  0,  0, 10, 70,
    30,  0,  0,  0, 20, 50,
   100,  0,  0,  0,  0,  0,
   100,  0,  0,  0,  0,  0,
   100,  0,  0,  0,  0,  0,
    // mode 5
    20,  0,  0,  0, 10, 70,
    30,  0,  0,  0, 20, 50,
   100,  0,  0,  0,  0,  0,
   100,  0,  0,  0,  0,  0,
   100,  0,  0,  0,  0,  0,
};

// dbl_61B8BC == 0.01 (double 0x3f847ae147ae147b).
constexpr double kScale_dbl_61B8BC = 0.01;

} // namespace

int InitDefaultParameters(float* table) {
    // --- load constants (mirrors the long block of integer-bit assignments) ---
    for (int i = 0; i < kCombatBalanceFloats; ++i)
        table[i] = kDefaultParams[i];

    // --- Pass A: scale by 0.01 then make columns cumulative within each row ---
    // Outer mode loop (v1 in 0..5); bucket rows v3 from 120*v1 step 24 until
    // 120*(v1+1); 6 columns. Working entirely in *float* (32-bit) like the orig.
    int v2 = 120;
    for (int v1 = 0; v1 < kCombatBalanceModes; ++v1) {
        int v3 = 120 * v1;
        do {
            int v5 = v3; // byte offset
            for (int v4 = 0; v4 < kCombatBalanceCols; ++v4, v5 += 4) {
                float* cell = reinterpret_cast<float*>(
                    reinterpret_cast<char*>(table) + v5);
                *cell = static_cast<float>(static_cast<double>(*cell) *
                                           kScale_dbl_61B8BC);
                if (v4) {
                    float* prev = reinterpret_cast<float*>(
                        reinterpret_cast<char*>(table) + v5 - 4);
                    *cell = *prev + *cell;
                }
            }
            v3 += 24;
        } while (v3 != v2);
        v2 += 120;
    }

    // --- Pass B: zero any later column equal to an earlier one in the row -----
    int v11 = 120;
    int result = 0;
    for (int v12 = 0; v12 < kCombatBalanceModes; ++v12) {
        int v13 = 120 * v12;
        do {
            int v7 = v13;
            for (int v6 = 0; v6 < kCombatBalanceCols; ++v6, v7 += 4) {
                int v8 = v6 + 1;
                if (v8 < kCombatBalanceCols) {
                    int v9 = v13 + 4 * v8;
                    do {
                        float* later = reinterpret_cast<float*>(
                            reinterpret_cast<char*>(table) + v9);
                        float* base = reinterpret_cast<float*>(
                            reinterpret_cast<char*>(table) + v7);
                        if (*later == *base)
                            *reinterpret_cast<int*>(
                                reinterpret_cast<char*>(table) + v9) = 0;
                        ++v8;
                        v9 += 4;
                    } while (v8 < kCombatBalanceCols);
                }
            }
            v13 += 24;
        } while (v13 != v11);
        result = v11 + 120;
        v11 += 120;
    }

    return result; // 840 (final v11+120 with v11==720)
}

std::array<float, kCombatBalanceFloats> InitDefaultParametersTable() {
    std::array<float, kCombatBalanceFloats> t{};
    InitDefaultParameters(t.data());
    return t;
}

// ---------------------------------------------------------------------------
// 0x5d8e80 — VIBE_Physics_Update
// ---------------------------------------------------------------------------
//
//   result = 17 * a1;
//   if ( table[a1].bank ) {
//     LOWORD(v6) = HIWORD(a3);
//     HIWORD(v6) = table[a1].velY;
//     HIWORD(v7) = table[a1].velX;
//     if ( table[a1].gateB )
//       VIBE_Velocity_Apply((v6>>16)+6, (v7>>16)+6, table[a1].bank, a2,
//                           table[a1].shapeIndex);
//     return VIBE_Animation_Basic(v6>>16, v7>>16, table[a1].bank, a2,
//                                 table[a1].shapeIndex);
//   }
//   return result;   // 17*a1
//
// v6/v7 are 32-bit ints whose HIGH 16 bits carry the signed velocity and whose
// LOW 16 bits are seeded from HIWORD(a3) (v6) / 0 (v7, never set). The `>>16`
// is an arithmetic shift on a signed int, so velocity sign is preserved.

namespace {

PhysicsRenderHooks g_hooks;
PhysicsRenderCall  g_lastVel;
PhysicsRenderCall  g_lastAnim;

void DefaultVelocityApply(int x, int y, u32 bank, int a2, u8 shape) {
    g_lastVel = PhysicsRenderCall{true, x, y, bank, a2, shape};
}
int DefaultAnimationBasic(int x, int y, u32 bank, int a2, u8 shape) {
    g_lastAnim = PhysicsRenderCall{true, x, y, bank, a2, shape};
    return 1; // VIBE_Animation_Basic returns 1 on a valid submit
}

} // namespace

void SetPhysicsRenderHooks(const PhysicsRenderHooks& hooks) { g_hooks = hooks; }
const PhysicsRenderCall& LastVelocityApplyCall()  { return g_lastVel; }
const PhysicsRenderCall& LastAnimationBasicCall() { return g_lastAnim; }
void ResetPhysicsRenderProbes() {
    g_lastVel = PhysicsRenderCall{};
    g_lastAnim = PhysicsRenderCall{};
}

int Physics_Update(int a1, int a2, int a3, PhysicsSpriteState* table) {
    int result = 17 * a1; // byte offset of slot a1 (mirrors `17 * a1`)
    PhysicsSpriteState& s = table[a1];
    if (s.bank) {
        // LOWORD(v6) = HIWORD(a3); HIWORD(v6) = velY.
        // Assemble in unsigned space (the original does this in a 32-bit register,
        // a well-defined two's-complement bitfield write); shifting a *signed*
        // negative velocity by 16 would be UB. Reinterpreting back to i32 lets the
        // later arithmetic `>> 16` recover the signed velocity, exactly as the
        // original HIWORD(v6) = velY + signed shift.
        i32 v6 = static_cast<i32>(
            (static_cast<u32>(static_cast<u16>(s.velY)) << 16) |
            static_cast<u16>(static_cast<u32>(a3) >> 16));
        // HIWORD(v7) = velX; low half untouched (0).
        i32 v7 = static_cast<i32>(static_cast<u32>(static_cast<u16>(s.velX)) << 16);

        if (s.gateB) {
            int x = (v6 >> 16) + 6;
            int y = (v7 >> 16) + 6;
            if (g_hooks.velocityApply)
                g_hooks.velocityApply(x, y, s.bank, a2, s.shapeIndex);
            else
                DefaultVelocityApply(x, y, s.bank, a2, s.shapeIndex);
        }
        int rx = v6 >> 16;
        int ry = v7 >> 16;
        if (g_hooks.animationBasic)
            return g_hooks.animationBasic(rx, ry, s.bank, a2, s.shapeIndex);
        return DefaultAnimationBasic(rx, ry, s.bank, a2, s.shapeIndex);
    }
    return result;
}

} // namespace sim
} // namespace guild
