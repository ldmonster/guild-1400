#include "render/water_vertices.h"

#include "render/floorwater.h"    // AnimateWaterWaveGrid (16-vertex wave grid)
#include "render/water_anim.h"    // WaterTextureFrameIndex (speed table)
#include "util/float_math.h"      // Fmod (VIBE_Math_Fmod @0x5d3fb2)

namespace guild::render {

namespace {
// gilde.exe dbl_628AF4 == 6.283185307179586 (2π) — the trig amplitude `t` AND the
// modulus the phase-propagation Fmod reduces against (recovered via get_bytes).
constexpr double kTwoPi = 6.283185307179586;
} // namespace

// gilde.exe 0x5be428 — the phase-propagation Fmod loop (loc_5BE50B..loc_5BE52A).
//   for k in [0,4): phaseOut[k] = Fmod(waveSpeed[k]*dt + phase[k], 2π).
// Each iteration: fld speed[k]; fmul dt; fadd phase[k]; Fmod(_, 2π); store.
void PropagatePhases(const float waveSpeed[4], const float phase[4], double dt,
                     float phaseOut[4]) {
    for (int k = 0; k < 4; ++k) {
        double arg = (double)waveSpeed[k] * dt + (double)phase[k];
        // VIBE_Math_Fmod(arg, 2π): st1=arg dividend, st0=2π divisor.
        phaseOut[k] = (float)util::Fmod(arg, kTwoPi);
    }
}

// gilde.exe 0x5be428 — VIBE_Floor_AnimateWaterVertices (full per-frame driver).
void AnimateWaterVertices(WaterMesh* meshes, u32 count, i32 time,
                          FindGroupMemberFn findGroupMember, void* ctx) {
    for (u32 i = 0; i < count; ++i) {
        WaterMesh& m = meshes[i];

        // ---- TEXTURE ADVANCE (loc_5BE45C..loc_5BE4A0) ---------------------
        //   if (texPtr && texPtr[0x70] && (texPtr[0x72] & 0xF)) {
        //       frame = (time / speedTable[sel]) % memberCount;
        //       texMember = FindGroupMember((texPtr-texBase)>>7, frame);
        //   }
        if (m.hasTexture && m.texMemberCount != 0) {
            u8 sel = (u8)(m.texSpeedNibble & 0x0Fu);
            if (sel != 0) {
                i32 frame =
                    WaterTextureFrameIndex((u32)time, sel, m.texMemberCount);
                m.activeMember =
                    findGroupMember(m.groupId, (u8)frame, ctx);
            }
        }

        // ---- TIME GATE (loc_5BE4A3): only animate when dt > 0 -------------
        //   eax = time - lastTime;  if (eax <= 0) skip the wave/accum work.
        i32 dtInt = time - m.lastTime;
        if (dtInt <= 0)
            continue;

        // ---- TEXTURE-COORD ACCUMULATORS (5be4b9..5be4fb) ------------------
        //   dt = (float)(time-lastTime);
        //   texAccumA = Fmod(texRateA*dt + texAccumA, 1.0);
        //   texAccumB = Fmod(1.0,           texAccumA);
        float dtF = (float)dtInt;
        double tA = (double)m.texRateA * (double)dtF + (double)m.texAccumA;
        double newAccumA = util::Fmod(tA, 1.0);           // dividend=tA, /1.0
        double newAccumB = util::Fmod(1.0, newAccumA);    // dividend=1.0, /accumA
        m.texAccumA = (float)newAccumA;
        m.texAccumB = (float)newAccumB;

        // ---- PHASE PROPAGATION (loc_5BE50B): faithful +0x134 write that ---
        // overlaps the +0x138 phase read by one float. The loop writes
        //   phaseOut buffer [-1..2] (bytes +0x134..+0x140) from speeds [0..3]
        //   and phases  [0..3], then the wave grid reads phase[0..3]
        //   (bytes +0x138..+0x144). Net effect (reproduced exactly):
        //     phase[0] <- Fmod(speed[1]*dt + phase[1], 2π)
        //     phase[1] <- Fmod(speed[2]*dt + phase[2], 2π)
        //     phase[2] <- Fmod(speed[3]*dt + phase[3], 2π)
        //     phase[3] <- (unchanged: +0x144 is never written)
        // The (float)dt the FILD reloads is var_14 == dtF.
        float prop[4];
        PropagatePhases(m.waveSpeed, m.phase, (double)dtF, prop);
        // prop[k] was written to byte +0x134+4k == phase index (k-1). Shift down:
        m.phaseOut[0] = prop[0];          // +0x134 (scratch, never read by grid)
        m.phase[0]    = prop[1];          // +0x138
        m.phase[1]    = prop[2];          // +0x13C
        m.phase[2]    = prop[3];          // +0x140
        // m.phase[3] (+0x144) intentionally untouched.

        // ---- WAVE GRID (loc_5BE53C): 16 vec4 from amp/phase, t = 2π -------
        AnimateWaterWaveGrid(m.waveOut, m.amp, m.phase, kTwoPi);

        // ---- LATCH (5be623): lastTime = time -----------------------------
        m.lastTime = time;
    }
}

} // namespace guild::render
