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
        // VERIFIED against the live 0x5be428 disasm (Fmod @0x5d3fb2 == fprem,
        // i.e. st0 mod st1 with st0=dividend, st1=divisor):
        //   dt = (float)(time-lastTime);
        //   5be4b9: fld a3[3](texRateA); fmul dt; fadd a3[82](texAccumA); fld1;
        //           fxch; Fmod -> (texRateA*dt + texAccumA) mod 1.0
        //   5be4d7: fld a3[4](texRateB); fmul dt; fadd a3[83](texAccumB); fld1;
        //           fxch; Fmod -> (texRateB*dt + texAccumB) mod 1.0
        //   5be4f3: fxch; fstp a3[82]=resultA; fstp a3[83]=resultB.
        // (The Hex-Rays `Fmod(1.0, v5)` for the second accumulator is a
        //  misrendering — the disasm fadds a3[14Ch] and divides by the fld1
        //  divisor, identical structure to the first accumulator.)
        float dtF = (float)dtInt;
        double tA = (double)m.texRateA * (double)dtF + (double)m.texAccumA;
        double tB = (double)m.texRateB * (double)dtF + (double)m.texAccumB;
        double newAccumA = util::Fmod(tA, 1.0);           // dividend=tA, /1.0
        double newAccumB = util::Fmod(tB, 1.0);           // dividend=tB, /1.0
        m.texAccumA = (float)newAccumA;
        m.texAccumB = (float)newAccumB;

        // ---- PHASE PROPAGATION (loc_5BE50B..loc_5BE52A) -------------------
        // VERIFIED against the live 0x5be428 disasm: the loop runs edx from a3
        // to a3+0x10 (4 iterations). Each iteration reads waveSpeed[k] at
        // [edx+0x18] and phase[k] at [edx+0x138], then does `add edx,4` and
        // stores to [edx+0x134] == the SAME byte +0x138+4k it just read. So it
        // is an IN-PLACE update of all four phase accumulators:
        //     phase[k] = Fmod(waveSpeed[k]*dt + phase[k], 2π)   for k in [0,4)
        // (There is NO +0x134 scratch write and NO one-float overlap shift; an
        // earlier reconstruction misread the post-increment store offset.)
        // The (float)dt the FILD reloads is var_14 == dtF.
        float prop[4];
        PropagatePhases(m.waveSpeed, m.phase, (double)dtF, prop);
        m.phase[0] = prop[0];             // +0x138
        m.phase[1] = prop[1];             // +0x13C
        m.phase[2] = prop[2];             // +0x140
        m.phase[3] = prop[3];             // +0x144
        m.phaseOut[0] = prop[0];          // mirror (unused by the grid; kept for tests)

        // ---- WAVE GRID (loc_5BE53C): 16 vec4 from amp/phase, t = 2π -------
        AnimateWaterWaveGrid(m.waveOut, m.amp, m.phase, kTwoPi);

        // ---- LATCH (5be623): lastTime = time -----------------------------
        m.lastTime = time;
    }
}

} // namespace guild::render
