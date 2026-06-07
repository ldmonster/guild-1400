#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — WATER SURFACE ANIMATION DRIVER: the full top-level loop of
//
//   0x5be428  VIBE_Floor_AnimateWaterVertices
//
// This is the per-frame water-mesh animator. The two halves it drives were
// already reconstructed standalone:
//   - the TEXTURE advance (render/water_anim.{h,cpp}: WaterTextureFrameIndex /
//     AnimateWaterTexture, speed table dword_5D93C8), and
//   - the 16-vertex SINE/COSINE wave grid (render/floorwater.{h,cpp}:
//     AnimateWaterWaveGrid, constants dbl_628AEC.. = 2.7/2.4/2.5/2.6/π/2.2/4.0).
// This module assembles them into the original driver and reproduces the parts
// that live ONLY in the driver: the time gate, the two Fmod phase accumulators
// (+0x148/+0x14C), the four-element phase-propagation Fmod loop (+0x134..+0x140
// from speeds +0x18..+0x24), and the lastTime latch (+0x150).
//
// MESH RECORD (0x158 bytes == 86 floats, the `a3` stride the engine walks):
//   float[0]   (+0x00)  texture record ptr   (opaque; texPtr != 0 gates texture anim)
//   float[1]   (+0x04)  active texture member (written by FindGroupMember)
//   float[3]   (+0x0C)  texAnimRateA   (* (time-lastTime) -> accumA)
//   float[4]   (+0x10)  texAnimRateB   (* (time-lastTime) -> accumB)
//   float[6..9](+0x18..+0x24)  waveSpeed[0..3]  (phase-propagation rates)
//   float[10..13](+0x28..+0x34) amp[0..3]       (wave amplitudes, AnimateWaterWaveGrid)
//   float[14..]  (+0x38)  16 output vec4 (the wave grid, 256 bytes)
//   float[77..80](+0x134..+0x140) phaseOut[0..3] (new propagated phases written here)
//   float[78..81](+0x138..+0x144) phase[0..3]    (phase accumulators; READ by the
//                                                  wave grid, ADVANCED by the prop loop)
//   float[82]  (+0x148)  texAccumA  (Fmod(rateA*dt + texAccumA, 1.0))
//   float[83]  (+0x14C)  texAccumB  (Fmod(1.0, texAccumA))
//   float[84]  (+0x150)  lastTime   (time of the previous animated frame; gate)
//   byte +0x70 (within texPtr)  memberCount;  byte +0x72  speedSel nibble
//
// Texture record byte fields live inside the *texture* record texPtr points at:
//   *(u8*)(texPtr+0x70) memberCount, *(u8*)(texPtr+0x72) flags (low nibble = speed).
//   groupId = (texPtr - texBase) >> 7 (128-byte texture-record stride).
//
// The texture bank (texBase == dword_1406A84, FindGroupMember @0x5daec0) is
// data-coupled global state, injected here as a callback so the driver is fully
// golden-vector testable.
// =============================================================================
namespace guild::render {

// One animated water mesh — the 86-float (0x158-byte) record the engine walks,
// modelled by name (only the fields the driver reads/writes). The 256-byte wave
// output grid is `waveOut` (16 vec4); `phase[k]` is BOTH the wave input and the
// accumulator the propagation loop advances.
struct WaterMesh {
    // Texture animation inputs (the texture record texPtr points at):
    bool  hasTexture;     // float[0] != 0
    i32   groupId;        // (texPtr - texBase) >> 7
    u8    texMemberCount; // *(u8*)(texPtr+0x70)
    u8    texSpeedNibble; // *(u8*)(texPtr+0x72) & 0x0F
    u32   activeMember;   // float[1]  (texture member id; updated in place)

    float texRateA;       // float[3]  (+0x0C)
    float texRateB;       // float[4]  (+0x10)
    float texAccumA;      // float[82] (+0x148)
    float texAccumB;      // float[83] (+0x14C)

    float waveSpeed[4];   // float[6..9]   (+0x18..+0x24)
    float amp[4];         // float[10..13] (+0x28..+0x34)
    float phase[4];       // float[78..81] (+0x138..+0x144)  (in/out accumulators)
    float phaseOut[4];    // float[77..80] (+0x134..+0x140)  (propagation scratch)
    float waveOut[64];    // float[14..77] (+0x38) 16 vec4 wave displacement grid

    i32   lastTime;       // float[84] (+0x150) latch
};

// Injected texture-group member lookup (VIBE_Texture_FindGroupMember @0x5daec0):
//   member = findGroupMember(groupId, frameByte)
using FindGroupMemberFn = u32 (*)(i32 groupId, u8 frameByte, void* ctx);

// gilde.exe 0x5be428 — VIBE_Floor_AnimateWaterVertices (full driver).
//   result@eax = mesh array, edx = count, ebx = first mesh record.
// For each of `count` meshes:
//   1. TEXTURE: if (hasTexture && texMemberCount && speedSel)
//          activeMember = findGroupMember(groupId,
//                            (time / speedTable[speedSel]) % texMemberCount);
//   2. GATE: only animate when (time - lastTime) > 0 (dt > 0).
//   3. TEX ACCUM: dt=(float)(time-lastTime);
//        texAccumA = Fmod(texRateA*dt + texAccumA, 1.0);
//        texAccumB = Fmod(1.0, texAccumA);
//   4. PHASE PROPAGATION (4 elements):
//        phaseOut[k] = Fmod(waveSpeed[k]*dt + phase[k], 2π);   k=0..3
//      then the wave grid reads phase[k] (the original's +0x138 read overlaps the
//      +0x134 write by one float; reproduced exactly via PropagatePhases below).
//   5. WAVE GRID: AnimateWaterWaveGrid(waveOut, amp, phase, 2π).
//   6. lastTime = time.
// Meshes whose dt <= 0 keep their previous wave grid and accumulators (only the
// texture advance, which is dt-independent, still runs).
void AnimateWaterVertices(WaterMesh* meshes, u32 count, i32 time,
                          FindGroupMemberFn findGroupMember, void* ctx);

// gilde.exe 0x5be428 — the phase-propagation Fmod loop (step 4), isolated for
// golden-vector testing. Writes phaseOut[k] = Fmod(waveSpeed[k]*dt + phase[k], 2π)
// for k in [0,4). `dt` is (float)(time - lastTime) as a double, matching the FILD.
void PropagatePhases(const float waveSpeed[4], const float phase[4], double dt,
                     float phaseOut[4]);

} // namespace guild::render
